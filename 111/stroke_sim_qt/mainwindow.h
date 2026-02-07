#pragma once

#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QDoubleSpinBox>
#include <QGroupBox>
#include <QLabel>
#include <QMainWindow>
#include <QPushButton>
#include <QRadioButton>
#include <QSpinBox>
#include <QTabWidget>
#include <QtCharts/QChart>
#include <QtCharts/QLineSeries>
#include <QtCharts/QScatterSeries>
#include <QtCharts/QValueAxis>

#include "chart_view.h"
#include "csv_loader.h"
#include "lvgl_widget.h"
#include "simulation_worker.h"
#include "stroke_detector.h"
#include "training_mode.h"

class QQuickWidget;
class QGraphicsTextItem;

class MainWindow : public QMainWindow {
  Q_OBJECT

public:
  explicit MainWindow(QWidget *parent = nullptr);
  ~MainWindow() override;

private slots:
  void onLoadCsv();
  void onLoadGnss();
  void onTogglePlay();
  void onSpeedChanged(const QString &text);
  void onAxisChanged(int id);
  void onSimulationFinished();
  void onGnssUpdated(double speed_mps, double lat, double lon, int sats,
                     QString pace, const QString &hdop, const QString &fix,
                     const QString &diff_age);
  void onTimeSyncUpdated(double base_time_ms, double gnss_offset_ms,
                         double imu_mean_dt, double imu_min_dt,
                         double imu_max_dt, double imu_std_dt,
                         double gnss_mean_dt, double gnss_min_dt,
                         double gnss_max_dt, double gnss_std_dt);
  void onSimTimeUpdated(qint64 sim_time_ms);
  void onStrokeDetected(const StrokeEvent &event);
  void updateUi();

  // K1-K4 button slots
  void onK1Pressed();
  void onK2Pressed();
  void onK3Pressed();
  void onK4Pressed();
  void onK4Released();

  // Training Mode slots
  void onTrainingStarted();
  void onTrainingStopped();
  void onExportStrokes();

private:
  void buildUi();
  void setupChart();
  QWidget *createChartLegend();
  void initMap();
  void addMapPoint(double lat, double lon);
  void setMapInitialCenterFromGnss(const QString &path);
  void resetSimulation();
  void syncParamsToDetector();
  void syncParamsFromDetector();
  void updateStateIndicator();
  void updateChart();
  void addPeakLabel(const StrokeEvent &event);
  void updatePeakLabels(double time_offset);
  void clearPeakLabels();

  RealtimeStrokeDetector detector_;
  CsvData data_;

  SimulationWorker *worker_ = nullptr;
  QThread *worker_thread_ = nullptr;

  bool is_playing_ = false;
  bool auto_tracking_ = true;

  // Control widgets
  QPushButton *load_btn_ = nullptr;
  QPushButton *play_btn_ = nullptr;
  QPushButton *export_btn_ = nullptr;
  QComboBox *speed_combo_ = nullptr;
  QCheckBox *auto_track_cb_ = nullptr;
  QButtonGroup *axis_group_ = nullptr;

  // Status labels
  QLabel *state_label_ = nullptr;
  QLabel *time_label_ = nullptr;
  QLabel *count_label_ = nullptr;
  QLabel *rate_label_ = nullptr;
  QLabel *axis_label_ = nullptr;
  QLabel *deviation_label_ = nullptr;
  QLabel *threshold_label_ = nullptr;
  QLabel *peak_label_ = nullptr;
  QLabel *trough_label_ = nullptr;
  QLabel *duration_label_ = nullptr;
  QLabel *recovery_label_ = nullptr;
  QLabel *stroke_length_label_ = nullptr;
  QLabel *distance_label_ = nullptr;
  QLabel *time_base_label_ = nullptr;
  QLabel *gnss_offset_label_ = nullptr;
  QLabel *imu_jitter_label_ = nullptr;
  QLabel *gnss_jitter_label_ = nullptr;
  QQuickWidget *map_view_ = nullptr;

  // Parameter Tab Widget
  QTabWidget *param_tabs_ = nullptr;


  // Detection threshold parameters (Tab 1)
  QDoubleSpinBox *min_peak_sb_ = nullptr;
  QDoubleSpinBox *trough_threshold_sb_ = nullptr;
  QDoubleSpinBox *min_amp_sb_ = nullptr;
  QDoubleSpinBox *peak_factor_sb_ = nullptr;
  QDoubleSpinBox *recovery_factor_sb_ = nullptr;

  // Timing parameters (Tab 2)
  QSpinBox *min_peak_duration_sb_ = nullptr;
  QSpinBox *min_trough_duration_sb_ = nullptr;
  QSpinBox *cooldown_sb_ = nullptr;
  QSpinBox *min_interval_sb_ = nullptr;

  // Filter/Window parameters (Tab 3)
  QSpinBox *window_size_sb_ = nullptr;
  QSpinBox *calibration_sb_ = nullptr;

  // Chart widgets
  ChartView *chart_view_ = nullptr;
  QWidget *chart_container_ = nullptr;
  QChart *chart_ = nullptr;
  QLineSeries *signal_series_ = nullptr;
  QLineSeries *mean_line_ = nullptr;
  QLineSeries *peak_threshold_line_ = nullptr;
  QLineSeries *recovery_upper_line_ = nullptr;
  QLineSeries *recovery_lower_line_ = nullptr;
  QScatterSeries *peak_scatter_ = nullptr;
  QScatterSeries *trough_scatter_ = nullptr;
  QValueAxis *x_axis_ = nullptr;
  QValueAxis *y_axis_ = nullptr;

  // K1-K4 button widgets
  QPushButton *k1_btn_ = nullptr;
  QPushButton *k2_btn_ = nullptr;
  QPushButton *k3_btn_ = nullptr;
  QPushButton *k4_btn_ = nullptr;

  qint64 k4_press_time_ = 0; // Long press tracking

  qint64 last_sim_time_ms_ = 0;
  double total_distance_m_ = 0.0;
  double last_stroke_length_m_ = 0.0;
  bool has_prev_output_ = false;
  double prev_output_lat_ = 0.0;
  double prev_output_lon_ = 0.0;
  int training_stroke_count_ = 0;
  qint64 training_start_sim_ms_ = 0;
  QDateTime training_start_wall_time_;
  int ui_stroke_count_ = 0;

  double last_gnss_speed_mps_ = 0.0;
  double last_gnss_lat_ = 0.0;
  double last_gnss_lon_ = 0.0;
  QString last_gnss_pace_str_ = "00:00.0";
  bool gnss_has_fix_ = false;

  struct PeakLabel {
    double peak_time = 0.0;  // seconds
    double peak_value = 0.0; // filtered value
    QString text;
    QGraphicsTextItem *item = nullptr;
  };
  std::vector<PeakLabel> peak_labels_;
  double last_time_offset_ = 0.0;

  // LVGL display widget
  LvglWidget *lvgl_widget_ = nullptr;
  TrainingMode *training_mode_ = nullptr;

  struct StrokeExportRow {
    int stroke_number = 0;
    double stroke_length_m = 0.0;
    double total_distance_m = 0.0;
    double elapsed_seconds = 0.0;
    double speed_mps = 0.0;
    double stroke_rate_spm = 0.0;
    double latitude = 0.0;
    double longitude = 0.0;
    QString pace_str;
    QString timestamp;
  };
  std::vector<StrokeExportRow> training_strokes_;
};
