#include "training_mode.h"
#include <QDateTime>
#include <QDebug>


TrainingMode::TrainingMode(QObject *parent) : QObject(parent) {}

void TrainingMode::start() {
  if (active_)
    return;

  active_ = true;
  running_ = false; // Wait for first stroke to start timing
  paused_ = false;
  currentTimeMs_ = 0;
  startTime_ = 0;
  lastStrokeTime_ = 0;
  pauseStartTime_ = 0;
  totalPausedTime_ = 0;

  trainId_ = generateTrainId();
  qDebug() << "[TrainingMode] Training READY. TrainID:" << trainId_;

  // In actual firmware, start() resets data managers.
  // Here we emit signal to notify UI/Main logic
  emit trainingStarted();
}

void TrainingMode::stop() {
  if (!active_)
    return;

  active_ = false;
  running_ = false;
  paused_ = false;

  qDebug() << "[TrainingMode] Training STOPPED. Duration:"
           << getElapsedSeconds() << "s";
  emit trainingStopped();
}

void TrainingMode::onStrokeDetected(qint64 sim_time_ms) {
  if (!active_)
    return;

  currentTimeMs_ = sim_time_ms;
  lastStrokeTime_ = sim_time_ms;

  // First stroke triggers running state
  if (!running_) {
    running_ = true;
    startTime_ = sim_time_ms;
    paused_ = false;
    pauseStartTime_ = 0;
    totalPausedTime_ = 0;
    qDebug() << "[TrainingMode] First stroke detected -> TIMING STARTED";
    emit trainingResumed();
  }

  // If paused, resume
  if (paused_) {
    paused_ = false;
    if (pauseStartTime_ > 0) {
      totalPausedTime_ += (sim_time_ms - pauseStartTime_);
      pauseStartTime_ = 0;
      qDebug() << "[TrainingMode] Resumed from pause";
      emit trainingResumed();
    }
  }
}

void TrainingMode::update(qint64 sim_time_ms) {
  currentTimeMs_ = sim_time_ms;
  if (!active_ || !running_)
    return;

  checkPauseConditions();
}

void TrainingMode::checkPauseConditions() {
  qint64 now = currentTimeMs_;

  // 10s timeout -> Auto Pause
  if (!paused_ && lastStrokeTime_ > 0 &&
      (now - lastStrokeTime_) > AUTO_PAUSE_TIMEOUT_MS) {
    paused_ = true;
    pauseStartTime_ = now;
    qDebug() << "[TrainingMode] Auto-PAUSED (10s inactivity)";
    emit trainingPaused();
  }

  // 5min pause -> Auto Stop
  if (paused_ && (now - pauseStartTime_) > AUTO_STOP_TIMEOUT_MS) {
    qDebug() << "[TrainingMode] Auto-STOPPED (5min pause)";
    stop();
  }
}

long TrainingMode::getElapsedSeconds() const {
  return static_cast<long>(getElapsedMillis() / 1000);
}

qint64 TrainingMode::getElapsedMillis() const {
  if (!active_ || !running_)
    return 0;

  qint64 now = currentTimeMs_;
  qint64 elapsedRaw = now - startTime_ - totalPausedTime_;

  if (paused_ && pauseStartTime_ > 0) {
    elapsedRaw -= (now - pauseStartTime_);
  }

  if (elapsedRaw < 0)
    return 0;

  return elapsedRaw;
}

QString TrainingMode::generateTrainId() {
  return QDateTime::currentDateTime().toString("yyyyMMddHHmmss");
}
