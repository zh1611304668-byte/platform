#ifndef PLATFORM_DEFINES_H
#define PLATFORM_DEFINES_H

#include <QDateTime>
#include <QDebug>
#include <QString>
#include <algorithm>
#include <cmath>

// Shim for Arduino String class using Qt's QString
class String : public QString {
public:
  using QString::QString;
  String() : QString() {}
  String(const QString &other) : QString(other) {}
  String(const char *str) : QString(str) {}
  String(int value) : QString(QString::number(value)) {}
  String(float value, int prec = 2)
      : QString(QString::number(value, 'f', prec)) {}
  String(double value, int prec = 2)
      : QString(QString::number(value, 'f', prec)) {}

  // Arduino String: substring(from, to_exclusive)
  String substring(int from, int to = -1) const {
    if (to == -1)
      return this->mid(from);
    return this->mid(from, to - from);
  }

  // Qt doesn't have equalsIgnoreCase, but has compare
  bool equalsIgnoreCase(const String &other) const {
    return this->compare(other, Qt::CaseInsensitive) == 0;
  }

  // charAt
  QChar charAt(int index) const {
    if (index < 0 || index >= length())
      return QChar();
    return this->at(index);
  }

  void remove(int index, int count) { QString::remove(index, count); }
};

#include <iostream>

class SerialMock {
public:
  template <typename T> void print(const T &val) { std::cout << val; }
  // Overload for our custom String class
  void print(const String &val) { std::cout << val.toStdString(); }

  template <typename T> void println(const T &val) {
    print(val);
    std::cout << std::endl;
  }
  void println() { std::cout << std::endl; }
};
static SerialMock Serial;

// Global millis() implementation
inline unsigned long millis() { return QDateTime::currentMSecsSinceEpoch(); }

#endif
