#include "Node_Storage.h"
#include <SPI.h>

extern SemaphoreHandle_t hSpiMutex;
extern SPIClass hspiShared;

bool NodeStorage::sdAvailable = false;
bool NodeStorage::isSystemReady = false;
QueueHandle_t NodeStorage::xStorageQueueHandle = NULL;

// ---------------------------------------------------------
// Switch bus to SD card: full re-init, not just pin remap.
// ---------------------------------------------------------
bool NodeStorage::switchToSD() {
    digitalWrite(CAN_CS, HIGH); // deselect CAN first
    hspiShared.end();
    delayMicroseconds(50);
    hspiShared.begin(SHARED_SCK, SD_MISO, SHARED_MOSI, SD_CS);

    // CRITICAL: Force a real remount every time by ending it first.
    SD.end();
    bool ok = SD.begin(SD_CS, hspiShared, 4000000);
    return ok;
}

void NodeStorage::init() {
    // xStorageQueueHandle = xQueueCreate(50, sizeof(StorageLogPacket));
    xStorageQueueHandle = xQueueCreate(50, sizeof(StorageLogPacket));
    configASSERT(xStorageQueueHandle != NULL);
    
    // Initial SD Mount with Retries (from your v2 code)
    bool sdOk = switchToSD();
    for (int i = 0; i < 3 && !sdOk; i++) {
        delay(50);
        sdOk = switchToSD();
    }
    
    if(sdOk) {
        sdAvailable = true;
        Serial.println(F("[Storage Engine] SD Card Mounted."));
    } else {
        Serial.println(F("[Storage Engine] SD Warning: Card not detected."));
    }
    
    isSystemReady = true;
}

void NodeStorage::logStringPacket(const String& csvRow) {
    if (xStorageQueueHandle == NULL) return;
    
    StorageLogPacket packet;
    memset(packet.dataRow, 0, MAX_LOG_LENGTH); 
    strncpy(packet.dataRow, csvRow.c_str(), MAX_LOG_LENGTH - 1);
    xQueueSend(xStorageQueueHandle, &packet, 0);
}

void NodeStorage::runStorageWorker(void* pvParameters) {
    // 🛡️ Safe, static array of Structs. Perfectly matches the Queue size!
    static StorageLogPacket batchBuffer[STORAGE_BATCH_SIZE]; 
    int count = 0;
    TickType_t lastFlushTime = xTaskGetTickCount();

    for (;;) {
        StorageLogPacket incomingPacket; // Exactly matches queue size

        if (count < STORAGE_BATCH_SIZE) {
            // Safely pull the full struct out of the queue
            if (xQueueReceive(xStorageQueueHandle, &incomingPacket, pdMS_TO_TICKS(1000)) == pdTRUE) {
                batchBuffer[count] = incomingPacket; // Direct struct-to-struct copy!
                count++;
            }
        }

        // ... Wait for batch full or timeout ...
        
        if (count >= STORAGE_BATCH_SIZE || (count > 0 && (xTaskGetTickCount() - lastFlushTime > pdMS_TO_TICKS(STORAGE_TIMEOUT_MS)))) {
            if (xSemaphoreTake(hSpiMutex, pdMS_TO_TICKS(1000))) {
                if (switchToSD()) {
                    digitalWrite(SD_CS, LOW); 
                    File f = SD.open("/telemetry.csv", FILE_APPEND);
                    if (f) {
                        for(int i=0; i<count; i++) {
                            // Print the character array INSIDE the struct
                            f.print(batchBuffer[i].dataRow);
                        }
                        f.flush();
                        f.close();
                        sdAvailable = true; 
                    } 
                    else {
                        sdAvailable = false; 
                    }
                    
                    // Empty the batch count
                    count = 0; 
                    lastFlushTime = xTaskGetTickCount();
                    
                    digitalWrite(SD_CS, HIGH); // Sleep SD
                } else {
                    sdAvailable = false;
                    // Hardware bus failed. Drop batch to prevent memory pile-up.
                    count = 0;
                    lastFlushTime = xTaskGetTickCount();
                }
                
                xSemaphoreGive(hSpiMutex); // RELEASE KEY
            }
        }
    }
}