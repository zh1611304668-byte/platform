#pragma once

#include <QMouseEvent>
#include <QtCharts/QChartView>
#include <QtCharts/QValueAxis>


class ChartView : public QChartView {
  Q_OBJECT

public:
  explicit ChartView(QChart *chart, QWidget *parent = nullptr);

  void setXAxis(QValueAxis *axis);
  void setYAxis(QValueAxis *axis);

  // When dragging, disable auto-tracking
  bool wasDragged() const { return was_dragged_; }
  void resetDragState() { was_dragged_ = false; }

signals:
  void userDragged(); // Emitted when user drags the chart

protected:
  void wheelEvent(QWheelEvent *event) override;
  void mousePressEvent(QMouseEvent *event) override;
  void mouseMoveEvent(QMouseEvent *event) override;
  void mouseReleaseEvent(QMouseEvent *event) override;

private:
  QValueAxis *x_axis_ = nullptr;
  QValueAxis *y_axis_ = nullptr;

  bool is_dragging_ = false;
  bool was_dragged_ = false;
  QPointF last_mouse_pos_;
};
