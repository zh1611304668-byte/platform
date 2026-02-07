/****************************************************************************
** Meta object code from reading C++ file 'simulation_worker.h'
**
** Created by: The Qt Meta Object Compiler version 69 (Qt 6.10.2)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include "../../../../simulation_worker.h"
#include <QtCore/qmetatype.h>

#include <QtCore/qtmochelpers.h>

#include <memory>


#include <QtCore/qxptype_traits.h>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'simulation_worker.h' doesn't include <QObject>."
#elif Q_MOC_OUTPUT_REVISION != 69
#error "This file was generated using the moc from 6.10.2. It"
#error "cannot be used with the include files from this version of Qt."
#error "(The moc has changed too much.)"
#endif

#ifndef Q_CONSTINIT
#define Q_CONSTINIT
#endif

QT_WARNING_PUSH
QT_WARNING_DISABLE_DEPRECATED
QT_WARNING_DISABLE_GCC("-Wuseless-cast")
namespace {
struct qt_meta_tag_ZN16SimulationWorkerE_t {};
} // unnamed namespace

template <> constexpr inline auto SimulationWorker::qt_create_metaobjectdata<qt_meta_tag_ZN16SimulationWorkerE_t>()
{
    namespace QMC = QtMocConstants;
    QtMocHelpers::StringRefStorage qt_stringData {
        "SimulationWorker",
        "frameUpdated",
        "",
        "simTimeUpdated",
        "sim_time_ms",
        "gnssUpdated",
        "speed_mps",
        "lat",
        "lon",
        "sats",
        "pace",
        "hdop",
        "fix",
        "diff_age",
        "timeSyncUpdated",
        "base_time_ms",
        "gnss_offset_ms",
        "imu_mean_dt",
        "imu_min_dt",
        "imu_max_dt",
        "imu_std_dt",
        "gnss_mean_dt",
        "gnss_min_dt",
        "gnss_max_dt",
        "gnss_std_dt",
        "strokeDetected",
        "StrokeEvent",
        "event",
        "finished",
        "run"
    };

    QtMocHelpers::UintData qt_methods {
        // Signal 'frameUpdated'
        QtMocHelpers::SignalData<void()>(1, 2, QMC::AccessPublic, QMetaType::Void),
        // Signal 'simTimeUpdated'
        QtMocHelpers::SignalData<void(qint64)>(3, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::LongLong, 4 },
        }}),
        // Signal 'gnssUpdated'
        QtMocHelpers::SignalData<void(double, double, double, int, QString, const QString &, const QString &, const QString &)>(5, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Double, 6 }, { QMetaType::Double, 7 }, { QMetaType::Double, 8 }, { QMetaType::Int, 9 },
            { QMetaType::QString, 10 }, { QMetaType::QString, 11 }, { QMetaType::QString, 12 }, { QMetaType::QString, 13 },
        }}),
        // Signal 'timeSyncUpdated'
        QtMocHelpers::SignalData<void(double, double, double, double, double, double, double, double, double, double)>(14, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Double, 15 }, { QMetaType::Double, 16 }, { QMetaType::Double, 17 }, { QMetaType::Double, 18 },
            { QMetaType::Double, 19 }, { QMetaType::Double, 20 }, { QMetaType::Double, 21 }, { QMetaType::Double, 22 },
            { QMetaType::Double, 23 }, { QMetaType::Double, 24 },
        }}),
        // Signal 'strokeDetected'
        QtMocHelpers::SignalData<void(const StrokeEvent &)>(25, 2, QMC::AccessPublic, QMetaType::Void, {{
            { 0x80000000 | 26, 27 },
        }}),
        // Signal 'finished'
        QtMocHelpers::SignalData<void()>(28, 2, QMC::AccessPublic, QMetaType::Void),
        // Slot 'run'
        QtMocHelpers::SlotData<void()>(29, 2, QMC::AccessPublic, QMetaType::Void),
    };
    QtMocHelpers::UintData qt_properties {
    };
    QtMocHelpers::UintData qt_enums {
    };
    return QtMocHelpers::metaObjectData<SimulationWorker, qt_meta_tag_ZN16SimulationWorkerE_t>(QMC::MetaObjectFlag{}, qt_stringData,
            qt_methods, qt_properties, qt_enums);
}
Q_CONSTINIT const QMetaObject SimulationWorker::staticMetaObject = { {
    QMetaObject::SuperData::link<QObject::staticMetaObject>(),
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN16SimulationWorkerE_t>.stringdata,
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN16SimulationWorkerE_t>.data,
    qt_static_metacall,
    nullptr,
    qt_staticMetaObjectRelocatingContent<qt_meta_tag_ZN16SimulationWorkerE_t>.metaTypes,
    nullptr
} };

void SimulationWorker::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    auto *_t = static_cast<SimulationWorker *>(_o);
    if (_c == QMetaObject::InvokeMetaMethod) {
        switch (_id) {
        case 0: _t->frameUpdated(); break;
        case 1: _t->simTimeUpdated((*reinterpret_cast<std::add_pointer_t<qint64>>(_a[1]))); break;
        case 2: _t->gnssUpdated((*reinterpret_cast<std::add_pointer_t<double>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<double>>(_a[2])),(*reinterpret_cast<std::add_pointer_t<double>>(_a[3])),(*reinterpret_cast<std::add_pointer_t<int>>(_a[4])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[5])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[6])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[7])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[8]))); break;
        case 3: _t->timeSyncUpdated((*reinterpret_cast<std::add_pointer_t<double>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<double>>(_a[2])),(*reinterpret_cast<std::add_pointer_t<double>>(_a[3])),(*reinterpret_cast<std::add_pointer_t<double>>(_a[4])),(*reinterpret_cast<std::add_pointer_t<double>>(_a[5])),(*reinterpret_cast<std::add_pointer_t<double>>(_a[6])),(*reinterpret_cast<std::add_pointer_t<double>>(_a[7])),(*reinterpret_cast<std::add_pointer_t<double>>(_a[8])),(*reinterpret_cast<std::add_pointer_t<double>>(_a[9])),(*reinterpret_cast<std::add_pointer_t<double>>(_a[10]))); break;
        case 4: _t->strokeDetected((*reinterpret_cast<std::add_pointer_t<StrokeEvent>>(_a[1]))); break;
        case 5: _t->finished(); break;
        case 6: _t->run(); break;
        default: ;
        }
    }
    if (_c == QMetaObject::IndexOfMethod) {
        if (QtMocHelpers::indexOfMethod<void (SimulationWorker::*)()>(_a, &SimulationWorker::frameUpdated, 0))
            return;
        if (QtMocHelpers::indexOfMethod<void (SimulationWorker::*)(qint64 )>(_a, &SimulationWorker::simTimeUpdated, 1))
            return;
        if (QtMocHelpers::indexOfMethod<void (SimulationWorker::*)(double , double , double , int , QString , const QString & , const QString & , const QString & )>(_a, &SimulationWorker::gnssUpdated, 2))
            return;
        if (QtMocHelpers::indexOfMethod<void (SimulationWorker::*)(double , double , double , double , double , double , double , double , double , double )>(_a, &SimulationWorker::timeSyncUpdated, 3))
            return;
        if (QtMocHelpers::indexOfMethod<void (SimulationWorker::*)(const StrokeEvent & )>(_a, &SimulationWorker::strokeDetected, 4))
            return;
        if (QtMocHelpers::indexOfMethod<void (SimulationWorker::*)()>(_a, &SimulationWorker::finished, 5))
            return;
    }
}

const QMetaObject *SimulationWorker::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *SimulationWorker::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_staticMetaObjectStaticContent<qt_meta_tag_ZN16SimulationWorkerE_t>.strings))
        return static_cast<void*>(this);
    return QObject::qt_metacast(_clname);
}

int SimulationWorker::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QObject::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 7)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 7;
    }
    if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 7)
            *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType();
        _id -= 7;
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
    QMetaObject::activate<void>(this, &staticMetaObject, 1, nullptr, _t1);
}

// SIGNAL 2
void SimulationWorker::gnssUpdated(double _t1, double _t2, double _t3, int _t4, QString _t5, const QString & _t6, const QString & _t7, const QString & _t8)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 2, nullptr, _t1, _t2, _t3, _t4, _t5, _t6, _t7, _t8);
}

// SIGNAL 3
void SimulationWorker::timeSyncUpdated(double _t1, double _t2, double _t3, double _t4, double _t5, double _t6, double _t7, double _t8, double _t9, double _t10)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 3, nullptr, _t1, _t2, _t3, _t4, _t5, _t6, _t7, _t8, _t9, _t10);
}

// SIGNAL 4
void SimulationWorker::strokeDetected(const StrokeEvent & _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 4, nullptr, _t1);
}

// SIGNAL 5
void SimulationWorker::finished()
{
    QMetaObject::activate(this, &staticMetaObject, 5, nullptr);
}
QT_WARNING_POP
