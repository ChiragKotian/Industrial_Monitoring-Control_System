#ifndef NODE_STORAGE_H
#define NODE_STORAGE_H

#include "Node_Config.h"
#include <SD.h>

struct StorageLogPacket {
    char dataRow[64]; 
};

class NodeStorage {
public:
    static void init();
    static void logStringPacket(const String& csvRow);
    static void runStorageWorker(void* pvParameters);
    
    static bool sdAvailable;
    static bool isSystemReady;
    static QueueHandle_t xStorageQueueHandle;

private:
    static bool switchToSD(); // 🔄 The Resync-on-Switch Helper
};

#endif