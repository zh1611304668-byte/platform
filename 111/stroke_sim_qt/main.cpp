#include <QApplication>

#include "mainwindow.h"

int main(int argc, char **argv) {
  QApplication app(argc, argv);
  MainWindow w;
  w.show();
  return app.exec();
}

extern "C" uint32_t millis_c_shim(void) {
  return static_cast<uint32_t>(QDateTime::currentMSecsSinceEpoch());
}
