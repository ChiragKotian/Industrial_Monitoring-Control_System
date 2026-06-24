#include "Node_Config.h"
#include "Node_Storage.h"
#include "Node_CAN.h"
#include "Node_UI.h"
#include "Node_Registry.h"
#include "Node_LoRa.h"

TaskHandle_t hStorageTask = NULL;
TaskHandle_t hCanTask      = NULL;
TaskHandle_t hUiTask       = NULL;

void setup() {
    Serial.begin(115200);
    delay(1000); 

    Serial.println(F("\n=================================================="));
    Serial.println(F("   HPCL GATEWAY NODE - PHYSICAL HARDWARE DEPLOY   "));
    Serial.println(F("=================================================="));

    Serial.println(F("[SYSTEM] Allocating System Memory Pipelines..."));
    
    // Initialize Registry FIRST so Mutexes are available for CAN and UI
    if (!NodeRegistry::initRegistry()) {
        Serial.println(F("[FATAL] Registry Allocation Failed!"));
        while(1);
    }
    
    NodeStorage::init(); 
    NodeCAN::init();
    NodeUI::init();
    NodeLoRa::init();



    Serial.println(F("[SYSTEM] Spawning Pinned Core Threads..."));

    xTaskCreatePinnedToCore(NodeUI::runHMITask, "HMI_Task", 4096, NULL, 1, &hUiTask, 0);
    xTaskCreatePinnedToCore(NodeStorage::runStorageWorker, "Storage_Task", 4096, NULL, 2, &hStorageTask, 1);
    xTaskCreatePinnedToCore(NodeCAN::runNetworkWorker, "CAN_Task", 4096, NULL, 3, &hCanTask, 1);
    xTaskCreatePinnedToCore(NodeLoRa::runLoRaWorker, "LoRa_Task", 4096, NULL, 1, NULL, 1);

    Serial.println(F("[SYSTEM] Peripherals Online. Triggering Network Discovery..."));
    NodeCAN::startDiscoveryCycle();
    vTaskDelete(NULL); 
}

void loop() {}