#pragma once

#include <QImage>
#include <QMouseEvent>
#include <QTimer>
#include <QWidget>
#include <vector>

extern "C" {
#include "lvgl/lvgl.h"
#include "ui/ui.h"
}

enum ScreenId {
  SCREEN1 = 0, // Motion/Training screen
  SCREEN2 = 1, // Menu screen
  SCREEN3 = 2, // Bluetooth devices
  SCREEN4 = 3  // Settings
};

class LvglWidget : public QWidget {
  Q_OBJECT

public:
  explicit LvglWidget(QWidget *parent = nullptr);
  ~LvglWidget() override;

  // Button interface (K1-K4)
  void pressK1(); // Screen switching
  void pressK2(); // Up/Panel1 cycle
  void pressK3(); // Down/Panel8 cycle
  void pressK4(); // Confirm/Training

  // UI update interface
  void updateStrokeRate(float rate);
  void updateStrokeCount(int count);
  void updateStrokeLength(float dist_meters);
  void updateDistance(float dist_meters);
  void updateTimer(const QString &time_str);
  void updateClock(const QString &time_str); // Screen1 top-right clock (Label10)
  void updateSpeed(float speed_mps);
  void updatePace(const QString &pace_str);
  void setTrainingActive(bool active);

protected:
  void paintEvent(QPaintEvent *event) override;
  void mousePressEvent(QMouseEvent *event) override;
  void mouseReleaseEvent(QMouseEvent *event) override;
  void mouseMoveEvent(QMouseEvent *event) override;

private slots:
  void onTick();

private:
  void initLvgl();
  void switchToScreen(ScreenId screen);
  void updatePanel1();
  void updatePanel8();
  void updateBoundData();

  static void displayFlushCb(lv_disp_drv_t *disp_drv, const lv_area_t *area,
                             lv_color_t *color_p);
  static void touchpadReadCb(lv_indev_drv_t *indev_drv, lv_indev_data_t *data);

  QTimer *tick_timer_;
  lv_disp_draw_buf_t disp_buf_;
  lv_disp_drv_t disp_drv_;
  lv_indev_drv_t indev_drv_;

  std::vector<lv_color_t> buf1_;
  std::vector<lv_color_t> buf2_;
  QImage framebuffer_;

  ScreenId current_screen_ = SCREEN1;
  int panel1_index_ = 0;  // 0-6: Timer/Split/Dist/Stroke/Speed/Power/HR
  int panel8_index_ = 0;  // 0-6: HR/Timer/Split/Dist/Stroke/Speed/Power
  int screen2_focus_ = 0; // 0=Button3(BT), 1=Button5(Settings)

  bool touch_pressed_ = false;
  lv_coord_t touch_x_ = 0;
  lv_coord_t touch_y_ = 0;

  static LvglWidget *instance_;
};
