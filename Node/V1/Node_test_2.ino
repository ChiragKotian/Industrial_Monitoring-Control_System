#include "Node_Config.h"
#include "Node_Registry.h"
#include "Node_CAN.h"
#include "Node_UI.h"
#include "Node_Storage.h"

// Task Handles for debugging diagnostics
TaskHandle_t xCanTaskHandle     = NULL;
TaskHandle_t xHmiTaskHandle     = NULL;
TaskHandle_t xStorageTaskHandle = NULL;
TaskHandle_t xSandboxTaskHandle = NULL;

// Forward declaration of our simulation injection worker
void vSandboxTestTask(void* pvParameters);

void setup() {
    Serial.begin(115200);
    delay(1000); // Allow physical USB UART lines to stabilize
    Serial.println(F("\n=================================================="));
    Serial.println(F("   HPCL GATEWAY NODE - SYSTEM INTEGRATION TEST      "));
    Serial.println(F("=================================================="));

    // 1. Boot up safe-state Shared Data Mutex Registry vault
    if (!NodeRegistry::initRegistry()) {
        Serial.println(F("[FATAL] Memory Registry Lock Key Allocation Failed!"));
        while (1);
    }
    Serial.println(F("[SYSTEM] Mutex Guard Engine Online."));

    // 2. Initialize physical display and mount input mapping ISR hooks
    NodeUI::init();

    // 3. Configure physical SPI lines and MCP2515 registers
    NodeCAN::init();

    // 4. Initialize background producer-consumer file structure logging queues
    NodeStorage::init();

    Serial.println(F("[SYSTEM] Spawning Pinned Core Threads..."));

    // 🏎️ CORE 1 REAL-TIME PROTOCOL STACK
    xTaskCreatePinnedToCore(
        NodeCAN::runNetworkWorker,
        "CAN_NETWORK_TASK",
        4096,
        NULL,
        PRIORITY_CAN,
        &xCanTaskHandle,
        1 // Strictly executed on real-time Core 1
    );

    // 🖼️ CORE 0 HUMAN INTERFACE DISPLAY WORKER
    xTaskCreatePinnedToCore(
        NodeUI::runHMITask,
        "HMI_DISPLAY_TASK",
        4096,
        NULL,
        PRIORITY_HMI,
        &xHmiTaskHandle,
        0 // Executed on application Core 0
    );

    // 💾 CORE 0 BACKGROUND FILE WRITING PIPELINE CONSUMER
    xTaskCreatePinnedToCore(
        NodeStorage::runStorageWorker,
        "SD_STORAGE_TASK",
        4096,
        NULL,
        PRIORITY_SD,
        &xStorageTaskHandle,
        0 // Executed on background Core 0
    );

    // 🧪 THE HACKATHON TESTBED SANDBOX INTERACTION TASK
    // This script will bypass physical wires and inject raw mock packets straight 
    // into our system to thoroughly test your architecture layers.
    xTaskCreatePinnedToCore(
        vSandboxTestTask,
        "SANDBOX_INJECT_TASK",
        4096,
        NULL,
        1, // Low execution priority matrix rank
        &xSandboxTaskHandle,
        1  // Run on Core 1 to interact directly with the CAN parser
    );

    Serial.println(F("[SYSTEM] All tasks running. Deleting boot setup handle."));
    
    // Kill default setup/loop task to recover memory workspace
    vTaskDelete(NULL);
}

void loop() {
    // Left empty deliberately. RTOS kernel has full command control.
}

/**
 * @brief Automated Test Scenario Injection Engine
 */
void vSandboxTestTask(void* pvParameters) {
    Serial.println(F("[Sandbox] Simulation engine online. Aligning groups..."));
    vTaskDelay(pdMS_TO_TICKS(1000));

    // 1. Kick off network registration loops
    NodeCAN::startDiscoveryCycle();
    vTaskDelay(pdMS_TO_TICKS(150)); 

    // 2. Draft three unique polymorphic discovery requests matching your address split rules
    struct can_frame lmp30_disc; lmp30_disc.can_id = 30; lmp30_disc.can_dlc = 3;
    lmp30_disc.data[0] = 0x00; lmp30_disc.data[1] = 0x01; lmp30_disc.data[2] = 1; // Group 1

    struct can_frame lmp40_disc; lmp40_disc.can_id = 40; lmp40_disc.can_dlc = 3;
    lmp40_disc.data[0] = 0x00; lmp40_disc.data[1] = 0x01; lmp40_disc.data[2] = 2; // Group 2

    struct can_frame lmp50_disc; lmp50_disc.can_id = 50; lmp50_disc.can_dlc = 3;
    lmp50_disc.data[0] = 0x00; lmp50_disc.data[1] = 0x01; lmp50_disc.data[2] = 3; // Group 3

    // Push into the parser sequence
    NodeCAN::parseIncomingFrame(lmp30_disc); vTaskDelay(pdMS_TO_TICKS(100));
    NodeCAN::parseIncomingFrame(lmp40_disc); vTaskDelay(pdMS_TO_TICKS(100));
    NodeCAN::parseIncomingFrame(lmp50_disc); vTaskDelay(pdMS_TO_TICKS(100));

    vTaskDelay(pdMS_TO_TICKS(3500));
    Serial.println(F("\n[Sandbox] Substation registry locked. Injecting multi-segment frames..."));

    for (;;) {
        // 🌡️ SCENARIO A: Group 1 Telemetry (Node 30) -> Obj: 45.2C, Amb: 28.1C
        // Unpacked raw bytes array: [0x01, 0xC4, 0x01, 0x19]
        struct can_frame g1_frame;
        g1_frame.can_id = 30; g1_frame.can_dlc = 7;
        g1_frame.data[0] = 0x00; g1_frame.data[1] = 0x04; g1_frame.data[2] = 0; // Standalone payload
        g1_frame.data[3] = 0x01; g1_frame.data[4] = 0xC4; g1_frame.data[5] = 0x01; g1_frame.data[6] = 0x19;
        
        NodeCAN::parseIncomingFrame(g1_frame);
        vTaskDelay(pdMS_TO_TICKS(1000));

        // 🌡️ SCENARIO B: Group 2 Segmented Telemetry (Node 40) -> Obj: 62.5C, Amb: 30.2C, Hum: 45.5%
        // Unpacked raw bytes array: [0x02, 0x71, 0x01, 0x2E, 0x5B]
        struct can_frame g2_frag1;
        g2_frag1.can_id = 40; g2_frag1.can_dlc = 6;
        g2_frag1.data[0] = 0x00; g2_frag1.data[1] = 0x04; g2_frag1.data[2] = 1; // 1 segment follows
        g2_frag1.data[3] = 0x02; g2_frag1.data[4] = 0x71; g2_frag1.data[5] = 0x01;

        struct can_frame g2_frag2;
        g2_frag2.can_id = 40; g2_frag2.can_dlc = 5;
        g2_frag2.data[0] = 0x00; g2_frag2.data[1] = 0x04; g2_frag2.data[2] = 0; // Final segment
        g2_frag2.data[3] = 0x01; g2_frag2.data[4] = 0x2E; g2_frag2.data[5] = 0x5B;

        NodeCAN::parseIncomingFrame(g2_frag1); vTaskDelay(pdMS_TO_TICKS(100));
        NodeCAN::parseIncomingFrame(g2_frag2); vTaskDelay(pdMS_TO_TICKS(1000));

        // ========================================================================
        // 🌡️ SCENARIO C: Group 3 Multi-Segment Frame Simulation (Node 50)
        // Reassembly Goals: Obj1 = 85.0C, Obj2 = 88.5C, Amb = 29.5C
        // Target raw buffer bytes block: [0x03, 0x52, 0x03, 0x75, 0x01, 0x27] (Exactly 6 Bytes)
        // ========================================================================
        struct can_frame g3_frag1;
        g3_frag1.can_id = 50; 
        g3_frag1.can_dlc = 7; // 3 protocol overhead + 4 payload bytes
        g3_frag1.data[0] = 0x00; 
        g3_frag1.data[1] = 0x04; 
        g3_frag1.data[2] = 1;    // 🔑 Countdown: 1 more segment is coming on the wire!
        g3_frag1.data[3] = 0x03; g3_frag1.data[4] = 0x52; // Obj1 bytes (0x0352 = 850)
        g3_frag1.data[5] = 0x03; g3_frag1.data[6] = 0x75; // Obj2 bytes (0x0375 = 885)

        struct can_frame g3_frag2;
        g3_frag2.can_id = 50; 
        g3_frag2.can_dlc = 5; // 3 protocol overhead + 2 payload bytes
        g3_frag2.data[0] = 0x00; 
        g3_frag2.data[1] = 0x04; 
        g3_frag2.data[2] = 0;    // 🔑 Countdown: 0! Sequence complete.
        g3_frag2.data[3] = 0x01; g3_frag2.data[4] = 0x27; // Amb bytes (0x0127 = 295)

        // Inject the segments sequentially into your parser
        Serial.println(F("[Sandbox] Injecting Group 3 Dual-Zone Target (Node 50 Fragment 1)..."));
        NodeCAN::parseIncomingFrame(g3_frag1);
        
        vTaskDelay(pdMS_TO_TICKS(150)); // Allow short physical propagation delay
        
        Serial.println(F("[Sandbox] Injecting Group 3 Dual-Zone Target (Node 50 Fragment 2)..."));
        NodeCAN::parseIncomingFrame(g3_frag2);
        
        vTaskDelay(pdMS_TO_TICKS(2000)); // Sleep before recycling the loop
    }
}