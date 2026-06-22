#include "Node_Storage.h"

bool NodeStorage::sdAvailable = false;
bool NodeStorage::isSystemReady = false;
QueueHandle_t NodeStorage::xStorageQueueHandle = NULL;

void NodeStorage::init() {
    xStorageQueueHandle = xQueueCreate(50, sizeof(StorageLogPacket));
    configASSERT(xStorageQueueHandle != NULL); 
    
    SPI.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);
    if(SD.begin(SD_CS, SPI)) {
        sdAvailable = true;
        Serial.println(F("[Storage Engine] SD Card Mounted."));
    } else {
        Serial.println(F("[Storage Engine] SD Warning: Card not detected."));
    }
    
    isSystemReady = true; 
}

void NodeStorage::logStringPacket(const String& csvRow) {
    // If card is out, drop packets instantly so the queue doesn't overflow
    if (xStorageQueueHandle == NULL || !sdAvailable) return;

    StorageLogPacket packet;
    strncpy(packet.dataRow, csvRow.c_str(), sizeof(packet.dataRow) - 1);
    packet.dataRow[sizeof(packet.dataRow) - 1] = '\0'; 

    // Send to queue. If queue is full (0), drop the packet to save the CAN engine
    xQueueSend(xStorageQueueHandle, &packet, 0);
}

void NodeStorage::runStorageWorker(void* pvParameters) {
    StorageLogPacket bufferedPacket;
    uint32_t lastRemountAttempt = 0;
    
    for (;;) {
        // Block until data arrives OR 1 second passes (pdMS_TO_TICKS(1000))
        // This allows the task to wake up and check the SD status even if the queue is empty
        if (xQueueReceive(xStorageQueueHandle, &bufferedPacket, pdMS_TO_TICKS(1000)) == pdTRUE) {
            
            if (sdAvailable) {
                File runtimeFile = SD.open("/telemetry.csv", FILE_APPEND);
                if (runtimeFile) {
                    runtimeFile.print(bufferedPacket.dataRow);
                    runtimeFile.flush();
                    runtimeFile.close();
                } else {
                    // 🚨 HOT-UNPLUG DETECTED
                    // The file failed to open. The card was physically yanked out!
                    sdAvailable = false;
                    Serial.println(F("🚨 [Storage Engine] SD Card Hot-Unplug Detected! Health -> ERR"));
                }
            }
            vTaskDelay(pdMS_TO_TICKS(50)); // Tiny cooling window for SD Flash controller
            
        } else {
            // 🔄 HOT-PLUG AUTO-RECOVERY
            // The queue was empty for 1 second. If the SD card is currently disconnected,
            // use this background idle time to try and remount it every 5 seconds.
            if (!sdAvailable && (millis() - lastRemountAttempt > 5000)) {
                lastRemountAttempt = millis();
                
                SD.end(); // Clear the broken SPI state memory
                
                if(SD.begin(SD_CS, SPI)) {
                    sdAvailable = true;
                    Serial.println(F("✅ [Storage Engine] SD Card Hot-Plugged and Remounted!"));
                }
            }
        }
    }
}