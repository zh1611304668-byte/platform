#include "lvgl_widget.h"
#include <QDebug>
#include <QPainter>
#include <cstring>

LvglWidget *LvglWidget::instance_ = nullptr;

LvglWidget::LvglWidget(QWidget *parent) : QWidget(parent) {
  instance_ = this;

  // Set fixed size matching UI design (480x320)
  setFixedSize(480, 320);
  setMouseTracking(true);

  // Initialize framebuffer
  framebuffer_ = QImage(480, 320, QImage::Format_RGB16);
  framebuffer_.fill(Qt::black);

  // Initialize LVGL
  initLvgl();

  // Create timer for LVGL tick (5ms = 200Hz)
  tick_timer_ = new QTimer(this);
  connect(tick_timer_, &QTimer::timeout, this, &LvglWidget::onTick);
  tick_timer_->start(5);

  qDebug() << "LvglWidget initialized successfully";
}

LvglWidget::~LvglWidget() {
  if (tick_timer_) {
    tick_timer_->stop();
  }
  instance_ = nullptr;
}

void LvglWidget::initLvgl() {
  lv_init();

  // Allocate draw buffers (480x40 pixels each, 1/8th of screen)
  size_t buf_size = 480 * 40;
  buf1_.resize(buf_size);
  buf2_.resize(buf_size);

  lv_disp_draw_buf_init(&disp_buf_, buf1_.data(), buf2_.data(), buf_size);

  // Register display driver
  lv_disp_drv_init(&disp_drv_);
  disp_drv_.hor_res = 480;
  disp_drv_.ver_res = 320;
  disp_drv_.flush_cb = displayFlushCb;
  disp_drv_.draw_buf = &disp_buf_;
  disp_drv_.user_data = this;
  lv_disp_drv_register(&disp_drv_);

  // Register input device (touchpad)
  lv_indev_drv_init(&indev_drv_);
  indev_drv_.type = LV_INDEV_TYPE_POINTER;
  indev_drv_.read_cb = touchpadReadCb;
  indev_drv_.user_data = this;
  lv_indev_drv_register(&indev_drv_);

  // Initialize UI
  ui_init();

  // Load Screen1 by default
  switchToScreen(SCREEN1);
}

void LvglWidget::displayFlushCb(lv_disp_drv_t *disp_drv, const lv_area_t *area,
                                lv_color_t *color_p) {
  LvglWidget *self = static_cast<LvglWidget *>(disp_drv->user_data);
  if (!self) {
    lv_disp_flush_ready(disp_drv);
    return;
  }

  // Copy LVGL buffer to QImage framebuffer
  int32_t w = area->x2 - area->x1 + 1;
  int32_t h = area->y2 - area->y1 + 1;

  uint16_t *fb_ptr = reinterpret_cast<uint16_t *>(self->framebuffer_.bits());
  lv_color_t *src_ptr = color_p;

  for (int32_t y = 0; y < h; y++) {
    int32_t fb_y = area->y1 + y;
    if (fb_y >= 0 && fb_y < 320) {
      uint16_t *fb_line = fb_ptr + (fb_y * 480) + area->x1;
      for (int32_t x = 0; x < w; x++) {
        if ((area->x1 + x) >= 0 && (area->x1 + x) < 480) {
          fb_line[x] = src_ptr->full;
        }
        src_ptr++;
      }
    } else {
      src_ptr += w;
    }
  }

  lv_disp_flush_ready(disp_drv);
  self->update(); // Trigger Qt repaint
}

void LvglWidget::touchpadReadCb(lv_indev_drv_t *indev_drv,
                                lv_indev_data_t *data) {
  LvglWidget *self = static_cast<LvglWidget *>(indev_drv->user_data);
  if (!self) {
    data->state = LV_INDEV_STATE_REL;
    return;
  }

  if (self->touch_pressed_) {
    data->state = LV_INDEV_STATE_PR;
    data->point.x = self->touch_x_;
    data->point.y = self->touch_y_;
  } else {
    data->state = LV_INDEV_STATE_REL;
  }
}

void LvglWidget::onTick() {
  // lv_tick_inc(5); // 5ms tick - Disabled because LV_TICK_CUSTOM is 1 in
  // lv_conf.h
  lv_timer_handler();
}

void LvglWidget::paintEvent(QPaintEvent *event) {
  Q_UNUSED(event);
  QPainter painter(this);
  painter.drawImage(0, 0, framebuffer_);
}

void LvglWidget::mousePressEvent(QMouseEvent *event) {
  touch_pressed_ = true;
  touch_x_ = event->pos().x();
  touch_y_ = event->pos().y();
}

void LvglWidget::mouseReleaseEvent(QMouseEvent *event) {
  Q_UNUSED(event);
  touch_pressed_ = false;
}

void LvglWidget::mouseMoveEvent(QMouseEvent *event) {
  if (touch_pressed_) {
    touch_x_ = event->pos().x();
    touch_y_ = event->pos().y();
  }
}

// ==================== Button Press Handlers  ====================

void LvglWidget::pressK1() {
  // K1: Screen switching
  switch (current_screen_) {
  case SCREEN1:
    switchToScreen(SCREEN2);
    break;
  case SCREEN2:
    switchToScreen(SCREEN1);
    break;
  case SCREEN3:
    switchToScreen(SCREEN2);
    break;
  case SCREEN4:
    switchToScreen(SCREEN2);
    break;
  }
}

void LvglWidget::pressK2() {
  // K2: Up / Panel1 cycle / Screen2 up
  switch (current_screen_) {
  case SCREEN1:
    panel1_index_ = (panel1_index_ + 1) % 7;
    updatePanel1();
    break;
  case SCREEN2:
    screen2_focus_ = (screen2_focus_ + 1) % 2;
    // Update button focus
    if (screen2_focus_ == 0) {
      lv_obj_add_state(ui_Label37, LV_STATE_FOCUSED);
      lv_obj_clear_state(ui_Label38, LV_STATE_FOCUSED);
    } else {
      lv_obj_clear_state(ui_Label37, LV_STATE_FOCUSED);
      lv_obj_add_state(ui_Label38, LV_STATE_FOCUSED);
    }
    break;
  default:
    break;
  }
}

void LvglWidget::pressK3() {
  // K3: Down / Panel8 cycle / Screen2 down
  switch (current_screen_) {
  case SCREEN1:
    panel8_index_ = (panel8_index_ + 1) % 7;
    updatePanel8();
    break;
  case SCREEN2:
    screen2_focus_ = (screen2_focus_ - 1 + 2) % 2;
    // Update button focus
    if (screen2_focus_ == 0) {
      lv_obj_add_state(ui_Label37, LV_STATE_FOCUSED);
      lv_obj_clear_state(ui_Label38, LV_STATE_FOCUSED);
    } else {
      lv_obj_clear_state(ui_Label37, LV_STATE_FOCUSED);
      lv_obj_add_state(ui_Label38, LV_STATE_FOCUSED);
    }
    break;
  default:
    break;
  }
}

void LvglWidget::pressK4() {
  // K4: Confirm / Training (simplified for simulator)
  if (current_screen_ == SCREEN2) {
    // Enter selected screen
    switchToScreen(screen2_focus_ == 0 ? SCREEN3 : SCREEN4);
  }
  // For SCREEN1, K4 would be long-press for training, skip in simulator
}

// ==================== Screen Switching ====================

void LvglWidget::switchToScreen(ScreenId screen) {
  current_screen_ = screen;

  switch (screen) {
  case SCREEN1:
    lv_scr_load_anim(ui_Screen1, LV_SCR_LOAD_ANIM_NONE, 0, 0, false);
    break;
  case SCREEN2:
    lv_scr_load_anim(ui_Screen2, LV_SCR_LOAD_ANIM_NONE, 0, 0, false);
    // Set focus state
    if (screen2_focus_ == 0) {
      lv_obj_add_state(ui_Label37, LV_STATE_FOCUSED);
      lv_obj_clear_state(ui_Label38, LV_STATE_FOCUSED);
    } else {
      lv_obj_clear_state(ui_Label37, LV_STATE_FOCUSED);
      lv_obj_add_state(ui_Label38, LV_STATE_FOCUSED);
    }
    break;
  case SCREEN3:
    lv_scr_load_anim(ui_Screen3, LV_SCR_LOAD_ANIM_NONE, 0, 0, false);
    break;
  case SCREEN4:
    lv_scr_load_anim(ui_Screen4, LV_SCR_LOAD_ANIM_NONE, 0, 0, false);
    break;
  }
}

// ==================== Panel Updates ====================

void LvglWidget::updatePanel1() {
  // Hide all first
  lv_obj_add_flag(ui_P1Timer, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(ui_P1Split, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(ui_P1Dist, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(ui_P1Stroke, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(ui_P1Speed, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(ui_P1Power, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(ui_P1HR, LV_OBJ_FLAG_HIDDEN);

  // Show selected
  switch (panel1_index_) {
  case 0:
    lv_obj_clear_flag(ui_P1Timer, LV_OBJ_FLAG_HIDDEN);
    break;
  case 1:
    lv_obj_clear_flag(ui_P1Split, LV_OBJ_FLAG_HIDDEN);
    break;
  case 2:
    lv_obj_clear_flag(ui_P1Dist, LV_OBJ_FLAG_HIDDEN);
    break;
  case 3:
    lv_obj_clear_flag(ui_P1Stroke, LV_OBJ_FLAG_HIDDEN);
    break;
  case 4:
    lv_obj_clear_flag(ui_P1Speed, LV_OBJ_FLAG_HIDDEN);
    break;
  case 5:
    lv_obj_clear_flag(ui_P1Power, LV_OBJ_FLAG_HIDDEN);
    break;
  case 6:
    lv_obj_clear_flag(ui_P1HR, LV_OBJ_FLAG_HIDDEN);
    break;
  }
}

void LvglWidget::updatePanel8() {
  // Hide all first
  lv_obj_add_flag(ui_P8HR, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(ui_P8Timer, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(ui_P8Split, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(ui_P8Dist, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(ui_P8Stroke, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(ui_P8Speed, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(ui_P8Power, LV_OBJ_FLAG_HIDDEN);

  // Show selected
  switch (panel8_index_) {
  case 0:
    lv_obj_clear_flag(ui_P8HR, LV_OBJ_FLAG_HIDDEN);
    break;
  case 1:
    lv_obj_clear_flag(ui_P8Timer, LV_OBJ_FLAG_HIDDEN);
    break;
  case 2:
    lv_obj_clear_flag(ui_P8Split, LV_OBJ_FLAG_HIDDEN);
    break;
  case 3:
    lv_obj_clear_flag(ui_P8Dist, LV_OBJ_FLAG_HIDDEN);
    break;
  case 4:
    lv_obj_clear_flag(ui_P8Stroke, LV_OBJ_FLAG_HIDDEN);
    break;
  case 5:
    lv_obj_clear_flag(ui_P8Speed, LV_OBJ_FLAG_HIDDEN);
    break;
  case 6:
    lv_obj_clear_flag(ui_P8Power, LV_OBJ_FLAG_HIDDEN);
    break;
  }
}

void LvglWidget::updateBoundData() {
  // Sync data between P1 and P8 panels
  const char *timerText = lv_label_get_text(ui_Label7);
  lv_label_set_text(ui_Label13, timerText);

  const char *splitText = lv_label_get_text(ui_Label12);
  lv_label_set_text(ui_Label56, splitText);

  const char *distText = lv_label_get_text(ui_Label23);
  lv_label_set_text(ui_Label60, distText);

  const char *strokeText = lv_label_get_text(ui_Label44);
  lv_label_set_text(ui_Label64, strokeText);

  const char *speedText = lv_label_get_text(ui_Label46);
  lv_label_set_text(ui_Label68, speedText);

  const char *powerText = lv_label_get_text(ui_Label48);
  lv_label_set_text(ui_Label72, powerText);
}

void LvglWidget::setTrainingActive(bool active) {
  if (ui_Image5) {
    if (active) {
      lv_obj_clear_flag(ui_Image5, LV_OBJ_FLAG_HIDDEN);
    } else {
      lv_obj_add_flag(ui_Image5, LV_OBJ_FLAG_HIDDEN);
    }
  }
}

// ==================== UI Update Methods ====================

void LvglWidget::updateStrokeRate(float rate) {
  char buf[16];
  snprintf(buf, sizeof(buf), "%.1f", rate);
  lv_label_set_text(ui_Label9, buf); // Main display
}

void LvglWidget::updateStrokeCount(int count) {
  char buf[16];
  snprintf(buf, sizeof(buf), "%d", count);
  lv_label_set_text(ui_Label44, buf); // P1Stroke
  updateBoundData();
}

void LvglWidget::updateStrokeLength(float dist_meters) {
  char buf[16];
  snprintf(buf, sizeof(buf), "%.1f", dist_meters);
  lv_label_set_text(ui_Label12, buf); // P1Split
  updateBoundData();
}

void LvglWidget::updateDistance(float dist_meters) {
  char buf[16];
  snprintf(buf, sizeof(buf), "%.3f", dist_meters / 1000.0f); // Convert to km
  lv_label_set_text(ui_Label23, buf);                        // P1Dist
  updateBoundData();
}

void LvglWidget::updateTimer(const QString &time_str) {
  lv_label_set_text(ui_Label7, time_str.toUtf8().constData()); // P1Timer
  updateBoundData();
}

void LvglWidget::updateClock(const QString &time_str) {
  // Top-right clock (Label10) on Screen1
  lv_label_set_text(ui_Label10, time_str.toUtf8().constData());
}

void LvglWidget::updateSpeed(float speed_mps) {
  char buf[16];
  snprintf(buf, sizeof(buf), "%.1f", speed_mps);
  lv_label_set_text(ui_Label46, buf); // P1Speed
  updateBoundData();
}

void LvglWidget::updatePace(const QString &pace_str) {
  lv_label_set_text(ui_Label8, pace_str.toUtf8().constData()); // Pace label
}
