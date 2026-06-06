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
 Serial.println(F("[Sandbox] Simulation engine online. Waiting for Discovery phase..."));
    vTaskDelay(pdMS_TO_TICKS(1000));

    // 🏎️ Step 1: Initialize Network Discovery Phase
    NodeCAN::startDiscoveryCycle();

    // 🔑 THE ARCHITECTURAL RESET SYNC FILTER:
    // Yield processor control for 150ms to allow Core 1's real-time loops 
    // to advance the global machine state from STATE_INIT_DISCOVERY into STATE_COLLECTING_REPLIES safely!
    vTaskDelay(pdMS_TO_TICKS(150)); 

    // Inject mock responses from LMPs responding to the Hub's identification query
    struct can_frame lmp40_disc;
    lmp40_disc.can_id = 40; lmp40_disc.can_dlc = 3;
    lmp40_disc.data[0] = 0x00; lmp40_disc.data[1] = 0x01; lmp40_disc.data[2] = 1; // Group 1 (Thermal Sensor Panel)

    struct can_frame lmp165_disc;
    lmp165_disc.can_id = 165; lmp165_disc.can_dlc = 3;
    lmp165_disc.data[0] = 0x00; lmp165_disc.data[1] = 0x01; lmp165_disc.data[2] = 2; // Group 2 (Relay Actuator Panel)

    // Push into our parser sequentially as if they just arrived over the physical Cat6 wire rules
    Serial.println(F("[Sandbox] Injecting Node 40 Discovery Echo..."));
    NodeCAN::parseIncomingFrame(lmp40_disc);
    
    vTaskDelay(pdMS_TO_TICKS(200)); // Brief network propagation slice
    
    Serial.println(F("[Sandbox] Injecting Node 165 Discovery Echo..."));
    NodeCAN::parseIncomingFrame(lmp165_disc);

    // Wait for the 3.5-second discovery collection timer window to close completely
    vTaskDelay(pdMS_TO_TICKS(3500));
    Serial.println(F("\n[Sandbox] Discovery closed. Commencing Real-time Stream Testing..."));

    for (;;) {
        // SCENARIO B: Clean polymorphic status feedback from Group 2 (Actuator Node 165)
        struct can_frame acp_frame;
        acp_frame.can_id = 165; acp_frame.can_dlc = 8; 
        acp_frame.data[0] = 0x00; acp_frame.data[1] = 0x04;
        acp_frame.data[2] = 0; // Standalone message token
        acp_frame.data[3] = 0xA4; acp_frame.data[4] = 0x01; acp_frame.data[5] = 0x00;
        acp_frame.data[6] = 0x00; acp_frame.data[7] = 0x00;

        NodeCAN::parseIncomingFrame(acp_frame);
        vTaskDelay(pdMS_TO_TICKS(1500));

        // SCENARIO C: Interleaved Fragment Reassembly from Group 1 (Thermal Sensor Node 40)
        struct can_frame temp_frag1;
        temp_frag1.can_id = 40; temp_frag1.can_dlc = 6;
        temp_frag1.data[0] = 0x00; temp_frag1.data[1] = 0x04;
        temp_frag1.data[2] = 1; // 1 data segment follows after this frame
        temp_frag1.data[3] = 0x02; temp_frag1.data[4] = 0x88; temp_frag1.data[5] = 0x01;

        struct can_frame temp_frag2;
        temp_frag2.can_id = 40; temp_frag2.can_dlc = 5;
        temp_frag2.data[0] = 0x00; temp_frag2.data[1] = 0x04;
        temp_frag2.data[2] = 0; // Complete transaction marker
        temp_frag2.data[3] = 0x1B; temp_frag2.data[4] = 0x55;

        NodeCAN::parseIncomingFrame(temp_frag1);
        vTaskDelay(pdMS_TO_TICKS(150)); // Short wire propagation slice
        NodeCAN::parseIncomingFrame(temp_frag2);
        
        vTaskDelay(pdMS_TO_TICKS(3000)); 
    }
}