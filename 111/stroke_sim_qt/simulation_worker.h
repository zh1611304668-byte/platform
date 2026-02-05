#pragma once

#include <QObject>
#include <QThread>
#include <atomic>

#include "GNSSProcessor.h"
#include "csv_loader.h"
#include "stroke_detector.h"

class SimulationWorker : public QObject {
  Q_OBJECT

public:
  explicit SimulationWorker(QObject *parent = nullptr);
  ~SimulationWorker() override;

  void set_detector(RealtimeStrokeDetector *detector);
  void load_data(const CsvData &data);
  void load_gnss_data(const QString &jsonl_path);
  void set_speed(double speed);
  void set_realtime(bool realtime);
  void pause();
  void resume();
  void stop();

signals:
  void frameUpdated();
  void simTimeUpdated(qint64 sim_time_ms);
  void gnssUpdated(double speed_mps, double lat, double lon, int sats,
                   QString pace, const QString &hdop, const QString &fix,
                   const QString &diff_age);
  void strokeDetected(const StrokeEvent &event);
  void finished();

public slots:
  void run();

private:
  RealtimeStrokeDetector *detector_ = nullptr;
  CsvData data_;
  std::atomic<bool> running_{false};
  std::atomic<bool> paused_{false};
  std::atomic<bool> realtime_{true};
  std::atomic<double> speed_{1.0};
  size_t current_idx_ = 0;

  double sim_start_time_ = 0.0;
  double base_time_ms_ = 0.0;          // IMU基准
  double gnss_utc_offset_ms_ = 0.0;    // sys_ms - nmea_ms
  bool gnss_offset_valid_ = false;
  bool nmea_normalized_ = false;

  // GNSS Integration
  GNSSProcessor gnss_processor_;
  struct NmeaEntry {
    qint64 timestamp_ms;   // relative to base_time_ms_
    qint64 raw_sys_ms;     // original sys_ms from CSV
    qint64 raw_nmea_ms;    // UTC-derived ms (-1 if none)
    QString raw_nmea;
  };
  std::vector<NmeaEntry> nmea_data_;
  size_t current_nmea_idx_ = 0;

  double prev_stroke_lat_ = 0.0;
  double prev_stroke_lon_ = 0.0;
  static double haversine(double lat1, double lon1, double lat2, double lon2);
};
