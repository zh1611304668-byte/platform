#include "mainwindow.h"

#include <QApplication>
#include <QButtonGroup>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QMessageBox>
#include <QScrollArea>
#include <QSizePolicy>
#include <QTextStream>
#include <QVBoxLayout>
#include <QQuickItem>
#include <QQuickWidget>
#include <QVariant>
#include <QQmlError>
#include <QStackedLayout>
#include <QFrame>
#include <QFile>
#include <QTextStream>
#include <QGraphicsTextItem>
#include <QFont>

#include <algorithm>
#include <cmath>

namespace {
constexpr double kEarthRadiusMeters = 6371000.0;
constexpr double kDegToRad = M_PI / 180.0;

double haversineMeters(double lat1, double lon1, double lat2, double lon2) {
  const double dLat = (lat2 - lat1) * kDegToRad;
  const double dLon = (lon2 - lon1) * kDegToRad;
  const double a = std::sin(dLat / 2.0) * std::sin(dLat / 2.0) +
                   std::cos(lat1 * kDegToRad) * std::cos(lat2 * kDegToRad) *
                       std::sin(dLon / 2.0) * std::sin(dLon / 2.0);
  const double c = 2.0 * std::atan2(std::sqrt(a), std::sqrt(1.0 - a));
  return kEarthRadiusMeters * c;
}

double roundToDecimals(double value, int decimals) {
  const double scale = std::pow(10.0, decimals);
  return std::round(value * scale) / scale;
}

QString formatSplitSeconds(double seconds) {
  if (seconds <= 0.0) {
    return "--";
  }

  const int total_seconds = static_cast<int>(seconds);
  int minutes = total_seconds / 60;
  int secs = total_seconds % 60;
  int tenths = static_cast<int>(
      (seconds - static_cast<double>(total_seconds)) * 10.0 + 0.5);

  if (tenths >= 10) {
    tenths = 0;
    secs += 1;
    if (secs >= 60) {
      secs = 0;
      minutes += 1;
    }
  }

  return QString("%1:%2.%3")
      .arg(minutes, 2, 10, QChar('0'))
      .arg(secs, 2, 10, QChar('0'))
      .arg(tenths, 1, 10, QChar('0'));
}
} // namespace

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent) {
  buildUi();

  training_mode_ = new TrainingMode(this);
  connect(training_mode_, &TrainingMode::trainingStarted, this,
          &MainWindow::onTrainingStarted);
  connect(training_mode_, &TrainingMode::trainingStopped, this,
          &MainWindow::onTrainingStopped);

  worker_thread_ = new QThread(this);
  worker_ = new SimulationWorker();
  worker_->set_detector(&detector_);
  worker_->moveToThread(worker_thread_);

  connect(worker_thread_, &QThread::started, worker_, &SimulationWorker::run);
  connect(worker_, &SimulationWorker::frameUpdated, this,
          &MainWindow::updateUi);
  connect(worker_, &SimulationWorker::gnssUpdated, this,
          &MainWindow::onGnssUpdated);
  connect(worker_, &SimulationWorker::timeSyncUpdated, this,
          &MainWindow::onTimeSyncUpdated);
  connect(worker_, &SimulationWorker::simTimeUpdated, this,
          &MainWindow::onSimTimeUpdated);
  connect(worker_, &SimulationWorker::strokeDetected, this,
          &MainWindow::onStrokeDetected);
  connect(worker_, &SimulationWorker::finished, this,
          &MainWindow::onSimulationFinished);
  connect(worker_, &SimulationWorker::finished, worker_thread_, &QThread::quit);

  resetSimulation();
}

MainWindow::~MainWindow() {
  if (worker_) {
    worker_->stop();
  }
  if (worker_thread_) {
    worker_thread_->quit();
    worker_thread_->wait();
  }
  delete worker_;
}

void MainWindow::buildUi() {
  setWindowTitle("划桨模拟器 v2.0");
  resize(1500, 900);

  auto *central = new QWidget(this);
  setCentralWidget(central);
  auto *main_layout = new QHBoxLayout(central);

  auto *sidebar = new QWidget();
  auto *sidebar_layout = new QVBoxLayout(sidebar);
  sidebar_layout->setAlignment(Qt::AlignTop);
  sidebar_layout->setSpacing(8);

  auto *scroll = new QScrollArea();
  scroll->setWidget(sidebar);
  scroll->setWidgetResizable(true);
  scroll->setFixedWidth(400);

  // ========== Control Group ==========
  auto *control_group = new QGroupBox("控制");
  auto *control_layout = new QVBoxLayout();

  auto *btn_row = new QHBoxLayout();
  load_btn_ = new QPushButton("加载CSV");
  load_btn_->setStyleSheet("font-size: 12px; padding: 6px;");
  connect(load_btn_, &QPushButton::clicked, this, &MainWindow::onLoadCsv);
  btn_row->addWidget(load_btn_);

  auto *load_gnss_btn = new QPushButton("加载GNSS");
  load_gnss_btn->setStyleSheet("font-size: 12px; padding: 6px;");
  connect(load_gnss_btn, &QPushButton::clicked, this, &MainWindow::onLoadGnss);
  btn_row->addWidget(load_gnss_btn);

  auto *reset_btn = new QPushButton("重置");
  reset_btn->setStyleSheet("font-size: 12px; padding: 6px;");
  connect(reset_btn, &QPushButton::clicked, this, &MainWindow::resetSimulation);
  btn_row->addWidget(reset_btn);
  control_layout->addLayout(btn_row);

  export_btn_ = new QPushButton("导出划桨");
  export_btn_->setEnabled(false);
  export_btn_->setStyleSheet("font-size: 12px; padding: 6px;");
  connect(export_btn_, &QPushButton::clicked, this,
          &MainWindow::onExportStrokes);
  control_layout->addWidget(export_btn_);

  play_btn_ = new QPushButton("开始");
  play_btn_->setEnabled(false);
  play_btn_->setStyleSheet("font-weight: bold; font-size: 14px; padding: 8px;");
  connect(play_btn_, &QPushButton::clicked, this, &MainWindow::onTogglePlay);
  control_layout->addWidget(play_btn_);

  auto *speed_row = new QHBoxLayout();
  speed_row->addWidget(new QLabel("速度:"));
  speed_combo_ = new QComboBox();
  speed_combo_->addItems(
      {"0.25x", "0.5x", "1.0x", "2.0x", "5.0x", "10x", "20x", "50x", "100x"});
  speed_combo_->setCurrentText("1.0x");
  connect(speed_combo_, &QComboBox::currentTextChanged, this,
          &MainWindow::onSpeedChanged);
  speed_row->addWidget(speed_combo_);
  control_layout->addLayout(speed_row);

  auto_track_cb_ = new QCheckBox("自动跟踪");
  auto_track_cb_->setChecked(true);
  connect(auto_track_cb_, &QCheckBox::toggled, this,
          [this](bool checked) { auto_tracking_ = checked; });
  control_layout->addWidget(auto_track_cb_);

  control_group->setLayout(control_layout);
  sidebar_layout->addWidget(control_group);

  // ========== Parameter Tabs ==========
  auto *params_group = new QGroupBox("算法参数");
  auto *params_main_layout = new QVBoxLayout();

  param_tabs_ = new QTabWidget();
  param_tabs_->setStyleSheet(
      "QTabWidget::pane { border: 1px solid #ccc; padding: 5px; }");

  // --- Tab 1: Detection Thresholds ---
  auto *threshold_tab = new QWidget();
  auto *threshold_layout = new QFormLayout(threshold_tab);
  threshold_layout->setSpacing(8);

  min_peak_sb_ = new QDoubleSpinBox();
  min_peak_sb_->setRange(0.01, 2.0);
  min_peak_sb_->setDecimals(3);
  min_peak_sb_->setSuffix(" g");
  min_peak_sb_->setValue(detector_.MIN_PEAK_ABSOLUTE);
  threshold_layout->addRow("最小峰值高度:", min_peak_sb_);

  trough_threshold_sb_ = new QDoubleSpinBox();
  trough_threshold_sb_->setRange(-2.0, 0.0);
  trough_threshold_sb_->setDecimals(3);
  trough_threshold_sb_->setSuffix(" g");
  trough_threshold_sb_->setValue(detector_.TROUGH_THRESHOLD);
  threshold_layout->addRow("谷值阈值:", trough_threshold_sb_);

  min_amp_sb_ = new QDoubleSpinBox();
  min_amp_sb_->setRange(0.01, 2.0);
  min_amp_sb_->setDecimals(3);
  min_amp_sb_->setSuffix(" g");
  min_amp_sb_->setValue(detector_.MIN_AMPLITUDE);
  threshold_layout->addRow("最小幅度:", min_amp_sb_);

  peak_factor_sb_ = new QDoubleSpinBox();
  peak_factor_sb_->setRange(0.1, 10.0);
  peak_factor_sb_->setDecimals(2);
  peak_factor_sb_->setSuffix(" 倍");
  peak_factor_sb_->setValue(detector_.PEAK_ENTER_FACTOR);
  threshold_layout->addRow("峰值进入系数:", peak_factor_sb_);

  recovery_factor_sb_ = new QDoubleSpinBox();
  recovery_factor_sb_->setRange(0.1, 10.0);
  recovery_factor_sb_->setDecimals(2);
  recovery_factor_sb_->setSuffix(" 倍");
  recovery_factor_sb_->setValue(detector_.RECOVERY_FACTOR);
  threshold_layout->addRow("恢复系数:", recovery_factor_sb_);

  param_tabs_->addTab(threshold_tab, "阈值");

  // --- Tab 2: Timing Parameters ---
  auto *timing_tab = new QWidget();
  auto *timing_layout = new QFormLayout(timing_tab);
  timing_layout->setSpacing(8);

  min_peak_duration_sb_ = new QSpinBox();
  min_peak_duration_sb_->setRange(0, 500);
  min_peak_duration_sb_->setSuffix(" ms");
  min_peak_duration_sb_->setValue(detector_.MIN_PEAK_DURATION);
  timing_layout->addRow("最小峰值时长:", min_peak_duration_sb_);

  min_trough_duration_sb_ = new QSpinBox();
  min_trough_duration_sb_->setRange(0, 500);
  min_trough_duration_sb_->setSuffix(" ms");
  min_trough_duration_sb_->setValue(detector_.MIN_TROUGH_DURATION);
  timing_layout->addRow("最小谷值时长:", min_trough_duration_sb_);

  cooldown_sb_ = new QSpinBox();
  cooldown_sb_->setRange(0, 2000);
  cooldown_sb_->setSuffix(" ms");
  cooldown_sb_->setValue(detector_.COOLDOWN_DURATION);
  timing_layout->addRow("冷却时长:", cooldown_sb_);

  min_interval_sb_ = new QSpinBox();
  min_interval_sb_->setRange(100, 3000);
  min_interval_sb_->setSuffix(" ms");
  min_interval_sb_->setValue(detector_.STROKE_MIN_INTERVAL);
  timing_layout->addRow("最小划桨间隔:", min_interval_sb_);

  param_tabs_->addTab(timing_tab, "时间");

  // --- Tab 3: Filter/Window Parameters ---
  auto *filter_tab = new QWidget();
  auto *filter_layout = new QFormLayout(filter_tab);
  filter_layout->setSpacing(8);

  window_size_sb_ = new QSpinBox();
  window_size_sb_->setRange(10, 1000);
  window_size_sb_->setSuffix(" 点");
  window_size_sb_->setValue(detector_.WINDOW_SIZE);
  filter_layout->addRow("窗口大小:", window_size_sb_);

  calibration_sb_ = new QSpinBox();
  calibration_sb_->setRange(0, 2000);
  calibration_sb_->setSuffix(" ms");
  calibration_sb_->setValue(detector_.CALIBRATION_DURATION);
  filter_layout->addRow("校准时长:", calibration_sb_);

  param_tabs_->addTab(filter_tab, "滤波");

  // --- Tab 4: Axis Selection ---
  auto *axis_tab = new QWidget();
  auto *axis_tab_layout = new QVBoxLayout(axis_tab);
  auto *axis_group_box = new QGroupBox("当前轴");
  auto *axis_layout = new QHBoxLayout();
  axis_group_ = new QButtonGroup(this);
  const QStringList axis_names = {"X", "Y", "Z"};
  for (int i = 0; i < 3; ++i) {
    auto *rb = new QRadioButton(axis_names[i]);
    axis_group_->addButton(rb, i);
    axis_layout->addWidget(rb);
    if (i == 2) {
      rb->setChecked(true);
    }
  }
  connect(axis_group_, &QButtonGroup::idClicked, this,
          &MainWindow::onAxisChanged);
  axis_group_box->setLayout(axis_layout);
  axis_tab_layout->addWidget(axis_group_box);
  axis_tab_layout->addStretch();
  param_tabs_->addTab(axis_tab, "轴");

  params_main_layout->addWidget(param_tabs_);

  // Connect all parameter changes
  auto on_param_changed = [this]() { syncParamsToDetector(); };
  connect(min_peak_sb_, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
          this, on_param_changed);
  connect(trough_threshold_sb_,
          QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
          on_param_changed);
  connect(min_amp_sb_, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
          this, on_param_changed);
  connect(peak_factor_sb_, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
          this, on_param_changed);
  connect(recovery_factor_sb_,
          QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
          on_param_changed);
  connect(min_peak_duration_sb_, QOverload<int>::of(&QSpinBox::valueChanged),
          this, on_param_changed);
  connect(min_trough_duration_sb_, QOverload<int>::of(&QSpinBox::valueChanged),
          this, on_param_changed);
  connect(cooldown_sb_, QOverload<int>::of(&QSpinBox::valueChanged), this,
          on_param_changed);
  connect(min_interval_sb_, QOverload<int>::of(&QSpinBox::valueChanged), this,
          on_param_changed);
  connect(window_size_sb_, QOverload<int>::of(&QSpinBox::valueChanged), this,
          on_param_changed);
  connect(calibration_sb_, QOverload<int>::of(&QSpinBox::valueChanged), this,
          on_param_changed);

  params_group->setLayout(params_main_layout);
  sidebar_layout->addWidget(params_group);

  // ========== Status Display ==========
  auto *stats_group = new QGroupBox("状态");
  auto *stats_layout = new QVBoxLayout();
  state_label_ = new QLabel("背景");
  state_label_->setAlignment(Qt::AlignCenter);
  state_label_->setStyleSheet("background: #9E9E9E; color: white; font-weight: "
                              "bold; padding: 8px; border-radius: 4px;");
  stats_layout->addWidget(state_label_);

  auto *grid = new QGridLayout();
  time_label_ = new QLabel("时间: 0.0s");
  count_label_ = new QLabel("划桨: 0");
  rate_label_ = new QLabel("频率: 0 SPM");
  axis_label_ = new QLabel("轴: Z");
  grid->addWidget(time_label_, 0, 0);
  grid->addWidget(count_label_, 0, 1);
  grid->addWidget(rate_label_, 1, 0);
  grid->addWidget(axis_label_, 1, 1);
  stats_layout->addLayout(grid);

  auto *stroke_stats_row = new QHBoxLayout();
  stroke_length_label_ = new QLabel("划距: -- m");
  distance_label_ = new QLabel("累计距离: -- m");
  stroke_length_label_->setStyleSheet("font-size: 12px; font-weight: bold;");
  distance_label_->setStyleSheet("font-size: 12px; font-weight: bold;");
  stroke_stats_row->addWidget(stroke_length_label_);
  stroke_stats_row->addWidget(distance_label_);
  stats_layout->addLayout(stroke_stats_row);

  auto *debug_group = new QGroupBox("调试");
  auto *debug_grid = new QGridLayout();
  deviation_label_ = new QLabel("偏差: 0.000g");
  threshold_label_ = new QLabel("阈值: 0.000g");
  peak_label_ = new QLabel("峰值: --");
  trough_label_ = new QLabel("谷值: --");
  duration_label_ = new QLabel("持续: 0ms");
  recovery_label_ = new QLabel("恢复: 0");
  debug_grid->addWidget(deviation_label_, 0, 0);
  debug_grid->addWidget(threshold_label_, 0, 1);
  debug_grid->addWidget(peak_label_, 1, 0);
  debug_grid->addWidget(trough_label_, 1, 1);
  debug_grid->addWidget(duration_label_, 2, 0);
  debug_grid->addWidget(recovery_label_, 2, 1);
  debug_group->setLayout(debug_grid);
  stats_layout->addWidget(debug_group);

  // Time sync group
  auto *time_group = new QGroupBox("时间对齐");
  auto *time_form = new QFormLayout();
  time_base_label_ = new QLabel("--");
  gnss_offset_label_ = new QLabel("--");
  imu_jitter_label_ = new QLabel("--");
  gnss_jitter_label_ = new QLabel("--");
  time_form->addRow("IMU 基准(ms):", time_base_label_);
  time_form->addRow("GNSS 偏移(sys-nmea):", gnss_offset_label_);
  time_form->addRow("IMU 周期/jitter:", imu_jitter_label_);
  time_form->addRow("GNSS 周期/jitter:", gnss_jitter_label_);
  time_group->setLayout(time_form);
  stats_layout->addWidget(time_group);

  stats_group->setLayout(stats_layout);
  sidebar_layout->addWidget(stats_group);

  // ========== K1-K4 Button Group ==========
  auto *key_group = new QGroupBox("硬件按键 (K1-K4)");
  auto *key_layout = new QGridLayout();
  key_layout->setSpacing(8);

  k1_btn_ = new QPushButton("K1\n屏幕");
  k2_btn_ = new QPushButton("K2\n上");
  k3_btn_ = new QPushButton("K3\n下");
  k4_btn_ = new QPushButton("K4\n确认");

  QString btn_style = "QPushButton { "
                      "min-width: 70px; "
                      "min-height: 50px; "
                      "font-size: 10px; "
                      "font-weight: bold; "
                      "}"
                      "QPushButton:pressed { "
                      "background-color: #4CAF50; "
                      "color: white; "
                      "}";
  k1_btn_->setStyleSheet(btn_style);
  k2_btn_->setStyleSheet(btn_style);
  k3_btn_->setStyleSheet(btn_style);
  k4_btn_->setStyleSheet(btn_style);

  connect(k1_btn_, &QPushButton::clicked, this, &MainWindow::onK1Pressed);
  connect(k2_btn_, &QPushButton::clicked, this, &MainWindow::onK2Pressed);
  connect(k3_btn_, &QPushButton::clicked, this, &MainWindow::onK3Pressed);
  connect(k4_btn_, &QPushButton::pressed, this, &MainWindow::onK4Pressed);
  connect(k4_btn_, &QPushButton::released, this, &MainWindow::onK4Released);

  key_layout->addWidget(k1_btn_, 0, 0);
  key_layout->addWidget(k2_btn_, 0, 1);
  key_layout->addWidget(k3_btn_, 1, 0);
  key_layout->addWidget(k4_btn_, 1, 1);
  key_group->setLayout(key_layout);
  sidebar_layout->addWidget(key_group);

  main_layout->addWidget(scroll);

  // ========== Right Side: Top (LVGL + Metrics) / Bottom (Chart) ==========
  auto *right_widget = new QWidget();
  auto *right_layout = new QVBoxLayout(right_widget);

  // Top: LVGL (left) + Metrics/Map (right), together same width as chart
  auto *top_row = new QHBoxLayout();

  lvgl_widget_ = new LvglWidget(this);
  lvgl_widget_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
  top_row->addWidget(lvgl_widget_, 1);

  auto *right_col = new QWidget();
  auto *right_col_layout = new QVBoxLayout(right_col);
  right_col_layout->setSpacing(8);

  auto *map_group = new QGroupBox("轨迹地图");
  auto *map_layout = new QVBoxLayout();
  initMap();
  if (map_view_) {
    map_view_->setMinimumHeight(260);
    map_view_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    map_layout->addWidget(map_view_, 1);
  }
  map_group->setLayout(map_layout);
  map_group->setFixedHeight(lvgl_widget_->height());

  right_col_layout->addWidget(map_group, 1);
  right_col->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);

  top_row->addWidget(right_col, 1);
  right_layout->addLayout(top_row, 0);

  // Bottom: Chart Area
  setupChart();
  chart_view_->setMinimumHeight(300); // Ensure chart is visible

  chart_container_ = new QWidget();
  auto *chart_stack = new QStackedLayout(chart_container_);
  chart_stack->setStackingMode(QStackedLayout::StackAll);
  chart_stack->addWidget(chart_view_);
  chart_stack->addWidget(createChartLegend());

  right_layout->addWidget(chart_container_, 1); // Stretch to fill remaining space

  main_layout->addWidget(right_widget, 1);
}

void MainWindow::setupChart() {
  signal_series_ = new QLineSeries();
  signal_series_->setName("信号");
  signal_series_->setPen(QPen(QColor("#1976D2"), 2));

  mean_line_ = new QLineSeries();
  mean_line_->setName("均值");
  peak_threshold_line_ = new QLineSeries();
  peak_threshold_line_->setName("峰值阈值");
  recovery_upper_line_ = new QLineSeries();
  recovery_upper_line_->setName("恢复上限");
  recovery_lower_line_ = new QLineSeries();
  recovery_lower_line_->setName("恢复下限");

  mean_line_->setPen(QPen(QColor("#90A4AE"), 1, Qt::DashLine));
  peak_threshold_line_->setPen(QPen(QColor("#E53935"), 1, Qt::DashLine));
  recovery_upper_line_->setPen(QPen(QColor("#43A047"), 1, Qt::DashLine));
  recovery_lower_line_->setPen(QPen(QColor("#43A047"), 1, Qt::DashLine));

  peak_scatter_ = new QScatterSeries();
  peak_scatter_->setName("峰值点");
  peak_scatter_->setMarkerShape(QScatterSeries::MarkerShapeTriangle);
  peak_scatter_->setMarkerSize(12.0);
  peak_scatter_->setColor(QColor(244, 67, 54));

  trough_scatter_ = new QScatterSeries();
  trough_scatter_->setName("谷值点");
  trough_scatter_->setMarkerShape(QScatterSeries::MarkerShapeRectangle);
  trough_scatter_->setMarkerSize(12.0);
  trough_scatter_->setColor(QColor(76, 175, 80));

  chart_ = new QChart();
  chart_->addSeries(signal_series_);
  chart_->addSeries(mean_line_);
  chart_->addSeries(peak_threshold_line_);
  chart_->addSeries(recovery_upper_line_);
  chart_->addSeries(recovery_lower_line_);
  chart_->addSeries(peak_scatter_);
  chart_->addSeries(trough_scatter_);
  chart_->legend()->setVisible(false);

  x_axis_ = new QValueAxis();
  x_axis_->setTitleText("时间 (s)");
  y_axis_ = new QValueAxis();
  y_axis_->setTitleText("加速度 (g)");
  y_axis_->setRange(-1.0, 1.0);

  chart_->addAxis(x_axis_, Qt::AlignBottom);
  chart_->addAxis(y_axis_, Qt::AlignLeft);

  for (auto *series : chart_->series()) {
    series->attachAxis(x_axis_);
    series->attachAxis(y_axis_);
  }

  chart_view_ = new ChartView(chart_);
  chart_view_->setRenderHint(QPainter::Antialiasing, true);
  chart_view_->setXAxis(x_axis_);
  chart_view_->setYAxis(y_axis_);

  // Keep peak labels aligned when axes change (pan/zoom)
  connect(x_axis_, &QValueAxis::rangeChanged, this,
          [this](qreal, qreal) { updatePeakLabels(last_time_offset_); });
  connect(y_axis_, &QValueAxis::rangeChanged, this,
          [this](qreal, qreal) { updatePeakLabels(last_time_offset_); });

  // Disable auto-tracking when user drags the chart
  connect(chart_view_, &ChartView::userDragged, this, [this]() {
    if (auto_track_cb_->isChecked()) {
      auto_track_cb_->setChecked(false);
    }
  });
}

void MainWindow::initMap() {
  if (map_view_) {
    return;
  }
  map_view_ = new QQuickWidget(this);
  map_view_->setResizeMode(QQuickWidget::SizeRootObjectToView);
  map_view_->setClearColor(QColor(20, 20, 20));
  connect(map_view_, &QQuickWidget::statusChanged, this,
          [this](QQuickWidget::Status status) {
            if (status != QQuickWidget::Error) {
              return;
            }
            QStringList errors;
            const auto list = map_view_->errors();
            for (const auto &err : list) {
              errors << err.toString();
            }
            qWarning() << "Map QML load errors:" << errors.join('\n');
          });
  map_view_->setSource(QUrl("qrc:/qml/MapView.qml"));
}

QWidget *MainWindow::createChartLegend() {
  auto *legend = new QFrame();
  legend->setStyleSheet(
      "QFrame { background: rgba(255, 255, 255, 210); border: 1px solid #ddd; "
      "border-radius: 6px; }");
  legend->setAttribute(Qt::WA_TransparentForMouseEvents, true);

  auto *layout = new QVBoxLayout(legend);
  layout->setContentsMargins(8, 6, 8, 6);
  layout->setSpacing(4);

  auto addLineItem = [&](const QString &text, const QColor &color,
                         Qt::PenStyle style) {
    auto *row = new QHBoxLayout();
    row->setSpacing(6);
    auto *line = new QFrame();
    line->setFixedSize(26, 2);
    QString css = "border-top: 2px ";
    css += (style == Qt::DashLine) ? "dashed " : "solid ";
    css += color.name() + ";";
    line->setStyleSheet(css);
    auto *label = new QLabel(text);
    row->addWidget(line);
    row->addWidget(label);
    row->addStretch();
    layout->addLayout(row);
  };

  auto addPointItem = [&](const QString &text, const QColor &color) {
    auto *row = new QHBoxLayout();
    row->setSpacing(6);
    auto *dot = new QFrame();
    dot->setFixedSize(8, 8);
    dot->setStyleSheet(QString("background:%1; border: 1px solid #333;")
                           .arg(color.name()));
    auto *label = new QLabel(text);
    row->addWidget(dot);
    row->addWidget(label);
    row->addStretch();
    layout->addLayout(row);
  };

  addLineItem("信号", QColor("#1976D2"), Qt::SolidLine);
  addLineItem("均值", QColor("#90A4AE"), Qt::DashLine);
  addLineItem("峰值阈值", QColor("#E53935"), Qt::DashLine);
  addLineItem("恢复上限/下限", QColor("#43A047"), Qt::DashLine);
  addPointItem("峰值点", QColor("#F44336"));
  addPointItem("谷值点", QColor("#4CAF50"));

  auto *wrap = new QWidget();
  wrap->setAttribute(Qt::WA_TransparentForMouseEvents, true);
  wrap->setStyleSheet("background: transparent;");
  auto *wrap_layout = new QVBoxLayout(wrap);
  wrap_layout->setContentsMargins(0, 0, 10, 10);
  wrap_layout->addWidget(legend, 0, Qt::AlignTop | Qt::AlignRight);
  return wrap;
}

void MainWindow::addMapPoint(double lat, double lon) {
  if (!map_view_) {
    return;
  }
  if (lat == 0.0 && lon == 0.0) {
    return;
  }
  if (QObject *root = map_view_->rootObject()) {
    QMetaObject::invokeMethod(root, "addPoint", Q_ARG(QVariant, lat),
                              Q_ARG(QVariant, lon));
  }
}

void MainWindow::setMapInitialCenterFromGnss(const QString &path) {
  if (!map_view_) {
    return;
  }
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
    return;
  }

  GNSSProcessor parser;
  QTextStream in(&file);
  while (!in.atEnd()) {
    QString line = in.readLine().trimmed();
    if (line.isEmpty() || !line.contains('$')) {
      continue;
    }

    QString nmea = line;
    int firstComma = line.indexOf(',');
    if (firstComma > 0) {
      int secondComma = line.indexOf(',', firstComma + 1);
      if (secondComma > 0 && secondComma + 1 < line.size()) {
        nmea = line.mid(secondComma + 1);
      }
    }

    if (!nmea.startsWith('$')) {
      continue;
    }
    parser.processNMEA(nmea, 0);
    const double lat = parser.getLatitude();
    const double lon = parser.getLongitude();
    if ((lat != 0.0 || lon != 0.0) && parser.isValidFix()) {
      if (QObject *root = map_view_->rootObject()) {
        QMetaObject::invokeMethod(root, "setInitialCenter",
                                  Q_ARG(QVariant, lat),
                                  Q_ARG(QVariant, lon));
      }
      break;
    }
  }
}

void MainWindow::resetSimulation() {
  if (worker_) {
    worker_->stop();
  }
  if (map_view_) {
    if (QObject *root = map_view_->rootObject()) {
      QMetaObject::invokeMethod(root, "resetTrack");
    }
  }
  if (map_view_) {
    if (QObject *root = map_view_->rootObject()) {
      QMetaObject::invokeMethod(root, "resetTrack");
    }
  }

  detector_.reset();
  syncParamsToDetector();
  is_playing_ = false;
  play_btn_->setText("开始");
  total_distance_m_ = 0.0;
  last_stroke_length_m_ = 0.0;
  has_prev_output_ = false;
  prev_output_lat_ = 0.0;
  prev_output_lon_ = 0.0;
  training_stroke_count_ = 0;
  ui_stroke_count_ = 0;
  ui_stroke_count_ = 0;
  training_start_sim_ms_ = 0;
  training_start_wall_time_ = QDateTime();
  training_strokes_.clear();
  last_gnss_lat_ = 0.0;
  last_gnss_lon_ = 0.0;
  gnss_has_fix_ = false;
  if (time_base_label_) time_base_label_->setText("--");
  if (gnss_offset_label_) gnss_offset_label_->setText("--");
  if (imu_jitter_label_) imu_jitter_label_->setText("--");
  if (gnss_jitter_label_) gnss_jitter_label_->setText("--");
  // 重置仿真线程时间轴
  if (worker_) {
    worker_->load_gnss_data(QString()); // 清空GNSS缓存
  }
  if (export_btn_) {
    export_btn_->setEnabled(false);
  }
  last_sim_time_ms_ = 0;
  signal_series_->clear();
  peak_scatter_->clear();
  trough_scatter_->clear();
  mean_line_->clear();
  peak_threshold_line_->clear();
  recovery_upper_line_->clear();
  recovery_lower_line_->clear();
  clearPeakLabels();

  if (!data_.timestamps_ms.empty()) {
    worker_->load_data(data_);
  }
}

void MainWindow::syncParamsToDetector() {
  detector_.MIN_PEAK_ABSOLUTE = min_peak_sb_->value();
  detector_.TROUGH_THRESHOLD = trough_threshold_sb_->value();
  detector_.MIN_AMPLITUDE = min_amp_sb_->value();
  detector_.PEAK_ENTER_FACTOR = peak_factor_sb_->value();
  detector_.RECOVERY_FACTOR = recovery_factor_sb_->value();
  detector_.MIN_PEAK_DURATION = min_peak_duration_sb_->value();
  detector_.MIN_TROUGH_DURATION = min_trough_duration_sb_->value();
  detector_.COOLDOWN_DURATION = cooldown_sb_->value();
  detector_.STROKE_MIN_INTERVAL = min_interval_sb_->value();
  detector_.WINDOW_SIZE = window_size_sb_->value();
  detector_.CALIBRATION_DURATION = calibration_sb_->value();
}

void MainWindow::syncParamsFromDetector() {
  min_peak_sb_->blockSignals(true);
  trough_threshold_sb_->blockSignals(true);
  min_amp_sb_->blockSignals(true);
  peak_factor_sb_->blockSignals(true);
  recovery_factor_sb_->blockSignals(true);
  min_peak_duration_sb_->blockSignals(true);
  min_trough_duration_sb_->blockSignals(true);
  cooldown_sb_->blockSignals(true);
  min_interval_sb_->blockSignals(true);
  window_size_sb_->blockSignals(true);
  calibration_sb_->blockSignals(true);

  min_peak_sb_->setValue(detector_.MIN_PEAK_ABSOLUTE);
  trough_threshold_sb_->setValue(detector_.TROUGH_THRESHOLD);
  min_amp_sb_->setValue(detector_.MIN_AMPLITUDE);
  peak_factor_sb_->setValue(detector_.PEAK_ENTER_FACTOR);
  recovery_factor_sb_->setValue(detector_.RECOVERY_FACTOR);
  min_peak_duration_sb_->setValue(detector_.MIN_PEAK_DURATION);
  min_trough_duration_sb_->setValue(detector_.MIN_TROUGH_DURATION);
  cooldown_sb_->setValue(detector_.COOLDOWN_DURATION);
  min_interval_sb_->setValue(detector_.STROKE_MIN_INTERVAL);
  window_size_sb_->setValue(detector_.WINDOW_SIZE);
  calibration_sb_->setValue(detector_.CALIBRATION_DURATION);

  min_peak_sb_->blockSignals(false);
  trough_threshold_sb_->blockSignals(false);
  min_amp_sb_->blockSignals(false);
  peak_factor_sb_->blockSignals(false);
  recovery_factor_sb_->blockSignals(false);
  min_peak_duration_sb_->blockSignals(false);
  min_trough_duration_sb_->blockSignals(false);
  cooldown_sb_->blockSignals(false);
  min_interval_sb_->blockSignals(false);
  window_size_sb_->blockSignals(false);
  calibration_sb_->blockSignals(false);
}

// ========== CSV & Simulation ==========

void MainWindow::onLoadCsv() {
  const QString path = QFileDialog::getOpenFileName(this, "选择CSV", QString(),
                                                    "CSV 文件 (*.csv)");
  if (path.isEmpty()) {
    return;
  }

  CsvData data;
  std::string error;
  if (!load_csv(path.toStdString(), data, error)) {
    QMessageBox::warning(this, "错误", QString::fromStdString(error));
    return;
  }

  double mean_diff = 0.0;
  for (size_t i = 1; i < data.timestamps_ms.size(); ++i) {
    mean_diff += (data.timestamps_ms[i] - data.timestamps_ms[i - 1]);
  }
  mean_diff /= static_cast<double>(data.timestamps_ms.size() - 1);

  if (mean_diff > 0.0 && mean_diff < 1.0) {
    for (double &ts : data.timestamps_ms) {
      ts *= 1000.0;
    }
    mean_diff *= 1000.0;
  }

  double actual_sample_rate = (mean_diff > 0.0) ? (1000.0 / mean_diff) : 0.0;
  if (actual_sample_rate > 0.0 && std::abs(actual_sample_rate - 125.0) > 10.0) {
    const double nyquist = actual_sample_rate / 2.0;
    const double optimal_cutoff = std::min(3.0, nyquist * 0.15);
    detector_.configure_filters(1.0, actual_sample_rate);
  } else {
    detector_.configure_filters(1.0, 62.5);
  }

  data_ = data; // Store loaded data in member variable
  resetSimulation();
  worker_->load_data(data_);

  // Try to auto-load GNSS data
  QFileInfo imuInfo(path);
  QString imuName = imuInfo.fileName(); // e.g. imu_log_032.csv
  if (imuName.startsWith("imu_log_") && imuName.endsWith(".csv")) {
    QString numPart = imuName.mid(8);     // 032.csv
    QString gnssName = "gnss_" + numPart; // gnss_032.csv
    QString gnssPath = imuInfo.absoluteDir().filePath(gnssName);
    if (QFile::exists(gnssPath)) {
      worker_->load_gnss_data(gnssPath);
      qDebug() << "Auto-loaded GNSS data from:" << gnssPath;
    }
  }
  play_btn_->setEnabled(true);
  load_btn_->setText(
      QString("已加载 %1 条样本").arg(data_.timestamps_ms.size()));
}

void MainWindow::onTogglePlay() {
  if (!is_playing_) {
    is_playing_ = true;
    play_btn_->setText("暂停");
    if (!worker_thread_->isRunning()) {
      worker_thread_->start();
    } else if (worker_) {
      worker_->resume();
    }
  } else {
    is_playing_ = false;
    play_btn_->setText("继续");
    if (worker_) {
      worker_->pause();
    }
  }
}

void MainWindow::onSpeedChanged(const QString &text) {
  QString t = text;
  t.remove('x');
  bool ok = false;
  double v = t.toDouble(&ok);
  if (ok && worker_) {
    worker_->set_speed(v);
  }
}

void MainWindow::onAxisChanged(int id) { detector_.set_active_axis(id); }

void MainWindow::onSimulationFinished() {
  is_playing_ = false;
  play_btn_->setText("开始");
  QMessageBox::information(
      this, "完成",
      QString("模拟结束。\n总划桨数: %1").arg(detector_.stroke_count()));
}

void MainWindow::updateStateIndicator() {
  int state = detector_.stroke_state();
  QString text;
  QString color;
  switch (state) {
  case RealtimeStrokeDetector::STATE_BACKGROUND:
    text = "背景";
    color = "#9E9E9E";
    break;
  case RealtimeStrokeDetector::STATE_PEAK_ZONE:
    text = "峰值";
    color = "#F44336";
    break;
  case RealtimeStrokeDetector::STATE_TROUGH_ZONE:
    text = "谷值";
    color = "#4CAF50";
    break;
  case RealtimeStrokeDetector::STATE_COOLDOWN:
    text = "冷却";
    color = "#FF9800";
    break;
  default:
    text = "未知";
    color = "#9E9E9E";
    break;
  }
  state_label_->setText(text);
  state_label_->setStyleSheet(QString("background:%1;color:white;font-weight:"
                                      "bold;padding:8px;border-radius:4px;")
                                  .arg(color));
}

void MainWindow::updateChart() {
  const auto &ft = detector_.filtered_t();
  const auto &fx = detector_.filtered_x();
  const auto &fy = detector_.filtered_y();
  const auto &fz = detector_.filtered_z();
  const auto &rt = detector_.raw_t();
  const auto &rx = detector_.raw_x();
  const auto &ry = detector_.raw_y();
  const auto &rz = detector_.raw_z();

  if (ft.empty() && rt.empty()) {
    return;
  }

  const int axis_id = axis_group_->checkedId();
  const std::vector<double> *axis_raw = &rz;
  if (axis_id == 0)
    axis_raw = &rx;
  if (axis_id == 1)
    axis_raw = &ry;

  const std::vector<double> *axis_filtered = &fz;
  if (axis_id == 0)
    axis_filtered = &fx;
  if (axis_id == 1)
    axis_filtered = &fy;

  const std::vector<double> *axis_data = axis_filtered;
  const std::vector<double> *time_data = &ft;

  if (axis_data->empty() || time_data->empty()) {
    return;
  }

  const size_t n = std::min(axis_data->size(), time_data->size());
  const double time_offset = (*time_data)[0];
  last_time_offset_ = time_offset;
  QVector<QPointF> points;
  points.reserve(static_cast<int>(n));
  for (size_t i = 0; i < n; ++i) {
    points.append(QPointF((*time_data)[i] - time_offset, (*axis_data)[i]));
  }
  signal_series_->replace(points);

  const double mean = detector_.background_mean();
  const double std = std::max(detector_.background_std(), 0.01);
  double peak_threshold =
      std::max(detector_.PEAK_ENTER_FACTOR * std, detector_.MIN_PEAK_ABSOLUTE);
  const double recovery_threshold = detector_.RECOVERY_FACTOR * std;

  const double min_x = 0.0;
  const double max_x = (*time_data).back() - time_offset;

  mean_line_->replace(
      QList<QPointF>{QPointF(min_x, mean), QPointF(max_x, mean)});
  peak_threshold_line_->replace(
      QList<QPointF>{QPointF(min_x, mean + peak_threshold),
                     QPointF(max_x, mean + peak_threshold)});
  recovery_upper_line_->replace(
      QList<QPointF>{QPointF(min_x, mean + recovery_threshold),
                     QPointF(max_x, mean + recovery_threshold)});
  recovery_lower_line_->replace(
      QList<QPointF>{QPointF(min_x, mean - recovery_threshold),
                     QPointF(max_x, mean - recovery_threshold)});

  QVector<QPointF> peak_points;
  QVector<QPointF> trough_points;
  for (const auto &stroke : detector_.detected_strokes()) {
    peak_points.append(
        QPointF(stroke.peak_time - time_offset, stroke.peak_filtered));
    trough_points.append(
        QPointF(stroke.trough_time - time_offset, stroke.trough_filtered));
  }
  peak_scatter_->replace(peak_points);
  trough_scatter_->replace(trough_points);
  updatePeakLabels(time_offset);

  if (auto_tracking_) {
    double latest = max_x;
    double min_view = std::max(min_x, latest - 10.0);
    x_axis_->setRange(min_view, std::max(min_view + 10.0, latest + 0.5));
  } else {
    x_axis_->setRange(min_x, max_x);
  }

  double y_min = (*axis_data)[0];
  double y_max = (*axis_data)[0];
  for (double v : *axis_data) {
    y_min = std::min(y_min, v);
    y_max = std::max(y_max, v);
  }
  double y_range = y_max - y_min;
  if (y_range < 0.1) {
    double center = (y_min + y_max) / 2.0;
    y_axis_->setRange(center - 0.5, center + 0.5);
  } else {
    double margin = y_range * 0.1;
    y_axis_->setRange(y_min - margin, y_max + margin);
  }
}

void MainWindow::updateUi() {
  if (!data_.timestamps_ms.empty() && last_sim_time_ms_ >= 0) {
    const double elapsed_s = last_sim_time_ms_ / 1000.0;
    time_label_->setText(QString("时间: %1s").arg(elapsed_s, 0, 'f', 2));
  }
  count_label_->setText(QString("划桨: %1").arg(detector_.stroke_count()));
  rate_label_->setText(
      QString("频率: %1 SPM").arg(detector_.stroke_rate(), 0, 'f', 1));
  if (stroke_length_label_ && distance_label_) {
    const bool training_active = training_mode_ && training_mode_->isActive();
    const int display_strokes =
        training_active ? training_stroke_count_ : ui_stroke_count_;
    if (display_strokes > 0) {
      stroke_length_label_->setText(
          QString("划距: %1 m").arg(last_stroke_length_m_, 0, 'f', 2));
      distance_label_->setText(
          QString("累计距离: %1 m").arg(total_distance_m_, 0, 'f', 2));
    } else {
      stroke_length_label_->setText("划距: -- m");
      distance_label_->setText("累计距离: -- m");
    }
  }

  const QString axis_name = (axis_group_->checkedId() == 0)
                                ? "X"
                                : (axis_group_->checkedId() == 1 ? "Y" : "Z");
  axis_label_->setText(QString("轴: %1").arg(axis_name));

  updateStateIndicator();

  double deviation = 0.0;
  double peak_threshold = 0.0;
  double recovery_threshold = 0.0;
  double phase_duration_ms = 0.0;
  if (!detector_.filtered_t().empty()) {
    const double mean = detector_.background_mean();
    const double std = detector_.background_std();
    peak_threshold = std::max(detector_.PEAK_ENTER_FACTOR * std,
                              detector_.MIN_PEAK_ABSOLUTE);
    recovery_threshold = detector_.RECOVERY_FACTOR * std;
    int axis_id = axis_group_->checkedId();
    const auto &axis_data =
        (axis_id == 0)
            ? detector_.filtered_x()
            : (axis_id == 1 ? detector_.filtered_y() : detector_.filtered_z());
    if (!axis_data.empty()) {
      deviation = axis_data.back() - mean;
    }
    if (detector_.phase_start_time() > 0) {
      phase_duration_ms =
          detector_.filtered_t().back() * 1000.0 - detector_.phase_start_time();
      if (phase_duration_ms < 0.0) {
        phase_duration_ms = 0.0;
      }
    }
  }

  deviation_label_->setText(QString("偏差: %1g").arg(deviation, 0, 'f', 3));
  threshold_label_->setText(
      QString("阈值: %1g").arg(peak_threshold, 0, 'f', 3));
  duration_label_->setText(
      QString("持续: %1ms").arg(static_cast<int>(phase_duration_ms)));
  if (detector_.stroke_state() == RealtimeStrokeDetector::STATE_PEAK_ZONE) {
    peak_label_->setText(
        QString("峰值: %1g").arg(detector_.peak_max_value(), 0, 'f', 3));
  } else {
    peak_label_->setText("峰值: --");
  }
  if (detector_.stroke_state() == RealtimeStrokeDetector::STATE_TROUGH_ZONE) {
    trough_label_->setText(
        QString("谷值: %1g").arg(detector_.trough_min_value(), 0, 'f', 3));
    recovery_label_->setText(QString("恢复: %1/%2")
                                 .arg(detector_.recovery_counter())
                                 .arg(detector_.RECOVERY_SAMPLES));
  } else {
    trough_label_->setText("谷值: --");
    recovery_label_->setText("恢复: 0");
  }

  updateChart();

  // Update LVGL UI
  if (lvgl_widget_) {
    const bool training_active = training_mode_ && training_mode_->isActive();
    const float display_rate =
        training_active ? static_cast<float>(detector_.stroke_rate()) : 0.0f;
    const int display_count = training_active ? training_stroke_count_ : 0;
    const float display_stroke_length =
        training_active ? static_cast<float>(last_stroke_length_m_) : 0.0f;
    const float display_distance =
        training_active ? static_cast<float>(total_distance_m_) : 0.0f;

    lvgl_widget_->updateStrokeRate(display_rate);
    lvgl_widget_->updateStrokeCount(display_count);
    lvgl_widget_->updateStrokeLength(display_stroke_length);
    lvgl_widget_->updateDistance(display_distance);

    if (training_mode_) {
      training_mode_->update(last_sim_time_ms_);
    }

    const long elapsed_sec_train =
        (training_mode_ && training_mode_->isActive())
            ? training_mode_->getElapsedSeconds()
            : 0;

    const int total_secs = static_cast<int>(elapsed_sec_train);
    const int mins = total_secs / 60;
    const int secs = total_secs % 60;

    const QString time_str = QString("%1:%2")
                                 .arg(mins, 2, 10, QChar('0'))
                                 .arg(secs, 2, 10, QChar('0'));
    lvgl_widget_->updateTimer(time_str);

    // 同时更新屏幕右上角 Label10 为北京时间
    const auto now = QDateTime::currentDateTimeUtc().toOffsetFromUtc(8 * 3600);
    lvgl_widget_->updateClock(now.time().toString("HH:mm"));

    const float display_speed = static_cast<float>(last_gnss_speed_mps_);
    const QString display_pace = last_gnss_pace_str_;
    lvgl_widget_->updateSpeed(display_speed);
    lvgl_widget_->updatePace(display_pace);
  }
}

// ========== K1-K4 Button Handlers ==========

void MainWindow::onK1Pressed() {
  if (lvgl_widget_) {
    lvgl_widget_->pressK1();
  }
}

void MainWindow::onK2Pressed() {
  if (lvgl_widget_) {
    lvgl_widget_->pressK2();
  }
}

void MainWindow::onK3Pressed() {
  if (lvgl_widget_) {
    lvgl_widget_->pressK3();
  }
}

void MainWindow::onK4Pressed() {
  k4_press_time_ = QDateTime::currentMSecsSinceEpoch();
}

void MainWindow::onK4Released() {
  qint64 now = QDateTime::currentMSecsSinceEpoch();
  if (now - k4_press_time_ > 2000) {
    // Long Press: Toggle Training
    if (training_mode_) {
      if (training_mode_->isActive()) {
        training_mode_->stop();
        QMessageBox::information(this, "训练", "训练已停止");
      } else {
        training_mode_->start();
        // Override timer manually for demo feeling if needed
      }
    }
  } else {
    // Short Press: Normal K4 action
    if (lvgl_widget_) {
      lvgl_widget_->pressK4();
    }
  }
}

void MainWindow::onTrainingStarted() {
  if (lvgl_widget_) {
    lvgl_widget_->setTrainingActive(true);
    lvgl_widget_->updateStrokeRate(0.0f);
    lvgl_widget_->updateStrokeCount(0);
    lvgl_widget_->updateStrokeLength(0.0f);
    lvgl_widget_->updateDistance(0.0f);
    lvgl_widget_->updateTimer("00:00");
  }

  total_distance_m_ = 0.0;
  last_stroke_length_m_ = 0.0;
  has_prev_output_ = false;
  prev_output_lat_ = 0.0;
  prev_output_lon_ = 0.0;
  training_stroke_count_ = 0;
  ui_stroke_count_ = 0;
  training_start_sim_ms_ = 0;
  training_start_wall_time_ = QDateTime();
  training_strokes_.clear();
  if (export_btn_) {
    export_btn_->setEnabled(false);
  }
}

void MainWindow::onTrainingStopped() {
  if (lvgl_widget_) {
    lvgl_widget_->setTrainingActive(false);
    lvgl_widget_->updateStrokeRate(0.0f);
    lvgl_widget_->updateStrokeCount(0);
    lvgl_widget_->updateStrokeLength(0.0f);
    lvgl_widget_->updateDistance(0.0f);
    lvgl_widget_->updateTimer("00:00");
  }

  total_distance_m_ = 0.0;
  last_stroke_length_m_ = 0.0;
  has_prev_output_ = false;
  prev_output_lat_ = 0.0;
  prev_output_lon_ = 0.0;
  training_stroke_count_ = 0;
  training_start_sim_ms_ = 0;
  training_start_wall_time_ = QDateTime();
}

void MainWindow::onGnssUpdated(double speed_mps, double lat, double lon,
                               int sats, QString pace, const QString &hdop,
                               const QString &fix, const QString &diff_age) {
  last_gnss_speed_mps_ = speed_mps;
  last_gnss_lat_ = lat;
  last_gnss_lon_ = lon;
  last_gnss_pace_str_ = pace;
  gnss_has_fix_ = (lat != 0.0 && lon != 0.0);

  // 更新GNSS状态标签（若存在）
  if (lvgl_widget_) {
    // 这些Label命名与固件一致：ui_Label15(卫星数), ui_Label58(解状态), ui_Label65(差分龄期), ui_Label43(HDOP)
    char buf[16];
    snprintf(buf, sizeof(buf), "%02d", sats);
    lv_label_set_text(ui_Label15, buf);
    lv_label_set_text(ui_Label58, fix.toUtf8().constData());
    lv_label_set_text(ui_Label65, diff_age.toUtf8().constData());
    lv_label_set_text(ui_Label43, hdop.toUtf8().constData());
  }

  // 实时轨迹：每次 GNSS 更新都推进折线
  if (gnss_has_fix_) {
    addMapPoint(lat, lon);
  }
}

void MainWindow::addPeakLabel(const StrokeEvent &event) {
  if (!chart_ || !chart_->scene()) {
    return;
  }
  if (event.latitude == 0.0 && event.longitude == 0.0) {
    return;
  }

  PeakLabel label;
  label.peak_time = event.peak_time;
  label.peak_value = event.peak_filtered;
  label.text = QString::number(event.latitude, 'f', 6) + "\n" +
               QString::number(event.longitude, 'f', 6);

  auto *item = new QGraphicsTextItem(label.text);
  QFont font = item->font();
  font.setPointSize(8);
  item->setFont(font);
  item->setDefaultTextColor(QColor(0, 0, 0));
  item->setZValue(10);
  chart_->scene()->addItem(item);
  label.item = item;
  peak_labels_.push_back(label);

  const int kMaxLabels = 80;
  if (static_cast<int>(peak_labels_.size()) > kMaxLabels) {
    auto &old = peak_labels_.front();
    if (old.item) {
      chart_->scene()->removeItem(old.item);
      delete old.item;
    }
    peak_labels_.erase(peak_labels_.begin());
  }
}

void MainWindow::updatePeakLabels(double time_offset) {
  if (!chart_) {
    return;
  }
  for (auto &label : peak_labels_) {
    if (!label.item) {
      continue;
    }
    const double x = label.peak_time - time_offset;
    const double y = label.peak_value;
    QPointF pos = chart_->mapToPosition(QPointF(x, y), signal_series_);
    label.item->setPos(pos + QPointF(6.0, -14.0));
  }
}

void MainWindow::clearPeakLabels() {
  if (!chart_ || !chart_->scene()) {
    peak_labels_.clear();
    return;
  }
  for (auto &label : peak_labels_) {
    if (label.item) {
      chart_->scene()->removeItem(label.item);
      delete label.item;
    }
  }
  peak_labels_.clear();
}

void MainWindow::onTimeSyncUpdated(double base_time_ms, double gnss_offset_ms,
                                   double imu_mean_dt, double imu_min_dt,
                                   double imu_max_dt, double imu_std_dt,
                                   double gnss_mean_dt, double gnss_min_dt,
                                   double gnss_max_dt, double gnss_std_dt) {
  if (time_base_label_) {
    time_base_label_->setText(QString::number(static_cast<qint64>(base_time_ms)));
  }
  if (gnss_offset_label_) {
    if (std::isnan(gnss_offset_ms)) {
      gnss_offset_label_->setText("--");
    } else {
      gnss_offset_label_->setText(QString::number(gnss_offset_ms, 'f', 1) + " ms");
    }
  }

  auto fmt_jitter = [](double mean, double mn, double mx, double stddev) {
    if (mean <= 0.0)
      return QString("--");
    return QString("均值 %1 ms | [%2, %3] | σ=%4 ms")
        .arg(QString::number(mean, 'f', 2))
        .arg(QString::number(mn, 'f', 2))
        .arg(QString::number(mx, 'f', 2))
        .arg(QString::number(stddev, 'f', 2));
  };

  if (imu_jitter_label_) {
    imu_jitter_label_->setText(fmt_jitter(imu_mean_dt, imu_min_dt, imu_max_dt,
                                          imu_std_dt));
  }
  if (gnss_jitter_label_) {
    gnss_jitter_label_->setText(
        fmt_jitter(gnss_mean_dt, gnss_min_dt, gnss_max_dt, gnss_std_dt));
  }
}

void MainWindow::onLoadGnss() {
  QString file_name = QFileDialog::getOpenFileName(
      this, "打开GNSS数据文件", "",
      "CSV 文件 (*.csv);;JSONL 文件 (*.jsonl);;所有文件 (*)");
  if (file_name.isEmpty()) {
    return;
  }

  worker_->load_gnss_data(file_name);
  setMapInitialCenterFromGnss(file_name);
  QMessageBox::information(this, "GNSS数据",
                           QString("GNSS数据加载自:\n%1").arg(file_name));
}

void MainWindow::onStrokeDetected(const StrokeEvent &event) {
  qDebug() << "Stroke Detected! Distance:" << event.distance_m << "m"
           << "Lat:" << event.latitude << "Lon:" << event.longitude;

  const qint64 event_time_ms = static_cast<qint64>(event.peak_time * 1000.0);
  const qint64 sim_time_ms =
      (event_time_ms > 0) ? event_time_ms : last_sim_time_ms_;

  if (training_mode_) {
    training_mode_->onStrokeDetected(sim_time_ms);
  }

  const bool training_active = training_mode_ && training_mode_->isActive();

  if (training_active && !training_start_wall_time_.isValid()) {
    training_start_wall_time_ = QDateTime::currentDateTime();
    training_start_sim_ms_ = sim_time_ms;
  }

  double lat = event.latitude;
  double lon = event.longitude;
  if ((lat == 0.0 && lon == 0.0) && gnss_has_fix_) {
    lat = last_gnss_lat_;
    lon = last_gnss_lon_;
  }

  const bool coords_complete = (lat != 0.0 && lon != 0.0);
  const double curr_lat = coords_complete ? roundToDecimals(lat, 7) : 0.0;
  const double curr_lon = coords_complete ? roundToDecimals(lon, 7) : 0.0;

  const int display_strokes =
      training_active ? training_stroke_count_ : ui_stroke_count_;
  if (display_strokes == 0) {
    total_distance_m_ = 0.0;
    has_prev_output_ = false;
  }

  double stroke_length_m = 0.0;
  if (coords_complete && has_prev_output_) {
    const double raw_dist =
        haversineMeters(prev_output_lat_, prev_output_lon_, curr_lat, curr_lon);
    stroke_length_m = roundToDecimals(raw_dist, 2);
  }

  if (coords_complete) {
    prev_output_lat_ = curr_lat;
    prev_output_lon_ = curr_lon;
    has_prev_output_ = true;
    addMapPoint(curr_lat, curr_lon);
    // 标记桨点：旧桨变黄，当前桨红
    if (map_view_) {
      if (QObject *root = map_view_->rootObject()) {
        QMetaObject::invokeMethod(root, "addStrokeMarker",
                                  Q_ARG(QVariant, curr_lat),
                                  Q_ARG(QVariant, curr_lon));
      }
    }
  }

  total_distance_m_ += stroke_length_m;
  last_stroke_length_m_ = stroke_length_m;
  if (training_active) {
    training_stroke_count_ += 1;
  } else {
    ui_stroke_count_ += 1;
  }

  if (!training_active) {
    addPeakLabel(event);
    return;
  }

  StrokeExportRow row;
  row.stroke_number = training_stroke_count_;
  row.stroke_length_m = stroke_length_m;
  row.total_distance_m = total_distance_m_;
  const double speed_mps = last_gnss_speed_mps_;
  row.speed_mps = speed_mps;
  row.pace_str =
      (speed_mps > 0.05) ? formatSplitSeconds(500.0 / speed_mps) : "--";
  row.stroke_rate_spm =
      (row.stroke_number == 1) ? 0.0 : detector_.stroke_rate();
  row.latitude = curr_lat;
  row.longitude = curr_lon;
  if (training_mode_) {
    row.elapsed_seconds =
        static_cast<double>(training_mode_->getElapsedMillis()) / 1000.0;
  }

  if (training_start_wall_time_.isValid()) {
    const qint64 offset_ms = sim_time_ms - training_start_sim_ms_;
    row.timestamp = training_start_wall_time_.addMSecs(offset_ms).toString(
        "yyyy-MM-dd HH:mm:ss.zzz");
  } else {
    row.timestamp =
        QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss.zzz");
  }

  training_strokes_.push_back(row);
  if (export_btn_) {
    export_btn_->setEnabled(!training_strokes_.empty());
  }

  // Add lat/lon label at peak point on waveform
  addPeakLabel(event);
}

void MainWindow::onSimTimeUpdated(qint64 sim_time_ms) {
  last_sim_time_ms_ = sim_time_ms;
}

void MainWindow::onExportStrokes() {
  if (training_strokes_.empty()) {
    QMessageBox::information(this, "导出", "没有可导出的训练划桨数据。");
    return;
  }

  const QString default_name =
      QString("strokes_%1.csv")
          .arg(training_mode_ ? training_mode_->getTrainId() : "training");
  const QString path = QFileDialog::getSaveFileName(
      this, "导出划桨CSV", default_name, "CSV 文件 (*.csv)");
  if (path.isEmpty()) {
    return;
  }

  QFile file(path);
  if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
    QMessageBox::warning(this, "导出", "无法打开文件进行写入。");
    return;
  }

  QTextStream out(&file);
  // Only English header row
  out << "boat_code,stroke_length_m,total_distance_m,elapsed_seconds,pace_per_500m,"
         "speed_mps,stroke_rate_spm,stroke_count,lat,lon,timestamp\n";

  const QString boat_code = "01";
  for (const auto &row : training_strokes_) {
    out << boat_code << ',';
    out << QString::number(row.stroke_length_m, 'f', 2) << ',';
    out << QString::number(row.total_distance_m, 'f', 2) << ',';
    out << QString::number(row.elapsed_seconds, 'f', 1) << ',';
    out << row.pace_str << ',';
    out << QString::number(row.speed_mps, 'f', 2) << ',';
    out << QString::number(row.stroke_rate_spm, 'f', 1) << ',';
    out << row.stroke_number << ',';
    out << QString::number(row.latitude, 'f', 7) << ',';
    out << QString::number(row.longitude, 'f', 7) << ',';
    out << row.timestamp << '\n';
  }

  QMessageBox::information(this, "导出",
                           QString("已导出 %1 条划桨到:\n%2")
                               .arg(training_strokes_.size())
                               .arg(path));
}
