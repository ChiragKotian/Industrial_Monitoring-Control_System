#include "Node_Config.h"
#include "Node_Registry.h"
#include "Node_CAN.h"
#include "Node_UI.h"

// Define global handles if tasks need direct signalling later
TaskHandle_t xCanTaskHandle = NULL;
TaskHandle_t xHmiTaskHandle = NULL;

void setup() {
    Serial.begin(115200);
    
    // 1. Initialize Thread-safe Mutexes and Registry arrays
    if(!NodeRegistry::initRegistry()) {
        Serial.println(F("CRITICAL ERROR: Memory Registry Lock Allocation Failed!"));
        while(1); // Lock here before launching tasks if OS cannot protect registers
    }
    
    // 2. Wake up local physical peripherals
    NodeUI::init();  // Boots OLED screen and binds button ISRs
    NodeCAN::init(); // Configures the secondary HSPI lines and MCP2515 registers
    
    Serial.println(F("System Hardware Stabilized. Instantiating FreeRTOS Core Tasks..."));
    
    // 3. Spawn Thread Engines into the ESP32 Core Scheduler
    xTaskCreatePinnedToCore(
        NodeCAN::runNetworkWorker,  // Function pointer to execution loop
        "CAN_NETWORK_TASK",         // Text name for debugging logs
        4096,                       // Stack depth allocated in words
        NULL,                       // Task input parameters passed
        PRIORITY_CAN,               // Strict Priority Level
        &xCanTaskHandle,            // Task Handle reference pointer
        1                           // Pin strictly to Core 1 (keep app isolated)
    );

    xTaskCreatePinnedToCore(
        NodeUI::runHMITask,
        "HMI_DISPLAY_TASK",
        4096,
        NULL,
        PRIORITY_HMI,
        &xHmiTaskHandle,
        0                           // Pin strictly to Core 0 (shares display/input cycles)
    );
    
    Serial.println(F("FreeRTOS Kernel Running. System Fully Operational."));
}

void loop() {
    // 🚨 ABSOLUTELY EMPTY. In professional FreeRTOS architecture, loop() 
    // is deleted from the execution loop because the tasks run continuously.
    vTaskDelete(NULL); 
}