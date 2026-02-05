#pragma once

#include <array>
#include <cstdint>
#include <deque>
#include <optional>
#include <vector>

struct StrokeEvent {
  double stroke_start_time = 0.0;
  double stroke_end_time = 0.0;
  double time = 0.0;
  double peak_time = 0.0;
  double peak_value = 0.0;
  double peak_raw = 0.0;
  double peak_filtered = 0.0;
  double trough_time = 0.0;
  double trough_value = 0.0;
  double trough_raw = 0.0;
  double trough_filtered = 0.0;
  double amplitude = 0.0;
  double rate = 0.0;
  double distance_m = 0.0;
  double latitude = 0.0;
  double longitude = 0.0;
};

class RealtimeStrokeDetector {
public:
  static constexpr int STATE_BACKGROUND = 0;
  static constexpr int STATE_PEAK_ZONE = 1;
  static constexpr int STATE_TROUGH_ZONE = 2;
  static constexpr int STATE_COOLDOWN = 3;

  int WINDOW_SIZE = 125;
  double PEAK_ENTER_FACTOR = 1.1;
  double PEAK_EXIT_FACTOR = 0.4;
  double TROUGH_THRESHOLD = -0.01;
  double RECOVERY_FACTOR = 1.05;
  double MIN_PEAK_ABSOLUTE = 0.04;
  double MIN_AMPLITUDE = 0.03;
  int MIN_PEAK_DURATION = 15;
  int MIN_TROUGH_DURATION = 30;
  int STROKE_MIN_INTERVAL = 150;
  int COOLDOWN_DURATION = 100;
  int RECOVERY_SAMPLES = 5;
  int CALIBRATION_DURATION = 1600;
  int STROKE_TIMEOUT = 8000;
  double STROKE_RATE_EMA_ALPHA = 0.3;

  double cutoff_hz = 1.0;
  double sample_rate = 62.5;

  bool DEBUG = false;

  RealtimeStrokeDetector();

  void configure_filters(double cutoff, double sr);
  void set_active_axis(int axis);

  int stroke_count() const { return stroke_count_; }
  double stroke_rate() const { return stroke_rate_; }
  int stroke_state() const { return stroke_state_; }

  double background_mean() const { return background_mean_; }
  double background_std() const { return background_std_; }
  double peak_max_value() const { return peak_max_value_; }
  double trough_min_value() const { return trough_min_value_; }
  uint64_t phase_start_time() const { return phase_start_time_; }
  int recovery_counter() const { return recovery_counter_; }

  const std::vector<double> &raw_t() const { return raw_t_; }
  const std::vector<double> &raw_x() const { return raw_x_; }
  const std::vector<double> &raw_y() const { return raw_y_; }
  const std::vector<double> &raw_z() const { return raw_z_; }

  const std::vector<double> &filtered_t() const { return filtered_t_; }
  const std::vector<double> &filtered_x() const { return filtered_x_; }
  const std::vector<double> &filtered_y() const { return filtered_y_; }
  const std::vector<double> &filtered_z() const { return filtered_z_; }
  const std::vector<StrokeEvent> &detected_strokes() const {
    return detected_strokes_;
  }

  std::optional<StrokeEvent> process_sample(uint64_t timestamp_ms, double acc_x,
                                            double acc_y, double acc_z);

  void reset();

private:
  struct ButterworthFilter {
    void set_params(double cutoff_hz, double sample_rate);
    double filter(double input);

    double b0 = 0.0;
    double b1 = 0.0;
    double b2 = 0.0;
    double a1 = 0.0;
    double a2 = 0.0;
    double x1 = 0.0;
    double x2 = 0.0;
    double y1 = 0.0;
    double y2 = 0.0;
  };

  struct ExponentialMovingAverage {
    ExponentialMovingAverage() = default;
    explicit ExponentialMovingAverage(double a) : alpha(a) {}
    double filter(double input);
    void reset();
    double alpha = 0.1;
    bool has_value = false;
    double value = 0.0;
  };

  size_t calibration_samples() const;

  std::array<ButterworthFilter, 3> filters_{};
  std::array<ExponentialMovingAverage, 3> ema_filters_{};
  std::array<std::deque<double>, 3> accel_history_{};

  int active_axis_ = 2;
  int stroke_state_ = STATE_BACKGROUND;
  int stroke_count_ = 0;
  double stroke_rate_ = 0.0;

  uint64_t phase_start_time_ = 0;
  uint64_t stroke_start_time_ = 0;
  double peak_max_value_ = 0.0;
  uint64_t peak_max_time_ = 0;
  double peak_max_raw_ = 0.0;
  double peak_max_filtered_ = 0.0;
  double trough_min_value_ = 0.0;
  uint64_t trough_min_time_ = 0;
  double trough_min_raw_ = 0.0;
  double trough_min_filtered_ = 0.0;

  int recovery_counter_ = 0;
  double background_mean_ = 0.0;
  double background_std_ = 0.1;
  uint64_t last_stroke_time_ = 0;

  bool is_calibrating_ = true;
  bool calibration_complete_ = false;

  std::vector<double> filtered_t_;
  std::vector<double> filtered_x_;
  std::vector<double> filtered_y_;
  std::vector<double> filtered_z_;
  std::vector<double> raw_t_;
  std::vector<double> raw_x_;
  std::vector<double> raw_y_;
  std::vector<double> raw_z_;
  std::vector<StrokeEvent> detected_strokes_;
};
