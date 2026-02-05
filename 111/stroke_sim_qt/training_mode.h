#ifndef TRAINING_MODE_H
#define TRAINING_MODE_H

#include <QDateTime>
#include <QObject>
#include <QString>


class TrainingMode : public QObject {
  Q_OBJECT

public:
  explicit TrainingMode(QObject *parent = nullptr);

  bool isActive() const { return active_; }
  bool isRunning() const { return running_; }
  bool isPaused() const { return paused_; }

  void start();
  void stop();
  void onStrokeDetected(qint64 sim_time_ms);
  void update(qint64 sim_time_ms); // Called periodically with sim time

  // Getters for UI
  qint64 getElapsedMillis() const;
  long getElapsedSeconds() const;
  QString getTrainId() const { return trainId_; }

signals:
  void trainingStarted();
  void trainingStopped();
  void trainingPaused();
  void trainingResumed();

private:
  bool active_ = false;
  bool running_ = false;
  bool paused_ = false;

  qint64 currentTimeMs_ = 0;   // sim time in ms
  qint64 startTime_ = 0;       // sim time in ms
  qint64 lastStrokeTime_ = 0;  // sim time in ms
  qint64 pauseStartTime_ = 0;  // sim time in ms
  qint64 totalPausedTime_ = 0; // ms

  QString trainId_;

  // Constants aligned with embedded firmware
  const int AUTO_PAUSE_TIMEOUT_MS = 10000; // 10s
  const int AUTO_STOP_TIMEOUT_MS = 300000; // 5min

  void checkPauseConditions();
  QString generateTrainId();
};

#endif // TRAINING_MODE_H
