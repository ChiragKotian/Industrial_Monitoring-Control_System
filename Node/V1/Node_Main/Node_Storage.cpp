#include "Node_Storage.h"

// 🚀 MANDATORY DEFINITIONS: These allocate physical memory in the ESP32's RAM.
// Without these lines, the linker (ld.exe) will throw "undefined reference" errors.
bool NodeStorage::sdAvailable = false;
bool NodeStorage::isSystemReady = false;
QueueHandle_t NodeStorage::xStorageQueueHandle = NULL;
SemaphoreHandle_t NodeStorage::xStorageMutex = NULL;
// Simple RAM buffer - NO complex Queues or Mutexes to avoid crashes
static String logBuffer = ""; 

// 1. Initialize the storage module
void NodeStorage::init() {
    // Force Queue Creation BEFORE anyone can use it
    xStorageQueueHandle = xQueueCreate(50, sizeof(StorageLogPacket));
    configASSERT(xStorageQueueHandle != NULL); // Use the assert to catch issues early
    
    xStorageMutex = xSemaphoreCreateMutex();
    configASSERT(xStorageMutex != NULL);
    
    // ... initialize SD ...
    sdAvailable = true;
    isSystemReady = true; 
}
// Ensure you are inside the NodeStorage:: scope
void NodeStorage::logStringPacket(const String& csvRow) {
    if (xStorageQueueHandle == NULL || !sdAvailable) return;

    StorageLogPacket packet;
    strncpy(packet.dataRow, csvRow.c_str(), sizeof(packet.dataRow) - 1);
    packet.dataRow[sizeof(packet.dataRow) - 1] = '\0'; 

    xQueueSend(xStorageQueueHandle, &packet, 0);
}

// Just add to a string in RAM - super fast, no bus access
void NodeStorage::runStorageWorker(void* pvParameters) {
    StorageLogPacket bufferedPacket;
    for (;;) {
        if (xQueueReceive(xStorageQueueHandle, &bufferedPacket, portMAX_DELAY) == pdTRUE) {
            
            // 🛡️ THE GLOBAL LOCK
            // We use the Mutex to stop the entire CAN engine from 
            // even *thinking* about SPI while we write.
            if (xSemaphoreTake(xStorageMutex, pdMS_TO_TICKS(5000)) == pdTRUE) {
                
                // FORCE: Re-init the bus every single write cycle 
                // This is the only way to recover if the card disconnected
                SPI.end(); 
                SPI.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);
                
                if (SD.begin(SD_CS, SPI)) {
                    File runtimeFile = SD.open("/telemetry.csv", FILE_APPEND);
                    if (runtimeFile) {
                        runtimeFile.print(bufferedPacket.dataRow);
                        runtimeFile.flush();
                        runtimeFile.close();
                        Serial.println(F("[Storage Engine] Write Success."));
                    }
                }
                xSemaphoreGive(xStorageMutex); 
            }
            vTaskDelay(pdMS_TO_TICKS(200)); // Crucial cool-down for the SD card hardware
        }
    }
}