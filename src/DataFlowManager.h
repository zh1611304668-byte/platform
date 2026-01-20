#ifndef DATAFLOWMANAGER_H
#define DATAFLOWMANAGER_H

#include <Arduino.h>
#include <queue>
#include <vector>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

// RAII锁管理类 - 自动管理互斥锁生命周期
class LockGuard {
private:
    SemaphoreHandle_t& mutex;
    bool locked;
    
public:
    explicit LockGuard(SemaphoreHandle_t& m) : mutex(m), locked(false) {
        // 检查信号量是否为NULL，防止断言失败
        if (mutex != nullptr && xSemaphoreTake(mutex, portMAX_DELAY) == pdTRUE) {
            locked = true;
        }
    }
    
    ~LockGuard() {
        if (locked) {
            xSemaphoreGive(mutex);
        }
    }
    
    // 禁止拷贝和赋值
    LockGuard(const LockGuard&) = delete;
    LockGuard& operator=(const LockGuard&) = delete;
};

// 数据类型枚举
enum DataType {
    DATA_SENSOR,        // 传感器数据
    DATA_TRAINING,      // 训练数据
    DATA_HEARTRATE,     // 心率数据
    DATA_STATUS,        // 状态数据
    DATA_CONFIG         // 配置数据
    // DATA_STROKE_EVENT 已移除 - 划桨事件现由StrokeDataManager独立管理
};

// 数据优先级
enum DataPriority {
    PRIORITY_LOW = 0,
    PRIORITY_NORMAL = 1,
    PRIORITY_HIGH = 2
};

// 数据包结构
struct DataPacket {
    DataType type;
    DataPriority priority;
    unsigned long timestamp;
    String payload;
    String topic;
    bool needsUpload;
    int retryCount;
    
    DataPacket() : type(DATA_SENSOR), priority(PRIORITY_NORMAL), 
                   timestamp(0), needsUpload(false), retryCount(0) {}
    
    DataPacket(DataType t, const String& data, const String& topicName = "", 
               DataPriority p = PRIORITY_NORMAL, bool upload = true) 
        : type(t), priority(p), timestamp(millis()), payload(data), 
          topic(topicName), needsUpload(upload), retryCount(0) {}
};

// 数据流管理器
class DataFlowManager {
private:
    // 数据队列
    std::queue<DataPacket> sensorQueue;      // 传感器数据队列
    std::queue<DataPacket> uploadQueue;      // 上传队列
    std::queue<DataPacket> configQueue;      // 配置下发队列
    
    // 互斥锁
    SemaphoreHandle_t sensorMutex;
    SemaphoreHandle_t uploadMutex;
    SemaphoreHandle_t configMutex;
    
    // 初始化状态标志
    bool isInitializing = false;  // 防止并发访问未完成初始化的对象
    
    // 数据缓存机制
    struct DataCache {
        String statusPayload;
        String trainingPayload;
        String heartRatePayload;
        unsigned long statusTime = 0;
        unsigned long trainingTime = 0;
        unsigned long heartRateTime = 0;
        static const unsigned long CACHE_VALID_MS = 500;  // 缓存有效期500ms
    } dataCache;
    
    // 统计信息
    struct Statistics {
        unsigned long totalProcessed;
        unsigned long totalUploaded;
        unsigned long uploadFailures;
        unsigned long lastResetTime;
    } stats;
    
    // 配置参数
    bool networkAvailable;
    unsigned long maxQueueSize;
    
public:
    DataFlowManager();
    ~DataFlowManager();
    
    // 初始化
    bool begin();
    
    // 数据输入接口
    bool pushSensorData(const String& data, DataType type = DATA_SENSOR);
    bool pushTrainingData(const String& data, const String& topic);
    bool pushHeartRateData(const String& data, const String& topic);
    bool pushStatusData(const String& data, const String& topic);
    // pushStrokeEvent 已移除 - 划桨事件现由StrokeDataManager独立管理
    
    // 统一数据收集接口 - 减少重复调用
    struct CollectionResult {
        bool hasStatusData = false;
        bool hasTrainingData = false; 
        bool hasHeartRateData = false;
        String statusPayload;
        String trainingPayload;
        String heartRatePayload;
        unsigned long collectionTime = 0;
        
        // 数据缓存标志
        bool fromCache = false;
        unsigned long cacheAge = 0;  // 缓存年龄（毫秒）
    };
    
    bool collectAllCurrentData(CollectionResult& result);
    
    // 数据输出接口
    bool popUploadData(DataPacket& packet);
    bool popConfigData(DataPacket& packet);
    
    // 配置下发接口
    bool pushConfigUpdate(const String& configData, DataType configType);
    
    // 网络状态管理
    void setNetworkStatus(bool available);
    bool isNetworkAvailable() const { return networkAvailable; }
    
    // 数据流暂停/恢复支持
    void pauseDataFlow();   // 暂停数据流（停止上传，保留队列）
    void resumeDataFlow();  // 恢复数据流
    bool isDataFlowPaused() const { return dataFlowPaused; }
    bool isInitialized() const { return sensorMutex != nullptr && uploadMutex != nullptr && configMutex != nullptr && !isInitializing; }  // 检查所有互斥量都已初始化且不在初始化过程中
    
private:
    bool dataFlowPaused = false;  // 数据流暂停标志
public:
    
    // 队列状态
    size_t getSensorQueueSize() const;
    size_t getUploadQueueSize() const;
    size_t getMaxQueueSize() const { return maxQueueSize; }
    
    // 统计信息
    void getStatistics(Statistics& outStats) const;
    void resetStatistics();
    
    // 统计更新方法（用于MQTT发布统计）
    void incrementUploadCount();
    void incrementFailureCount();
    void incrementProcessedCount();
    
    // 数据处理任务
    static void dataProcessTask(void* pvParameters);
    
    // 优先级处理
    void processHighPriorityData();
    
    // 数据验证
    bool validateDataPacket(const DataPacket& packet);
    
private:
    // 内部处理方法
    void processDataPacket(const DataPacket& packet);
    void cleanupQueues();
    bool isQueueFull(const std::queue<DataPacket>& queue) const;
    
    // 数据转换
    String formatSensorData(const String& rawData, DataType type);
    String formatUploadData(const DataPacket& packet);
};

// 全局实例
extern DataFlowManager dataFlow;

// 数据流状态枚举
enum DataFlowState {
    FLOW_IDLE,
    FLOW_COLLECTING,
    FLOW_PROCESSING,
    FLOW_UPLOADING,
    FLOW_ERROR
};

// 数据流监控器
class DataFlowMonitor {
private:
    DataFlowState currentState;
    unsigned long stateChangeTime;
    unsigned long processingRate;  // 数据处理速率 (packets/sec)
    
public:
    DataFlowMonitor() : currentState(FLOW_IDLE), stateChangeTime(0), processingRate(0) {}
    
    void setState(DataFlowState newState);
    DataFlowState getState() const { return currentState; }
    unsigned long getStateTime() const { return millis() - stateChangeTime; }
    
    void updateProcessingRate(unsigned long packetsProcessed);
    unsigned long getProcessingRate() const { return processingRate; }
    
    // 性能监控
    void printStatus() const;
};

extern DataFlowMonitor dataFlowMonitor;

#endif // DATAFLOWMANAGER_H
