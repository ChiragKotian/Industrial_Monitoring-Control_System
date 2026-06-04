#include "Node_Storage.h"

bool NodeStorage::sdAvailable = false;
static QueueHandle_t xStorageQueue = NULL;

void NodeStorage::init() {
    xStorageQueue = xQueueCreate(30, sizeof(LogPayload));
    if (xStorageQueue == NULL) return;

    if (!SD.begin(sdCS)) {
        sdAvailable = false;
        return;
    }

    File file = SD.open("/telemetry.csv", FILE_APPEND);
    if (file) {
        if (file.size() == 0) {
            // Standard generic data ledger columns
            file.println(F("Timestamp_ms,Node_ID,Group_Type,Parsed_Industrial_Telemetry_Line"));
        }
        file.close();
        sdAvailable = true;
    }
}

bool NodeStorage::queueRawRow(uint8_t id, uint8_t group, uint8_t* rawData, uint8_t length) {
    if (xStorageQueue == NULL || !sdAvailable) return false;

    LogPayload snap;
    snap.nodeId = id;
    snap.groupType = group;
    snap.length = (length > 32) ? 32 : length;
    snap.timestamp = millis();
    
    // Raw copy of byte stream directly into data block queue space
    memcpy(snap.payloadBytes, rawData, snap.length);

    BaseType_t state = xQueueSend(xStorageQueue, &snap, 0);
    return (state == pdPASS);
}

void NodeStorage::runStorageWorker(void* pvParameters) {
    LogPayload snap;

    for (;;) {
        if (xQueueReceive(xStorageQueue, &snap, portMAX_DELAY) == pdTRUE) {
            if (!sdAvailable) continue;

            File file = SD.open("/telemetry.csv", FILE_APPEND);
            if (file) {
                // 1. Log system tracking parameters standard across all configurations
                file.print(snap.timestamp); file.print(',');
                file.print(snap.nodeId);    file.print(',');
                file.print(snap.groupType); file.print(',');

                // 2. 🎛️ Dynamic group-based parsing string construction
                switch (snap.groupType) {
                    
                    case 1: { // 🌡️ Thermal Monitoring Group Layout
                        int16_t rawObj = (snap.payloadBytes[0] << 8) | snap.payloadBytes[1];
                        int16_t rawAmb = (snap.payloadBytes[2] << 8) | snap.payloadBytes[3];
                        uint8_t rawHum = snap.payloadBytes[4];
                        
                        float objectTemp = rawObj / 10.0f;
                        float ambientTemp = rawAmb / 10.0f;
                        float humidity    = rawHum / 2.0f;
                        
                        file.print(F("OBJ_T:")); file.print(objectTemp, 1); file.print(F(";"));
                        file.print(F("AMB_T:")); file.print(ambientTemp, 1); file.print(F(";"));
                        file.print(F("HUM:"));   file.print(humidity, 1);
                        break;
                    }
                    
                    case 2: { // 🎚️ Relay Status / Digital Actuator Group Layout
                        uint8_t switchMask = snap.payloadBytes[0];
                        uint8_t breakerFb  = snap.payloadBytes[1];
                        uint8_t interlock  = snap.payloadBytes[2];
                        
                        // Output cleanly formatted raw hex status indicators directly without scaling factors
                        file.print(F("SW_MASK:0x")); file.print(switchMask, HEX); file.print(F(";"));
                        file.print(F("BKR_FB:0x"));  file.print(breakerFb, HEX);  file.print(F(";"));
                        file.print(F("INTLK:0x"));   file.print(interlock, HEX);
                        break;
                    }

                    default: // 📈 Unmapped/Future Expansion fallback formatting dump
                        file.print(F("RAW_HEX:"));
                        for(uint8_t i = 0; i < snap.length; i++) {
                            file.print(snap.payloadBytes[i], HEX);
                            file.print(' ');
                        }
                        break;
                }
                
                file.println(); // Terminate row sequence line entry
                file.close();
            }
        }
    }
}