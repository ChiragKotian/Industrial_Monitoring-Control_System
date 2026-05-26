#include "Node_Registry.h"

// 🔑 Define the FreeRTOS Mutex Handle at file scope (hidden from other files)
static SemaphoreHandle_t xRegistryMutex = NULL;

// 💾 Allocate the actual memory storage statically (No malloc heap risk!)
LMPDataRecord NodeRegistry::registry[MAX_NODE_ID + 1];
uint8_t NodeRegistry::activeNodesArray[MAX_NODE_ID];
uint8_t NodeRegistry::totalDiscoveredCount = 0;

/**
 * @brief Initializes the Mutex and blanks out the data registry arrays.
 */
bool NodeRegistry::initRegistry() {
    // Create a Mutex semaphore. Returns NULL if RAM is completely full.
    xRegistryMutex = xSemaphoreCreateMutex();
    
    if (xRegistryMutex == NULL) {
        return false; // Kernel failed to allocate the key
    }

    // Initialize the main data structures with safe default empty states
    for (int i = 0; i <= MAX_NODE_ID; i++) {
        registry[i].nodeId = EMPTY_SLOT_MARK; // 255 marks the slot as unused
        registry[i].groupType = 0;
        registry[i].objectTemp1 = 0.0f;
        registry[i].objectTemp2 = 0.0f;
        registry[i].ambientTemp = 0.0f;
        registry[i].humidity = 0.0f;
        registry[i].errorCode = 0x00;
        registry[i].lastSeenTimestamp = 0;
    }

    for (int i = 0; i < MAX_NODE_ID; i++) {
        activeNodesArray[i] = EMPTY_SLOT_MARK;
    }

    totalDiscoveredCount = 0;
    return true;
}

/**
 * @brief Public wrappers to manually lock/unlock the database if a task 
 * needs to perform multiple consecutive read operations safely.
 */
void NodeRegistry::lock() {
    if (xRegistryMutex != NULL) {
        // portMAX_DELAY means: "If another task has the key, sleep forever until it's returned"
        xSemaphoreTake(xRegistryMutex, portMAX_DELAY);
    }
}

void NodeRegistry::unlock() {
    if (xRegistryMutex != NULL) {
        xSemaphoreGive(xRegistryMutex); // Drop the key for the next task
    }
}

/**
 * @brief Adds a newly discovered LMP unit into our active database.
 */
void NodeRegistry::registerNode(uint8_t id, uint8_t group) {
    if (id > MAX_NODE_ID) return;

    lock(); // 🔒 ACQUIRE LOCK
    
    if (registry[id].nodeId == EMPTY_SLOT_MARK) {
        registry[id].nodeId = id;
        registry[id].groupType = group;
        registry[id].lastSeenTimestamp = millis();
        
        // Add to our raw discovery list tracker
        if (totalDiscoveredCount < MAX_NODE_ID) {
            activeNodesArray[totalDiscoveredCount] = id;
            totalDiscoveredCount++;
        }
    }
    
    unlock(); // 🔓 RELEASE LOCK
}

/**
 * @brief Updates a specific node's operational data metrics.
 */
void NodeRegistry::updateTelemetry(uint8_t id, float obj1, float obj2, float amb, float hum) {
    if (id > MAX_NODE_ID) return;

    lock(); // 🔒 ACQUIRE LOCK
    
    if (registry[id].nodeId != EMPTY_SLOT_MARK) {
        registry[id].objectTemp1 = obj1;
        registry[id].objectTemp2 = obj2;
        registry[id].ambientTemp = amb;
        registry[id].humidity = hum;
        registry[id].lastSeenTimestamp = millis();
    }
    
    unlock(); // 🔓 RELEASE LOCK
}

/**
 * @brief Marks a safety alert or connection loss bitmask error flags for a unit.
 */
void NodeRegistry::updateNodeError(uint8_t id, uint8_t errorMask) {
    if (id > MAX_NODE_ID) return;

    lock();
    if (registry[id].nodeId != EMPTY_SLOT_MARK) {
        registry[id].errorCode = errorMask;
    }
    unlock();
}

/**
 * @brief Runs sorting on our discovered array once Discovery Mode finishes.
 * This ensures the HMI screen displays nodes sequentially (e.g., Node 3, 4, 12).
 */
void NodeRegistry::finalizeDiscoveryRegistry() {
    lock();
    if (totalDiscoveredCount > 1) {
        // Simple, predictable Bubble Sort tailored for deterministic embedded arrays
        for (uint8_t i = 0; i < totalDiscoveredCount - 1; i++) {
            for (uint8_t j = 0; j < totalDiscoveredCount - i - 1; j++) {
                if (activeNodesArray[j] > activeNodesArray[j + 1]) {
                    uint8_t temp = activeNodesArray[j];
                    activeNodesArray[j] = activeNodesArray[j + 1];
                    activeNodesArray[j + 1] = temp;
                }
            }
        }
    }
    unlock();
}

/**
 * @brief Safe extraction function for the HMI module to fetch the current active array map.
 * @return Total count of validated physical hardware units discovered.
 */
uint8_t NodeRegistry::getActiveNodesList(uint8_t* outBuffer) {
    uint8_t count = 0;
    
    lock();
    count = totalDiscoveredCount;
    for (uint8_t i = 0; i < totalDiscoveredCount; i++) {
        outBuffer[i] = activeNodesArray[i];
    }
    unlock();
    
    return count;
}

/**
 * @brief Securely extracts a complete visual snapshot block of a single LMP.
 * Prevents the UI task from rendering halfway-corrupted memory chunks.
 */
bool NodeRegistry::getNodeSnapshot(uint8_t id, LMPDataRecord& destRecord) {
    if (id > MAX_NODE_ID) return false;
    bool exists = false;

    lock();
    if (registry[id].nodeId != EMPTY_SLOT_MARK) {
        destRecord = registry[id]; // Direct structural copy while guarded by the key
        exists = true;
    }
    unlock();

    return exists;
}