/****************************************************************************
** Meta object code from reading C++ file 'mainwindow.h'
**
** Created by: The Qt Meta Object Compiler version 67 (Qt 5.15.2)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include <memory>
#include "../../../mainwindow.h"
#include <QtCore/qbytearray.h>
#include <QtCore/qmetatype.h>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'mainwindow.h' doesn't include <QObject>."
#elif Q_MOC_OUTPUT_REVISION != 67
#error "This file was generated using the moc from 5.15.2. It"
#error "cannot be used with the include files from this version of Qt."
#error "(The moc has changed too much.)"
#endif

QT_BEGIN_MOC_NAMESPACE
QT_WARNING_PUSH
QT_WARNING_DISABLE_DEPRECATED
struct qt_meta_stringdata_MainWindow_t {
    QByteArrayData data[34];
    char stringdata0[379];
};
#define QT_MOC_LITERAL(idx, ofs, len) \
    Q_STATIC_BYTE_ARRAY_DATA_HEADER_INITIALIZER_WITH_OFFSET(len, \
    qptrdiff(offsetof(qt_meta_stringdata_MainWindow_t, stringdata0) + ofs \
        - idx * sizeof(QByteArrayData)) \
    )
static const qt_meta_stringdata_MainWindow_t qt_meta_stringdata_MainWindow = {
    {
QT_MOC_LITERAL(0, 0, 10), // "MainWindow"
QT_MOC_LITERAL(1, 11, 9), // "onLoadCsv"
QT_MOC_LITERAL(2, 21, 0), // ""
QT_MOC_LITERAL(3, 22, 10), // "onLoadGnss"
QT_MOC_LITERAL(4, 33, 12), // "onTogglePlay"
QT_MOC_LITERAL(5, 46, 14), // "onSpeedChanged"
QT_MOC_LITERAL(6, 61, 4), // "text"
QT_MOC_LITERAL(7, 66, 13), // "onAxisChanged"
QT_MOC_LITERAL(8, 80, 2), // "id"
QT_MOC_LITERAL(9, 83, 20), // "onSimulationFinished"
QT_MOC_LITERAL(10, 104, 13), // "onGnssUpdated"
QT_MOC_LITERAL(11, 118, 9), // "speed_mps"
QT_MOC_LITERAL(12, 128, 3), // "lat"
QT_MOC_LITERAL(13, 132, 3), // "lon"
QT_MOC_LITERAL(14, 136, 4), // "sats"
QT_MOC_LITERAL(15, 141, 4), // "pace"
QT_MOC_LITERAL(16, 146, 16), // "onSimTimeUpdated"
QT_MOC_LITERAL(17, 163, 11), // "sim_time_ms"
QT_MOC_LITERAL(18, 175, 16), // "onStrokeDetected"
QT_MOC_LITERAL(19, 192, 11), // "StrokeEvent"
QT_MOC_LITERAL(20, 204, 5), // "event"
QT_MOC_LITERAL(21, 210, 8), // "updateUi"
QT_MOC_LITERAL(22, 219, 12), // "onSavePreset"
QT_MOC_LITERAL(23, 232, 12), // "onLoadPreset"
QT_MOC_LITERAL(24, 245, 15), // "onPresetChanged"
QT_MOC_LITERAL(25, 261, 4), // "name"
QT_MOC_LITERAL(26, 266, 11), // "onK1Pressed"
QT_MOC_LITERAL(27, 278, 11), // "onK2Pressed"
QT_MOC_LITERAL(28, 290, 11), // "onK3Pressed"
QT_MOC_LITERAL(29, 302, 11), // "onK4Pressed"
QT_MOC_LITERAL(30, 314, 12), // "onK4Released"
QT_MOC_LITERAL(31, 327, 17), // "onTrainingStarted"
QT_MOC_LITERAL(32, 345, 17), // "onTrainingStopped"
QT_MOC_LITERAL(33, 363, 15) // "onExportStrokes"

    },
    "MainWindow\0onLoadCsv\0\0onLoadGnss\0"
    "onTogglePlay\0onSpeedChanged\0text\0"
    "onAxisChanged\0id\0onSimulationFinished\0"
    "onGnssUpdated\0speed_mps\0lat\0lon\0sats\0"
    "pace\0onSimTimeUpdated\0sim_time_ms\0"
    "onStrokeDetected\0StrokeEvent\0event\0"
    "updateUi\0onSavePreset\0onLoadPreset\0"
    "onPresetChanged\0name\0onK1Pressed\0"
    "onK2Pressed\0onK3Pressed\0onK4Pressed\0"
    "onK4Released\0onTrainingStarted\0"
    "onTrainingStopped\0onExportStrokes"
};
#undef QT_MOC_LITERAL

static const uint qt_meta_data_MainWindow[] = {

 // content:
       8,       // revision
       0,       // classname
       0,    0, // classinfo
      21,   14, // methods
       0,    0, // properties
       0,    0, // enums/sets
       0,    0, // constructors
       0,       // flags
       0,       // signalCount

 // slots: name, argc, parameters, tag, flags
       1,    0,  119,    2, 0x08 /* Private */,
       3,    0,  120,    2, 0x08 /* Private */,
       4,    0,  121,    2, 0x08 /* Private */,
       5,    1,  122,    2, 0x08 /* Private */,
       7,    1,  125,    2, 0x08 /* Private */,
       9,    0,  128,    2, 0x08 /* Private */,
      10,    5,  129,    2, 0x08 /* Private */,
      16,    1,  140,    2, 0x08 /* Private */,
      18,    1,  143,    2, 0x08 /* Private */,
      21,    0,  146,    2, 0x08 /* Private */,
      22,    0,  147,    2, 0x08 /* Private */,
      23,    0,  148,    2, 0x08 /* Private */,
      24,    1,  149,    2, 0x08 /* Private */,
      26,    0,  152,    2, 0x08 /* Private */,
      27,    0,  153,    2, 0x08 /* Private */,
      28,    0,  154,    2, 0x08 /* Private */,
      29,    0,  155,    2, 0x08 /* Private */,
      30,    0,  156,    2, 0x08 /* Private */,
      31,    0,  157,    2, 0x08 /* Private */,
      32,    0,  158,    2, 0x08 /* Private */,
      33,    0,  159,    2, 0x08 /* Private */,

 // slots: parameters
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void, QMetaType::QString,    6,
    QMetaType::Void, QMetaType::Int,    8,
    QMetaType::Void,
    QMetaType::Void, QMetaType::Double, QMetaType::Double, QMetaType::Double, QMetaType::Int, QMetaType::QString,   11,   12,   13,   14,   15,
    QMetaType::Void, QMetaType::LongLong,   17,
    QMetaType::Void, 0x80000000 | 19,   20,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void, QMetaType::QString,   25,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,

       0        // eod
};

void MainWindow::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    if (_c == QMetaObject::InvokeMetaMethod) {
        auto *_t = static_cast<MainWindow *>(_o);
        Q_UNUSED(_t)
        switch (_id) {
        case 0: _t->onLoadCsv(); break;
        case 1: _t->onLoadGnss(); break;
        case 2: _t->onTogglePlay(); break;
        case 3: _t->onSpeedChanged((*reinterpret_cast< const QString(*)>(_a[1]))); break;
        case 4: _t->onAxisChanged((*reinterpret_cast< int(*)>(_a[1]))); break;
        case 5: _t->onSimulationFinished(); break;
        case 6: _t->onGnssUpdated((*reinterpret_cast< double(*)>(_a[1])),(*reinterpret_cast< double(*)>(_a[2])),(*reinterpret_cast< double(*)>(_a[3])),(*reinterpret_cast< int(*)>(_a[4])),(*reinterpret_cast< QString(*)>(_a[5]))); break;
        case 7: _t->onSimTimeUpdated((*reinterpret_cast< qint64(*)>(_a[1]))); break;
        case 8: _t->onStrokeDetected((*reinterpret_cast< const StrokeEvent(*)>(_a[1]))); break;
        case 9: _t->updateUi(); break;
        case 10: _t->onSavePreset(); break;
        case 11: _t->onLoadPreset(); break;
        case 12: _t->onPresetChanged((*reinterpret_cast< const QString(*)>(_a[1]))); break;
        case 13: _t->onK1Pressed(); break;
        case 14: _t->onK2Pressed(); break;
        case 15: _t->onK3Pressed(); break;
        case 16: _t->onK4Pressed(); break;
        case 17: _t->onK4Released(); break;
        case 18: _t->onTrainingStarted(); break;
        case 19: _t->onTrainingStopped(); break;
        case 20: _t->onExportStrokes(); break;
        default: ;
        }
    }
}

QT_INIT_METAOBJECT const QMetaObject MainWindow::staticMetaObject = { {
    QMetaObject::SuperData::link<QMainWindow::staticMetaObject>(),
    qt_meta_stringdata_MainWindow.data,
    qt_meta_data_MainWindow,
    qt_static_metacall,
    nullptr,
    nullptr
} };


const QMetaObject *MainWindow::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *MainWindow::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_meta_stringdata_MainWindow.stringdata0))
        return static_cast<void*>(this);
    return QMainWindow::qt_metacast(_clname);
}

int MainWindow::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QMainWindow::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 21)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 21;
    } else if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 21)
            *reinterpret_cast<int*>(_a[0]) = -1;
        _id -= 21;
    }
    return _id;
}
QT_WARNING_POP
QT_END_MOC_NAMESPACE
