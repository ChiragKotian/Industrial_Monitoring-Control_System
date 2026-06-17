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
    if (xStorageQueueHandle == NULL || !sdAvailable) return;

    StorageLogPacket packet;
    strncpy(packet.dataRow, csvRow.c_str(), sizeof(packet.dataRow) - 1);
    packet.dataRow[sizeof(packet.dataRow) - 1] = '\0'; 

    // Send to queue. If queue is full (0), drop the packet to save the CAN engine
    xQueueSend(xStorageQueueHandle, &packet, 0);
}

void NodeStorage::runStorageWorker(void* pvParameters) {
    StorageLogPacket bufferedPacket;
    
    for (;;) {
        // Block until data arrives (Zero CPU usage while waiting)
        if (xQueueReceive(xStorageQueueHandle, &bufferedPacket, portMAX_DELAY) == pdTRUE) {
            
            if (sdAvailable) {
                File runtimeFile = SD.open("/telemetry.csv", FILE_APPEND);
                if (runtimeFile) {
                    runtimeFile.print(bufferedPacket.dataRow);
                    runtimeFile.flush();
                    runtimeFile.close();
                } else {
                    Serial.println(F("[Storage] File open failed."));
                }
            }
            vTaskDelay(pdMS_TO_TICKS(50)); // Tiny cooling window for SD Flash controller
        }
    }
}