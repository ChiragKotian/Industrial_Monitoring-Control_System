#ifndef NODE_REGISTRY_H
#define NODE_REGISTRY_H

#include <Arduino.h>
#include "Node_Config.h"

// Unified system telemetry footprint for any given local panel
struct LMPDataRecord {
    uint8_t  nodeId;
    uint8_t  groupType;
    float    objectTemp1;
    float    objectTemp2;
    float    ambientTemp;
    float    humidity;
    uint8_t  errorCode;
    uint32_t lastSeenTimestamp;
};

class NodeRegistry {
private:
    static LMPDataRecord registry[MAX_NODE_ID + 1];
    static uint8_t activeNodesArray[MAX_NODE_ID];
    static uint8_t totalDiscoveredCount;
    
public:
    static bool initRegistry();
    static void lock();
    static void unlock();
    
    // Thread-safe data manipulation APIs
    static void registerNode(uint8_t id, uint8_t group);
    static void updateTelemetry(uint8_t id, float obj1, float obj2, float amb, float hum);
    static void updateNodeError(uint8_t id, uint8_t errorMask);
    
    static void finalizeDiscoveryRegistry(); // Handles filtering placeholders & sorting
    static uint8_t getActiveNodesList(uint8_t* outBuffer);
    static bool getNodeSnapshot(uint8_t id, LMPDataRecord& destRecord);
};

#endif