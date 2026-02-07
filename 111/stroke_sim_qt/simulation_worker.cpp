#include "simulation_worker.h"

#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QThread>
#include <algorithm>
#include <cmath>
#include <limits>

SimulationWorker::SimulationWorker(QObject *parent) : QObject(parent) {}

SimulationWorker::~SimulationWorker() { stop(); }

void SimulationWorker::set_detector(RealtimeStrokeDetector *detector) {
  detector_ = detector;
}

void SimulationWorker::load_data(const CsvData &data) {
  data_ = data;
  current_idx_ = 0;
  resetTimingStats();
  if (!data_.timestamps_ms.empty()) {
    base_time_ms_ = data_.timestamps_ms.front();
    // 每次加载新 IMU 数据时重置 GNSS 归一化状态
    nmea_normalized_ = false;
    gnss_offset_valid_ = false;
    // 建立 UTC -> sys 偏移（首条有效 UTC）
    for (const auto &entry : nmea_data_) {
      if (entry.raw_nmea_ms >= 0) {
        gnss_utc_offset_ms_ = entry.raw_sys_ms - entry.raw_nmea_ms;
        gnss_offset_valid_ = true;
        break;
      }
    }
    if (!nmea_data_.empty() && !nmea_normalized_) {
      for (auto &entry : nmea_data_) {
        qint64 aligned = entry.raw_sys_ms;
        if (gnss_offset_valid_ && entry.raw_nmea_ms >= 0) {
          aligned = entry.raw_nmea_ms + gnss_utc_offset_ms_;
        }
        entry.timestamp_ms = aligned - static_cast<qint64>(base_time_ms_);
      }
      nmea_normalized_ = true;
    }
  }
}

void SimulationWorker::set_speed(double speed) { speed_ = speed; }

void SimulationWorker::set_realtime(bool realtime) { realtime_ = realtime; }

void SimulationWorker::pause() { paused_ = true; }

void SimulationWorker::resume() { paused_ = false; }

void SimulationWorker::stop() { running_ = false; }

// Helper to parse JSONL line manually or using QJsonDocument if permissible
// (assuming minimal deps) Line format: {"ts":"...","ms":870067,"raw":"$GP..."}
void SimulationWorker::load_gnss_data(const QString &csv_path) {
  QFile file(csv_path);
  if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
    return;
  }

  nmea_data_.clear();
  current_nmea_idx_ = 0;
  gnss_offset_valid_ = false;
  gnss_utc_offset_ms_ = 0.0;
  nmea_normalized_ = false;
  prev_gnss_sys_ms_ = -1;
  gnss_dt_sum_ = gnss_dt_sq_sum_ = 0.0;
  gnss_dt_count_ = 0;
  gnss_dt_min_ = gnss_dt_max_ = 0.0;

  // Read line by line
  while (!file.atEnd()) {
    QString line = file.readLine();
    line = line.trimmed();
    if (line.isEmpty())
      continue;

    // Split by comma
    // Format: sys_ms,nmea_ms,nmea   (legacy: timestamp,nmea)
    // Example: 34201,3271350,$GNGGA,,,,,,0,,,,,,,,*78
    // 取第一列为 sys_ms，第二列为 nmea_ms（可为-1/空），第三列开始为原始 NMEA（带逗号）
    int firstComma = line.indexOf(',');
    if (firstComma <= 0)
      continue;
    int secondComma = line.indexOf(',', firstComma + 1);
    if (secondComma <= 0 || secondComma >= line.size() - 1)
      continue;
    QString tsPart = line.left(firstComma);
    QString nmeaMsPart = line.mid(firstComma + 1, secondComma - firstComma - 1);
    QString nmeaPart = line.mid(secondComma + 1);

    bool ok;
    qint64 ts = tsPart.toLongLong(&ok);
    if (!ok)
      continue; // First line might be header

    // Only process lines that start with $
    if (!nmeaPart.startsWith('$'))
      continue;

    qint64 raw_nmea_ms = -1;
    bool okUtc = false;
    raw_nmea_ms = nmeaMsPart.toLongLong(&okUtc);
    if (!okUtc)
      raw_nmea_ms = -1;

    NmeaEntry entry;
    entry.timestamp_ms = ts; // 临时，占位，稍后归一化
    entry.raw_sys_ms = ts;
    entry.raw_nmea_ms = raw_nmea_ms;
    entry.raw_nmea = nmeaPart;
    nmea_data_.push_back(entry);
  }

  // Sort by sys_ms just in case
  std::sort(nmea_data_.begin(), nmea_data_.end(),
            [](const NmeaEntry &a, const NmeaEntry &b) {
              return a.raw_sys_ms < b.raw_sys_ms;
            });

  nmea_normalized_ = false; // 需要等 IMU 基准后归一化

  // 如果 IMU 基准已在 load_data 中设定，立即归一化，避免 run() 时因 base_time_ms_>0 而跳过
  if (base_time_ms_ > 0.0 && !nmea_data_.empty()) {
    if (!gnss_offset_valid_) {
      for (const auto &entry : nmea_data_) {
        if (entry.raw_nmea_ms >= 0) {
          gnss_utc_offset_ms_ = entry.raw_sys_ms - entry.raw_nmea_ms;
          gnss_offset_valid_ = true;
          break;
        }
      }
    }
    for (auto &entry : nmea_data_) {
      qint64 aligned = entry.raw_sys_ms;
      if (gnss_offset_valid_ && entry.raw_nmea_ms >= 0) {
        aligned = entry.raw_nmea_ms + gnss_utc_offset_ms_;
      }
      entry.timestamp_ms = aligned - static_cast<qint64>(base_time_ms_);
    }
    nmea_normalized_ = true;
  }
}

// Haversine formula to calculate distance between two points in meters
double SimulationWorker::haversine(double lat1, double lon1, double lat2,
                                   double lon2) {
  constexpr double R = 6371000.0; // Earth radius in meters
  constexpr double TO_RAD = M_PI / 180.0;

  double dLat = (lat2 - lat1) * TO_RAD;
  double dLon = (lon2 - lon1) * TO_RAD;

  double a = std::sin(dLat / 2.0) * std::sin(dLat / 2.0) +
             std::cos(lat1 * TO_RAD) * std::cos(lat2 * TO_RAD) *
                 std::sin(dLon / 2.0) * std::sin(dLon / 2.0);

  double c = 2.0 * std::atan2(std::sqrt(a), std::sqrt(1.0 - a));

  return R * c;
}

void SimulationWorker::run() {
  if (!detector_) {
    emit finished();
    return;
  }
  if (data_.timestamps_ms.empty()) {
    emit finished();
    return;
  }

  if (base_time_ms_ <= 0.0) {
    base_time_ms_ = data_.timestamps_ms.front();
    if (!nmea_data_.empty() && !nmea_normalized_) {
      for (auto &entry : nmea_data_) {
        entry.timestamp_ms =
            entry.raw_sys_ms - static_cast<qint64>(base_time_ms_);
      }
      nmea_normalized_ = true;
    }
  }

  running_ = true;
  paused_ = false;
  resetTimingStats();

  QElapsedTimer timer;
  timer.start();
  // Assume generic alignment: First IMU timestamp aligns with simulation start
  sim_start_time_ = data_.timestamps_ms[current_idx_] - base_time_ms_;

  // Find initial index for GNSS based on start time (usually 0 if files are
  // synced)
  current_nmea_idx_ = 0;

  // Reset prev stroke location
  prev_stroke_lat_ = 0.0;
  prev_stroke_lon_ = 0.0;

  qint64 last_ui_update = 0;
  const qint64 ui_interval_ms = 33;

  if (!realtime_) {
    for (; current_idx_ < data_.timestamps_ms.size() && running_;
         ++current_idx_) {
      while (paused_ && running_) {
        QThread::msleep(5);
      }

      // IMU timing stats (offline)
      if (prev_imu_ts_raw_ >= 0) {
        double dt = static_cast<double>(data_.timestamps_ms[current_idx_] -
                                        prev_imu_ts_raw_);
        imu_dt_sum_ += dt;
        imu_dt_sq_sum_ += dt * dt;
        imu_dt_count_++;
        if (imu_dt_count_ == 1) {
          imu_dt_min_ = imu_dt_max_ = dt;
        } else {
          imu_dt_min_ = std::min(imu_dt_min_, dt);
          imu_dt_max_ = std::max(imu_dt_max_, dt);
        }
      }
      prev_imu_ts_raw_ = data_.timestamps_ms[current_idx_];

      uint64_t current_ts_ms = static_cast<uint64_t>(
          data_.timestamps_ms[current_idx_] - base_time_ms_);

      // IMU timing stats (use raw timestamps)
      if (prev_imu_ts_raw_ >= 0) {
        double dt = static_cast<double>(data_.timestamps_ms[current_idx_] -
                                        prev_imu_ts_raw_);
        imu_dt_sum_ += dt;
        imu_dt_sq_sum_ += dt * dt;
        imu_dt_count_++;
        if (imu_dt_count_ == 1) {
          imu_dt_min_ = imu_dt_max_ = dt;
        } else {
          imu_dt_min_ = std::min(imu_dt_min_, dt);
          imu_dt_max_ = std::max(imu_dt_max_, dt);
        }
      }
      prev_imu_ts_raw_ = data_.timestamps_ms[current_idx_];

      auto event = detector_->process_sample(
          current_ts_ms, data_.acc_x[current_idx_], data_.acc_y[current_idx_],
          data_.acc_z[current_idx_]);

      if (event) {
        // Calculate distance for this stroke
        unsigned long peak_time =
            static_cast<unsigned long>(event->peak_time * 1000.0);
        GNSSPoint point = gnss_processor_.getInterpolatedPosition(peak_time);

        double use_lat = 0.0;
        double use_lon = 0.0;
        if (point.valid) {
          use_lat = point.latitude;
          use_lon = point.longitude;
        } else {
          use_lat = gnss_processor_.getLatitude();
          use_lon = gnss_processor_.getLongitude();
        }

        if (use_lat != 0.0 || use_lon != 0.0) {
          event->latitude = use_lat;
          event->longitude = use_lon;

          if (prev_stroke_lat_ != 0.0 && prev_stroke_lon_ != 0.0) {
            event->distance_m = haversine(prev_stroke_lat_, prev_stroke_lon_,
                                          use_lat, use_lon);
          }

          prev_stroke_lat_ = use_lat;
          prev_stroke_lon_ = use_lon;
        }

        emit strokeDetected(*event);
      }

      // Process GNSS sync'd by timestamp
      double current_ts = data_.timestamps_ms[current_idx_] - base_time_ms_;
      while (current_nmea_idx_ < nmea_data_.size() &&
             nmea_data_[current_nmea_idx_].timestamp_ms <= current_ts) {
        const auto &gnss_entry = nmea_data_[current_nmea_idx_];
        qint64 current_gnss_ts = (gnss_entry.raw_nmea_ms >= 0)
                                     ? gnss_entry.raw_nmea_ms
                                     : gnss_entry.raw_sys_ms;
        qint64 &prev_ref =
            (gnss_entry.raw_nmea_ms >= 0) ? prev_gnss_nmea_ms_ : prev_gnss_sys_ms_;

        if (prev_ref >= 0) {
          double dt = static_cast<double>(current_gnss_ts - prev_ref);
          if (dt > 0.0) {
            gnss_dt_sum_ += dt;
            gnss_dt_sq_sum_ += dt * dt;
            gnss_dt_count_++;
            if (gnss_dt_count_ == 1) {
              gnss_dt_min_ = gnss_dt_max_ = dt;
            } else {
              gnss_dt_min_ = std::min(gnss_dt_min_, dt);
              gnss_dt_max_ = std::max(gnss_dt_max_, dt);
            }
            prev_ref = current_gnss_ts;
          }
        } else {
          prev_ref = current_gnss_ts;
        }

        gnss_processor_.processNMEA(gnss_entry.raw_nmea,
                                    gnss_entry.timestamp_ms);
        current_nmea_idx_++;
      }

      if (current_idx_ % 200 == 0) {
        emit simTimeUpdated(static_cast<qint64>(current_ts_ms));
        emit frameUpdated();
        emit gnssUpdated(gnss_processor_.getSpeed(),
                         gnss_processor_.getLatitude(),
                         gnss_processor_.getLongitude(),
                         gnss_processor_.getVisibleSatellites(),
                         gnss_processor_.getPaceString(),
                         gnss_processor_.getHDOP(),
                         gnss_processor_.getFixStatus(),
                         gnss_processor_.getDiffAge());
        emitTimeSync();
      }
    }
    emit frameUpdated();
    if (!data_.timestamps_ms.empty()) {
      emit simTimeUpdated(
          static_cast<qint64>(data_.timestamps_ms.back() - base_time_ms_));
    }
    running_ = false;
    emit finished();
    return;
  }

  while (running_ && current_idx_ < data_.timestamps_ms.size()) {
    while (paused_ && running_) {
      QThread::msleep(5);
    }
    const qint64 elapsed = timer.elapsed();
    const double target_sim_time = sim_start_time_ + (elapsed * speed_.load());

    // 1. Process IMU
    while (current_idx_ < data_.timestamps_ms.size() &&
           (data_.timestamps_ms[current_idx_] - base_time_ms_) <=
               target_sim_time) {

      uint64_t current_ts_ms = static_cast<uint64_t>(
          data_.timestamps_ms[current_idx_] - base_time_ms_);

      auto event = detector_->process_sample(
          current_ts_ms, data_.acc_x[current_idx_], data_.acc_y[current_idx_],
          data_.acc_z[current_idx_]);

      if (event) {
        // Calculate distance for this stroke
        unsigned long peak_time =
            static_cast<unsigned long>(event->peak_time * 1000.0);
        GNSSPoint point = gnss_processor_.getInterpolatedPosition(peak_time);

        double use_lat = 0.0;
        double use_lon = 0.0;
        if (point.valid) {
          use_lat = point.latitude;
          use_lon = point.longitude;
        } else {
          use_lat = gnss_processor_.getLatitude();
          use_lon = gnss_processor_.getLongitude();
        }

        if (use_lat != 0.0 || use_lon != 0.0) {
          event->latitude = use_lat;
          event->longitude = use_lon;

          if (prev_stroke_lat_ != 0.0 && prev_stroke_lon_ != 0.0) {
            event->distance_m = haversine(prev_stroke_lat_, prev_stroke_lon_,
                                          use_lat, use_lon);
          }

          prev_stroke_lat_ = use_lat;
          prev_stroke_lon_ = use_lon;
        }

        emit strokeDetected(*event);
      }

      ++current_idx_;
    }

    // 2. Process GNSS
    while (current_nmea_idx_ < nmea_data_.size() &&
           nmea_data_[current_nmea_idx_].timestamp_ms <= target_sim_time) {
      const auto &gnss_entry = nmea_data_[current_nmea_idx_];
      // Prefer nmea_ms for jitter stats; fallback to sys_ms if missing
      qint64 current_gnss_ts = (gnss_entry.raw_nmea_ms >= 0)
                                   ? gnss_entry.raw_nmea_ms
                                   : gnss_entry.raw_sys_ms;
      qint64 &prev_ref =
          (gnss_entry.raw_nmea_ms >= 0) ? prev_gnss_nmea_ms_ : prev_gnss_sys_ms_;

      if (prev_ref >= 0) {
        double dt = static_cast<double>(current_gnss_ts - prev_ref);
        if (dt > 0.0) { // 忽略重复时间戳导致的0/负间隔
          gnss_dt_sum_ += dt;
          gnss_dt_sq_sum_ += dt * dt;
          gnss_dt_count_++;
          if (gnss_dt_count_ == 1) {
            gnss_dt_min_ = gnss_dt_max_ = dt;
          } else {
            gnss_dt_min_ = std::min(gnss_dt_min_, dt);
            gnss_dt_max_ = std::max(gnss_dt_max_, dt);
          }
          prev_ref = current_gnss_ts;
        }
      } else {
        prev_ref = current_gnss_ts;
      }

      gnss_processor_.processNMEA(String(gnss_entry.raw_nmea),
                                  gnss_entry.timestamp_ms);
      current_nmea_idx_++;
    }

    if (elapsed - last_ui_update >= ui_interval_ms) {
      // Emit combined update
      emit simTimeUpdated(static_cast<qint64>(target_sim_time));
      emit frameUpdated();

      // Emit GNSS data (converted to double/int)
      emit gnssUpdated(
          gnss_processor_.getSpeed(), gnss_processor_.getLatitude(),
          gnss_processor_.getLongitude(),
          gnss_processor_.getVisibleSatellites(), // or solvingSatellites
          gnss_processor_.getPaceString(), gnss_processor_.getHDOP(),
          gnss_processor_.getFixStatus(), gnss_processor_.getDiffAge());
      emitTimeSync();

      last_ui_update = elapsed;
    }

    QThread::msleep(1);
  }

  running_ = false;
  emit finished();
}

void SimulationWorker::resetTimingStats() {
  prev_imu_ts_raw_ = -1;
  imu_dt_sum_ = imu_dt_sq_sum_ = 0.0;
  imu_dt_count_ = 0;
  imu_dt_min_ = imu_dt_max_ = 0.0;

  prev_gnss_sys_ms_ = -1;
  prev_gnss_nmea_ms_ = -1;
  gnss_dt_sum_ = gnss_dt_sq_sum_ = 0.0;
  gnss_dt_count_ = 0;
  gnss_dt_min_ = gnss_dt_max_ = 0.0;
}

void SimulationWorker::emitTimeSync() {
  computeImuStatsIfEmpty();

  auto calc_std = [](double sum, double sq_sum, int n) -> double {
    if (n <= 1)
      return 0.0;
    double mean = sum / n;
    double var = (sq_sum / n) - mean * mean;
    return (var > 0.0) ? std::sqrt(var) : 0.0;
  };

  double imu_mean = (imu_dt_count_ > 0) ? (imu_dt_sum_ / imu_dt_count_) : 0.0;
  double imu_std = calc_std(imu_dt_sum_, imu_dt_sq_sum_, imu_dt_count_);
  double gnss_mean =
      (gnss_dt_count_ > 0) ? (gnss_dt_sum_ / gnss_dt_count_) : 0.0;
  double gnss_std = calc_std(gnss_dt_sum_, gnss_dt_sq_sum_, gnss_dt_count_);

  double gnss_offset =
      gnss_offset_valid_ ? gnss_utc_offset_ms_ : std::numeric_limits<double>::quiet_NaN();

  emit timeSyncUpdated(base_time_ms_, gnss_offset, imu_mean, imu_dt_min_,
                       imu_dt_max_, imu_std, gnss_mean, gnss_dt_min_,
                       gnss_dt_max_, gnss_std);
}

void SimulationWorker::computeImuStatsIfEmpty() {
  if (imu_dt_count_ > 0 || data_.timestamps_ms.size() < 2) {
    return;
  }
  double prev = data_.timestamps_ms[0];
  for (size_t i = 1; i < data_.timestamps_ms.size(); ++i) {
    double dt = data_.timestamps_ms[i] - prev;
    prev = data_.timestamps_ms[i];
    imu_dt_sum_ += dt;
    imu_dt_sq_sum_ += dt * dt;
    imu_dt_count_++;
    if (imu_dt_count_ == 1) {
      imu_dt_min_ = imu_dt_max_ = dt;
    } else {
      imu_dt_min_ = std::min(imu_dt_min_, dt);
      imu_dt_max_ = std::max(imu_dt_max_, dt);
    }
  }
}
