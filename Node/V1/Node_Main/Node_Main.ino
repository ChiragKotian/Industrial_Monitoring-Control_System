/**
 * @file Node_Main.ino
 * @brief Industrial Telemetry Gateway Master Core Execution File
 * @author Chirag Kotian / HPCL Mumbai Refinery R&D Baseline
 * * DESIGN ARCHITECTURE:
 * - Core 0: Dedicated to Asynchronous User Interfaces and Display Tasks (Low Priority).
 * - Core 1: Dedicated to High-Interrupt, Time-Sensitive Fieldbus Networks & Storage (High Priority).
 * - Synchronization: Maintained strictly via Global Binary/Mutex Primitives.
 */

#include "Node_Config.h"
#include "Node_Storage.h"
#include "Node_CAN.h"
#include "Node_UI.h"
#include "Node_Registry.h"

// Task handles for diagnostic logging and stack monitoring
TaskHandle_t hStorageTask = NULL;
TaskHandle_t hCanTask      = NULL;
TaskHandle_t hUiTask       = NULL;

void setup() {
    // 1. Initialize High-Speed Serial Debug Diagnostic Interface
    Serial.begin(115200);
    delay(1000); // Give the host USB CDC layer time to establish stability

    Serial.println(F("\n=================================================="));
    Serial.println(F("   HPCL GATEWAY NODE - PHYSICAL HARDWARE DEPLOY   "));
    Serial.println(F("=================================================="));

    // 2. DEPENDENCY SEQUENCING: Peripheral Handshaking Layer
    // Initialize storage first to allocate the global SPI mutex guard and memory queues
    Serial.println(F("[SYSTEM] Allocating System Memory Pipelines..."));
    NodeStorage::init(); 

    // Initialize the Field Bus Interface (SPI Controller and Hardware Bitrates)
    NodeCAN::init();

    // Initialize the Display Drivers, Fonts, and Input Map Matrices
    NodeUI::init();

    Serial.println(F("[SYSTEM] Spawning Pinned Core Threads..."));

    // 3. TASK SCHEDULER MATRIX (Symmetric Multiprocessing Deployment)
    
    // Core 0: High-Latency IO & Graphics Layer
    xTaskCreatePinnedToCore(
        NodeUI::runUIWorker,          // Task function pointer
        "HMI_Task",                   // Debug name
        4096,                         // Stack size (Words)
        NULL,                         // Parameters passed
        1,                            // Low priority (Preemptible by network alerts)
        &hUiTask,                     // Task tracking handle
        0                             // Pin execution to Core 0
    );

    // Core 1: Balanced Storage Thread (Shares SPI bus via Mutex)
    xTaskCreatePinnedToCore(
        NodeStorage::runStorageWorker,
        "Storage_Task",
        4096,
        NULL,
        2,                            // Medium priority
        &hStorageTask,
        1                             // Pin execution to Core 1
    );

    // Core 1: Maximum Priority Thread for Low-Latency Real-Time Network Engine
    xTaskCreatePinnedToCore(
        NodeCAN::runNetworkWorker,
        "CAN_Task",
        4096,
        NULL,
        3,                            // Maximum priority thread (Must process frames instantly)
        &hCanTask,
        1                             // Pin execution to Core 1
    );

    Serial.println(F("[SYSTEM] Peripherals Online. Triggering Network Discovery..."));
    
    // 4. Fire the physical CAN discovery protocol to find live LMPs on the network
    NodeCAN::startDiscoveryCycle();

    // 5. Clean up the boot thread completely to preserve CPU scheduling performance
    vTaskDelete(NULL); 
}

void loop() {
    // This loop is unreachable due to vTaskDelete(NULL) in setup. 
    // All system instructions are managed completely by the FreeRTOS kernel.
}