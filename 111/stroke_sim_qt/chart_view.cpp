#include "chart_view.h"

#include <QMouseEvent>
#include <QWheelEvent>

ChartView::ChartView(QChart *chart, QWidget *parent)
    : QChartView(chart, parent) {
  setRubberBand(QChartView::NoRubberBand);
  setDragMode(QGraphicsView::NoDrag);
  setCursor(Qt::OpenHandCursor);
}

void ChartView::setXAxis(QValueAxis *axis) { x_axis_ = axis; }
void ChartView::setYAxis(QValueAxis *axis) { y_axis_ = axis; }

void ChartView::wheelEvent(QWheelEvent *event) {
  const QPoint angle = event->angleDelta();
  if (angle.y() == 0) {
    QChartView::wheelEvent(event);
    return;
  }

  const double factor = (angle.y() > 0) ? 0.85 : 1.15;

  // Zoom Y axis
  if (y_axis_) {
    const double min_y = y_axis_->min();
    const double max_y = y_axis_->max();
    const double center = (min_y + max_y) * 0.5;
    const double half_range = (max_y - min_y) * 0.5 * factor;
    y_axis_->setRange(center - half_range, center + half_range);
  }

  // Zoom X axis around mouse position
  if (x_axis_) {
    const double min_x = x_axis_->min();
    const double max_x = x_axis_->max();
    const double center = (min_x + max_x) * 0.5;
    const double half_range = (max_x - min_x) * 0.5 * factor;
    x_axis_->setRange(center - half_range, center + half_range);
  }

  was_dragged_ = true;
  emit userDragged();
  event->accept();
}

void ChartView::mousePressEvent(QMouseEvent *event) {
  if (event->button() == Qt::LeftButton) {
    is_dragging_ = true;
    last_mouse_pos_ = chart()->mapToValue(event->pos());
    setCursor(Qt::ClosedHandCursor);
    event->accept();
  } else {
    QChartView::mousePressEvent(event);
  }
}

void ChartView::mouseMoveEvent(QMouseEvent *event) {
  if (is_dragging_ && (x_axis_ || y_axis_)) {
    QPointF current_pos = chart()->mapToValue(event->pos());
    QPointF delta = last_mouse_pos_ - current_pos;

    // Pan X axis
    if (x_axis_) {
      double min_x = x_axis_->min();
      double max_x = x_axis_->max();
      x_axis_->setRange(min_x + delta.x(), max_x + delta.x());
    }

    // Pan Y axis
    if (y_axis_) {
      double min_y = y_axis_->min();
      double max_y = y_axis_->max();
      y_axis_->setRange(min_y + delta.y(), max_y + delta.y());
    }

    last_mouse_pos_ = chart()->mapToValue(event->pos());
    was_dragged_ = true;
    event->accept();
  } else {
    QChartView::mouseMoveEvent(event);
  }
}

void ChartView::mouseReleaseEvent(QMouseEvent *event) {
  if (event->button() == Qt::LeftButton && is_dragging_) {
    is_dragging_ = false;
    setCursor(Qt::OpenHandCursor);
    if (was_dragged_) {
      emit userDragged();
    }
    event->accept();
  } else {
    QChartView::mouseReleaseEvent(event);
  }
}
