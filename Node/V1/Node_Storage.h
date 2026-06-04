#ifndef NODE_STORAGE_H
#define NODE_STORAGE_H

#include <Arduino.h>
#include <FS.h>
#include <SD.h>
#include <SPI.h>
#include "Node_Config.h"

// 📦 Decoupled data footprint carrying raw, finalized segment arrays
struct LogPayload {
    uint8_t  nodeId;
    uint8_t  groupType;
    uint8_t  payloadBytes[32]; // Stores up to 32 bytes of raw compiled data
    uint8_t  length;
    uint32_t timestamp;
};

class NodeStorage {
private:
    static bool sdAvailable;
    static const int sdCS = 13;

public:
    static void init();
    
    // ✅ Polymorphic non-blocking pipeline ingestion wrapper
    static bool queueRawRow(uint8_t id, uint8_t group, uint8_t* rawData, uint8_t length);
    
    static void runStorageWorker(void* pvParameters);
};

#endif