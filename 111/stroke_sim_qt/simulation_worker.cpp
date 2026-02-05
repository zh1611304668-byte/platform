#include "simulation_worker.h"

#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QThread>
#include <algorithm>

SimulationWorker::SimulationWorker(QObject *parent) : QObject(parent) {}

SimulationWorker::~SimulationWorker() { stop(); }

void SimulationWorker::set_detector(RealtimeStrokeDetector *detector) {
  detector_ = detector;
}

void SimulationWorker::load_data(const CsvData &data) {
  data_ = data;
  current_idx_ = 0;
  if (!data_.timestamps_ms.empty()) {
    base_time_ms_ = data_.timestamps_ms.front();
    if (!nmea_data_.empty() && !nmea_normalized_) {
      for (auto &entry : nmea_data_) {
        entry.timestamp_ms -= static_cast<qint64>(base_time_ms_);
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

  // Read line by line
  while (!file.atEnd()) {
    QString line = file.readLine();
    line = line.trimmed();
    if (line.isEmpty())
      continue;

    // Split by comma
    // Format: timestamp,nmea
    // Example: 34201,$GNGGA,,,,,,0,,,,,,,,*78

    // Find first comma
    int commaIdx = line.indexOf(',');
    if (commaIdx == -1)
      continue;

    QString tsPart = line.left(commaIdx);
    QString nmeaPart = line.mid(commaIdx + 1);

    bool ok;
    qint64 ts = tsPart.toLongLong(&ok);
    if (!ok)
      continue; // First line might be header "timestamp,nmea"

    // Only process lines that start with $
    if (!nmeaPart.startsWith('$'))
      continue;

    qint64 adjusted_ts = ts;
    if (base_time_ms_ > 0.0) {
      adjusted_ts -= static_cast<qint64>(base_time_ms_);
    }
    nmea_data_.push_back({adjusted_ts, nmeaPart});
  }

  // Sort by timestamp just in case
  std::sort(nmea_data_.begin(), nmea_data_.end(),
            [](const NmeaEntry &a, const NmeaEntry &b) {
              return a.timestamp_ms < b.timestamp_ms;
            });

  nmea_normalized_ = (base_time_ms_ > 0.0);
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
        entry.timestamp_ms -= static_cast<qint64>(base_time_ms_);
      }
      nmea_normalized_ = true;
    }
  }

  running_ = true;
  paused_ = false;

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

      // Process GNSS sync'd by timestamp
      double current_ts = data_.timestamps_ms[current_idx_] - base_time_ms_;
      while (current_nmea_idx_ < nmea_data_.size() &&
             nmea_data_[current_nmea_idx_].timestamp_ms <= current_ts) {
        gnss_processor_.processNMEA(nmea_data_[current_nmea_idx_].raw_nmea,
                                    nmea_data_[current_nmea_idx_].timestamp_ms);
        current_nmea_idx_++;
      }

      if (current_idx_ % 200 == 0) {
        emit simTimeUpdated(static_cast<qint64>(current_ts_ms));
        emit frameUpdated();
        emit gnssUpdated(gnss_processor_.getSpeed(),
                         gnss_processor_.getLatitude(),
                         gnss_processor_.getLongitude(),
                         gnss_processor_.getVisibleSatellites(),
                         gnss_processor_.getPaceString());
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
      gnss_processor_.processNMEA(
          String(nmea_data_[current_nmea_idx_].raw_nmea),
          nmea_data_[current_nmea_idx_].timestamp_ms);
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
          gnss_processor_.getPaceString());

      last_ui_update = elapsed;
    }

    QThread::msleep(1);
  }

  running_ = false;
  emit finished();
}
