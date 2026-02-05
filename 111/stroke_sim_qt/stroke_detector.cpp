#include "stroke_detector.h"

#include <algorithm>
#include <cmath>

RealtimeStrokeDetector::RealtimeStrokeDetector() {
  for (int i = 0; i < 3; ++i) {
    filters_[i].set_params(cutoff_hz, sample_rate);
    ema_filters_[i] = ExponentialMovingAverage(0.25);
    accel_history_[i].clear();
    for (int j = 0; j < WINDOW_SIZE; ++j) {
      accel_history_[i].push_back(0.0);
    }
  }
}

void RealtimeStrokeDetector::reset() {
  for (int i = 0; i < 3; ++i) {
    filters_[i].set_params(cutoff_hz, sample_rate);
    ema_filters_[i].reset();
    accel_history_[i].clear();
    for (int j = 0; j < WINDOW_SIZE; ++j) {
      accel_history_[i].push_back(0.0);
    }
  }
  active_axis_ = 2;
  stroke_state_ = STATE_BACKGROUND;
  stroke_count_ = 0;
  stroke_rate_ = 0.0;
  phase_start_time_ = 0;
  stroke_start_time_ = 0;
  peak_max_value_ = 0.0;
  peak_max_time_ = 0;
  peak_max_raw_ = 0.0;
  peak_max_filtered_ = 0.0;
  trough_min_value_ = 0.0;
  trough_min_time_ = 0;
  trough_min_raw_ = 0.0;
  trough_min_filtered_ = 0.0;
  recovery_counter_ = 0;
  background_mean_ = 0.0;
  background_std_ = 0.1;
  last_stroke_time_ = 0;
  is_calibrating_ = true;
  calibration_complete_ = false;
  filtered_t_.clear();
  filtered_x_.clear();
  filtered_y_.clear();
  filtered_z_.clear();
  raw_t_.clear();
  raw_x_.clear();
  raw_y_.clear();
  raw_z_.clear();
  detected_strokes_.clear();
}

void RealtimeStrokeDetector::ButterworthFilter::set_params(double cutoff_hz,
                                                           double sample_rate) {
  (void)cutoff_hz;
  (void)sample_rate;
  // Match device-side fixed coefficients (IMUManager).
  b0 = 0.00515;
  b1 = 0.01030;
  b2 = 0.00515;
  a1 = -1.7873;
  a2 = 0.8080;

  x1 = 0.0;
  x2 = 0.0;
  y1 = 0.0;
  y2 = 0.0;
}

double RealtimeStrokeDetector::ButterworthFilter::filter(double input) {
  if (x1 == 0.0 && x2 == 0.0 && y1 == 0.0 && y2 == 0.0) {
    x1 = input;
    x2 = input;
    y1 = input;
    y2 = input;
  }

  double output = (b0 * input + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2);
  x2 = x1;
  x1 = input;
  y2 = y1;
  y1 = output;
  return output;
}

double RealtimeStrokeDetector::ExponentialMovingAverage::filter(double input) {
  if (value == 0.0) {
    value = input;
  } else {
    value = alpha * input + (1.0 - alpha) * value;
  }
  return value;
}

void RealtimeStrokeDetector::ExponentialMovingAverage::reset() {
  has_value = false;
  value = 0.0;
}

void RealtimeStrokeDetector::configure_filters(double cutoff, double sr) {
  cutoff_hz = cutoff;
  sample_rate = sr;
  for (int i = 0; i < 3; ++i) {
    filters_[i].set_params(cutoff_hz, sample_rate);
    ema_filters_[i].reset();
  }
}

void RealtimeStrokeDetector::set_active_axis(int axis) {
  if (axis >= 0 && axis < 3) {
    active_axis_ = axis;
  }
}

size_t RealtimeStrokeDetector::calibration_samples() const {
  const double samples = (static_cast<double>(CALIBRATION_DURATION) * sample_rate) / 1000.0;
  if (samples < 1.0) {
    return 1;
  }
  return static_cast<size_t>(samples);
}

static void compute_mean_std(const std::deque<double> &data, size_t start,
                             size_t count, double &mean, double &stddev) {
  if (count == 0) {
    mean = 0.0;
    stddev = 0.0;
    return;
  }
  double sum = 0.0;
  for (size_t i = start; i < start + count; ++i) {
    sum += data[i];
  }
  mean = sum / static_cast<double>(count);

  if (count < 2) {
    stddev = 0.0;
    return;
  }
  double sum_sq = 0.0;
  for (size_t i = start; i < start + count; ++i) {
    double diff = data[i] - mean;
    sum_sq += diff * diff;
  }
  stddev = std::sqrt(sum_sq / static_cast<double>(count - 1));
}

std::optional<StrokeEvent> RealtimeStrokeDetector::process_sample(uint64_t timestamp_ms,
                                                                  double acc_x,
                                                                  double acc_y,
                                                                  double acc_z) {
  double bw_x = filters_[0].filter(acc_x);
  double bw_y = filters_[1].filter(acc_y);
  double bw_z = filters_[2].filter(acc_z);

  double filtered_x = ema_filters_[0].filter(bw_x);
  double filtered_y = ema_filters_[1].filter(bw_y);
  double filtered_z = ema_filters_[2].filter(bw_z);

  raw_t_.push_back(timestamp_ms / 1000.0);
  raw_x_.push_back(acc_x);
  raw_y_.push_back(acc_y);
  raw_z_.push_back(acc_z);

  const double filtered[3] = {filtered_x, filtered_y, filtered_z};
  for (int i = 0; i < 3; ++i) {
    if (static_cast<int>(accel_history_[i].size()) >= WINDOW_SIZE) {
      accel_history_[i].pop_front();
    }
    accel_history_[i].push_back(filtered[i]);
  }

  filtered_t_.push_back(timestamp_ms / 1000.0);
  filtered_x_.push_back(filtered_x);
  filtered_y_.push_back(filtered_y);
  filtered_z_.push_back(filtered_z);

  if (is_calibrating_) {
    const size_t cal_samples = calibration_samples();
    if (cal_samples > 0 && accel_history_[active_axis_].size() >= cal_samples) {
      const auto &hist = accel_history_[active_axis_];
      const size_t start = hist.size() - cal_samples;
      compute_mean_std(hist, start, cal_samples, background_mean_, background_std_);
      if (background_std_ < 0.02) {
        background_std_ = 0.02;
      }
      is_calibrating_ = false;
      calibration_complete_ = true;
    }
    return std::nullopt;
  }

  if (!calibration_complete_ && accel_history_[active_axis_].size() < 20) {
    return std::nullopt;
  }

  {
    const auto &hist = accel_history_[active_axis_];
    compute_mean_std(hist, 0, hist.size(), background_mean_, background_std_);
    if (background_std_ < 0.02) {
      background_std_ = 0.02;
    }
  }

  const double current_filtered = filtered[active_axis_];
  const double current_raw = (active_axis_ == 0) ? acc_x : (active_axis_ == 1 ? acc_y : acc_z);
  const double deviation = current_filtered - background_mean_;

  double peak_threshold = PEAK_ENTER_FACTOR * background_std_;
  if (peak_threshold < MIN_PEAK_ABSOLUTE) {
    peak_threshold = MIN_PEAK_ABSOLUTE;
  }

  if (stroke_state_ == STATE_BACKGROUND) {
    if (deviation > peak_threshold) {
      stroke_state_ = STATE_PEAK_ZONE;
      phase_start_time_ = timestamp_ms;
      stroke_start_time_ = timestamp_ms;
      peak_max_value_ = deviation;
      peak_max_time_ = timestamp_ms;
      peak_max_raw_ = current_raw;
      peak_max_filtered_ = current_filtered;
    }
  } else if (stroke_state_ == STATE_PEAK_ZONE) {
    if (deviation > peak_max_value_) {
      peak_max_value_ = deviation;
      peak_max_time_ = timestamp_ms;
      peak_max_filtered_ = current_filtered;
      peak_max_raw_ = current_raw;
    }

    if (deviation < TROUGH_THRESHOLD) {
      stroke_state_ = STATE_TROUGH_ZONE;
      phase_start_time_ = timestamp_ms;
      trough_min_value_ = deviation;
      trough_min_time_ = timestamp_ms;
      trough_min_raw_ = current_raw;
      trough_min_filtered_ = current_filtered;
    }
  } else if (stroke_state_ == STATE_TROUGH_ZONE) {
    if (deviation < trough_min_value_) {
      trough_min_value_ = deviation;
      trough_min_time_ = timestamp_ms;
      trough_min_filtered_ = current_filtered;
      trough_min_raw_ = current_raw;
      recovery_counter_ = 0;
    }

    bool in_recovery_zone = false;
    if (trough_min_value_ < -0.1) {
      if (deviation > (trough_min_value_ * 0.5)) {
        in_recovery_zone = true;
      }
    } else {
      const double recovery_threshold = RECOVERY_FACTOR * background_std_;
      if (deviation > -recovery_threshold) {
        in_recovery_zone = true;
      }
    }

    if (in_recovery_zone) {
      recovery_counter_ += 1;
    } else {
      recovery_counter_ = 0;
    }

    if (deviation > 0.0) {
      recovery_counter_ = RECOVERY_SAMPLES;
    }

    if (recovery_counter_ >= RECOVERY_SAMPLES) {
      const double amplitude = peak_max_value_ - trough_min_value_;
      if (amplitude >= MIN_AMPLITUDE) {
        if (last_stroke_time_ > 0) {
          const double interval =
              static_cast<double>(peak_max_time_ - last_stroke_time_);
          if (interval > 0.0) {
            const double instant_rate = 60000.0 / interval;
            if (stroke_rate_ > 0.0) {
              stroke_rate_ = STROKE_RATE_EMA_ALPHA * instant_rate +
                             (1.0 - STROKE_RATE_EMA_ALPHA) * stroke_rate_;
            } else {
              stroke_rate_ = instant_rate;
            }
          }
        }

        stroke_count_ += 1;
        last_stroke_time_ = peak_max_time_;

        StrokeEvent event;
        event.stroke_start_time = stroke_start_time_ / 1000.0;
        event.stroke_end_time = timestamp_ms / 1000.0;
        event.time = timestamp_ms / 1000.0;
        event.peak_time = peak_max_time_ / 1000.0;
        event.peak_value = peak_max_value_;
        event.peak_raw = peak_max_raw_;
        event.peak_filtered = peak_max_filtered_;
        event.trough_time = trough_min_time_ / 1000.0;
        event.trough_value = trough_min_value_;
        event.trough_raw = trough_min_raw_;
        event.trough_filtered = trough_min_filtered_;
        event.amplitude = amplitude;
        event.rate = stroke_rate_;

        detected_strokes_.push_back(event);

        stroke_state_ = STATE_COOLDOWN;
        phase_start_time_ = timestamp_ms;
        recovery_counter_ = 0;
        return event;
      } else {
        stroke_state_ = STATE_BACKGROUND;
        recovery_counter_ = 0;
      }
    }
  } else if (stroke_state_ == STATE_COOLDOWN) {
    const uint64_t cooldown_elapsed = timestamp_ms - phase_start_time_;
    if (cooldown_elapsed >= static_cast<uint64_t>(COOLDOWN_DURATION)) {
      stroke_state_ = STATE_BACKGROUND;
    }
  }

  return std::nullopt;
}
