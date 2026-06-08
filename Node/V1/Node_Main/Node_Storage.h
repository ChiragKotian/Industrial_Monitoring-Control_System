#ifndef NODE_STORAGE_H
#define NODE_STORAGE_H

#include "Node_Config.h"
#include <SD.h>


extern QueueHandle_t xStorageQueueHandle;
struct StorageLogPacket {
    char dataRow[96];
};

class NodeStorage {
public:
    static bool sdAvailable;
    static QueueHandle_t xStorageQueueHandle;
    static SemaphoreHandle_t xStorageMutex;
    static bool isSystemReady;

    static void init();
    static void logStringPacket(const String& csvRow); // Ensure this matches exactly
    static void runStorageWorker(void* pvParameters);
};

#endif