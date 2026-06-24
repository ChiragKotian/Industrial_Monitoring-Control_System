#include "Node_LoRa.h"

// 🎛️ EXACT INTERNAL PINS FOR HELTEC V3 (ESP32-S3 + SX1262)
#define LORA_SCK 9
#define LORA_MISO 11
#define LORA_MOSI 10
#define RADIO_CS_PIN 8
#define RADIO_DIO1_PIN 14
#define RADIO_RST_PIN 12
#define RADIO_BUSY_PIN 13

// 🔥 Create a dedicated Hardware SPI bus specifically for the LoRa chip
static SPIClass spiLoRa(FSPI); 

// Pass the custom SPI bus into the RadioLib Module
SX1262 radio = new Module(RADIO_CS_PIN, RADIO_DIO1_PIN, RADIO_RST_PIN, RADIO_BUSY_PIN, spiLoRa);

static QueueHandle_t xLoRaQueue = NULL;

void NodeLoRa::init() {
    xLoRaQueue = xQueueCreate(20, sizeof(char) * 64);
    
    Serial.print(F("[LoRa Engine] Initializing SX1262 on IN865 Channel 0... "));
    
    // 1. Boot up the dedicated SPI lines before touching the radio
    spiLoRa.begin(LORA_SCK, LORA_MISO, LORA_MOSI, RADIO_CS_PIN);
    
    // 2. Boot the Radio
    // 865.0625 MHz, 125kHz, SF7, CR 4/5, Sync 0x34
    // 14 dBm power, 8 byte preamble, 1.8V TCXO (Critical for Heltec V3!)
    int state = radio.begin(865.0625, 125.0, 7, 5, 0x34, 14, 8, 1.8, false);
    
    if (state == RADIOLIB_ERR_NONE) {
        Serial.println(F("Success! Radio is HOT."));
    } else {
        Serial.print(F("Failed, code "));
        Serial.println(state);
    }
}

void NodeLoRa::transmitTelemetry(String payload) {
    if (xLoRaQueue == NULL) return;
    
    char buffer[64];
    strncpy(buffer, payload.c_str(), sizeof(buffer) - 1);
    buffer[sizeof(buffer) - 1] = '\0';
    
    xQueueSend(xLoRaQueue, buffer, 0); 
}

void NodeLoRa::runLoRaWorker(void* pvParameters) {
    char bufferedPayload[64];
    
    for(;;) {
        if (xQueueReceive(xLoRaQueue, bufferedPayload, portMAX_DELAY) == pdTRUE) {
            
            // 📡 Transmit the data payload into the air!
            int transmissionState = radio.transmit(bufferedPayload);
            
            if (transmissionState == RADIOLIB_ERR_NONE) {
                Serial.print(F("📡 [LoRa TX] Sent: "));
                Serial.println(bufferedPayload);
            } else {
                Serial.println(F("⚠️ [LoRa TX] Transmission Failed."));
            }
        }
    }
}