/****************************************************************************
** Meta object code from reading C++ file 'simulation_worker.h'
**
** Created by: The Qt Meta Object Compiler version 67 (Qt 5.15.2)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include <memory>
#include "../../../simulation_worker.h"
#include <QtCore/qbytearray.h>
#include <QtCore/qmetatype.h>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'simulation_worker.h' doesn't include <QObject>."
#elif Q_MOC_OUTPUT_REVISION != 67
#error "This file was generated using the moc from 5.15.2. It"
#error "cannot be used with the include files from this version of Qt."
#error "(The moc has changed too much.)"
#endif

QT_BEGIN_MOC_NAMESPACE
QT_WARNING_PUSH
QT_WARNING_DISABLE_DEPRECATED
struct qt_meta_stringdata_SimulationWorker_t {
    QByteArrayData data[16];
    char stringdata0[144];
};
#define QT_MOC_LITERAL(idx, ofs, len) \
    Q_STATIC_BYTE_ARRAY_DATA_HEADER_INITIALIZER_WITH_OFFSET(len, \
    qptrdiff(offsetof(qt_meta_stringdata_SimulationWorker_t, stringdata0) + ofs \
        - idx * sizeof(QByteArrayData)) \
    )
static const qt_meta_stringdata_SimulationWorker_t qt_meta_stringdata_SimulationWorker = {
    {
QT_MOC_LITERAL(0, 0, 16), // "SimulationWorker"
QT_MOC_LITERAL(1, 17, 12), // "frameUpdated"
QT_MOC_LITERAL(2, 30, 0), // ""
QT_MOC_LITERAL(3, 31, 14), // "simTimeUpdated"
QT_MOC_LITERAL(4, 46, 11), // "sim_time_ms"
QT_MOC_LITERAL(5, 58, 11), // "gnssUpdated"
QT_MOC_LITERAL(6, 70, 9), // "speed_mps"
QT_MOC_LITERAL(7, 80, 3), // "lat"
QT_MOC_LITERAL(8, 84, 3), // "lon"
QT_MOC_LITERAL(9, 88, 4), // "sats"
QT_MOC_LITERAL(10, 93, 4), // "pace"
QT_MOC_LITERAL(11, 98, 14), // "strokeDetected"
QT_MOC_LITERAL(12, 113, 11), // "StrokeEvent"
QT_MOC_LITERAL(13, 125, 5), // "event"
QT_MOC_LITERAL(14, 131, 8), // "finished"
QT_MOC_LITERAL(15, 140, 3) // "run"

    },
    "SimulationWorker\0frameUpdated\0\0"
    "simTimeUpdated\0sim_time_ms\0gnssUpdated\0"
    "speed_mps\0lat\0lon\0sats\0pace\0strokeDetected\0"
    "StrokeEvent\0event\0finished\0run"
};
#undef QT_MOC_LITERAL

static const uint qt_meta_data_SimulationWorker[] = {

 // content:
       8,       // revision
       0,       // classname
       0,    0, // classinfo
       6,   14, // methods
       0,    0, // properties
       0,    0, // enums/sets
       0,    0, // constructors
       0,       // flags
       5,       // signalCount

 // signals: name, argc, parameters, tag, flags
       1,    0,   44,    2, 0x06 /* Public */,
       3,    1,   45,    2, 0x06 /* Public */,
       5,    5,   48,    2, 0x06 /* Public */,
      11,    1,   59,    2, 0x06 /* Public */,
      14,    0,   62,    2, 0x06 /* Public */,

 // slots: name, argc, parameters, tag, flags
      15,    0,   63,    2, 0x0a /* Public */,

 // signals: parameters
    QMetaType::Void,
    QMetaType::Void, QMetaType::LongLong,    4,
    QMetaType::Void, QMetaType::Double, QMetaType::Double, QMetaType::Double, QMetaType::Int, QMetaType::QString,    6,    7,    8,    9,   10,
    QMetaType::Void, 0x80000000 | 12,   13,
    QMetaType::Void,

 // slots: parameters
    QMetaType::Void,

       0        // eod
};

void SimulationWorker::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    if (_c == QMetaObject::InvokeMetaMethod) {
        auto *_t = static_cast<SimulationWorker *>(_o);
        Q_UNUSED(_t)
        switch (_id) {
        case 0: _t->frameUpdated(); break;
        case 1: _t->simTimeUpdated((*reinterpret_cast< qint64(*)>(_a[1]))); break;
        case 2: _t->gnssUpdated((*reinterpret_cast< double(*)>(_a[1])),(*reinterpret_cast< double(*)>(_a[2])),(*reinterpret_cast< double(*)>(_a[3])),(*reinterpret_cast< int(*)>(_a[4])),(*reinterpret_cast< QString(*)>(_a[5]))); break;
        case 3: _t->strokeDetected((*reinterpret_cast< const StrokeEvent(*)>(_a[1]))); break;
        case 4: _t->finished(); break;
        case 5: _t->run(); break;
        default: ;
        }
    } else if (_c == QMetaObject::IndexOfMethod) {
        int *result = reinterpret_cast<int *>(_a[0]);
        {
            using _t = void (SimulationWorker::*)();
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&SimulationWorker::frameUpdated)) {
                *result = 0;
                return;
            }
        }
        {
            using _t = void (SimulationWorker::*)(qint64 );
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&SimulationWorker::simTimeUpdated)) {
                *result = 1;
                return;
            }
        }
        {
            using _t = void (SimulationWorker::*)(double , double , double , int , QString );
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&SimulationWorker::gnssUpdated)) {
                *result = 2;
                return;
            }
        }
        {
            using _t = void (SimulationWorker::*)(const StrokeEvent & );
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&SimulationWorker::strokeDetected)) {
                *result = 3;
                return;
            }
        }
        {
            using _t = void (SimulationWorker::*)();
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&SimulationWorker::finished)) {
                *result = 4;
                return;
            }
        }
    }
}

QT_INIT_METAOBJECT const QMetaObject SimulationWorker::staticMetaObject = { {
    QMetaObject::SuperData::link<QObject::staticMetaObject>(),
    qt_meta_stringdata_SimulationWorker.data,
    qt_meta_data_SimulationWorker,
    qt_static_metacall,
    nullptr,
    nullptr
} };


const QMetaObject *SimulationWorker::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *SimulationWorker::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_meta_stringdata_SimulationWorker.stringdata0))
        return static_cast<void*>(this);
    return QObject::qt_metacast(_clname);
}

int SimulationWorker::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QObject::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 6)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 6;
    } else if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 6)
            *reinterpret_cast<int*>(_a[0]) = -1;
        _id -= 6;
    }
    return _id;
}

// SIGNAL 0
void SimulationWorker::frameUpdated()
{
    QMetaObject::activate(this, &staticMetaObject, 0, nullptr);
}

// SIGNAL 1
void SimulationWorker::simTimeUpdated(qint64 _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))) };
    QMetaObject::activate(this, &staticMetaObject, 1, _a);
}

// SIGNAL 2
void SimulationWorker::gnssUpdated(double _t1, double _t2, double _t3, int _t4, QString _t5)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))), const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t2))), const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t3))), const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t4))), const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t5))) };
    QMetaObject::activate(this, &staticMetaObject, 2, _a);
}

// SIGNAL 3
void SimulationWorker::strokeDetected(const StrokeEvent & _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))) };
    QMetaObject::activate(this, &staticMetaObject, 3, _a);
}

// SIGNAL 4
void SimulationWorker::finished()
{
    QMetaObject::activate(this, &staticMetaObject, 4, nullptr);
}
QT_WARNING_POP
QT_END_MOC_NAMESPACE
