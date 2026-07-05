#include "Node_LoRa.h"
#include "Node_Secrets.h"
#include <mbedtls/aes.h> // Use angle brackets to force system-level include
#include <string.h>      // Make sure string ops are explicitly linked

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
bool NodeLoRa::isSystemHealthy = false;

void NodeLoRa::init() {
    xLoRaQueue = xQueueCreate(20, sizeof(char) * 64);
    
    Serial.print(F("[LoRa Engine] Initializing SX1262 on IN865 Channel 0... "));
    
    spiLoRa.begin(LORA_SCK, LORA_MISO, LORA_MOSI, RADIO_CS_PIN);
    
    int state = radio.begin(865.0625, 125.0, 7, 5, 0x34, 14, 8, 1.8, false);
    
    if (state == RADIOLIB_ERR_NONE) {
        Serial.println(F("Success! Radio is HOT."));
        isSystemHealthy = true;
    } else {
        Serial.print(F("Failed, code "));
        Serial.println(state);
        isSystemHealthy = false;
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
            
            // -----------------------------------------------------
            // 🔒 AES-128 ENCRYPTION BLOCK
            // -----------------------------------------------------
            mbedtls_aes_context aes;
            mbedtls_aes_init(&aes);
            mbedtls_aes_setkey_enc(&aes, AES_KEY, 128);

            // 1. Calculate PKCS#7 Padding lengths
            int originalLen = strlen(bufferedPayload);
            int paddingLen = 16 - (originalLen % 16);
            int paddedLen = originalLen + paddingLen;

            unsigned char inputData[128]; // Safe buffer size
            unsigned char encryptedData[128];

            // 2. Apply Data & Padding
            memcpy(inputData, bufferedPayload, originalLen);
            for (int i = originalLen; i < paddedLen; i++) {
                inputData[i] = (unsigned char)paddingLen; // PKCS#7 standard
            }

            // 3. Setup the IV (Must be copied because mbedtls modifies it during CBC)
            unsigned char iv_copy[16];
            memcpy(iv_copy, AES_IV_BASE, 16);

            // 4. Encrypt!
            mbedtls_aes_crypt_cbc(&aes, MBEDTLS_AES_ENCRYPT, paddedLen, iv_copy, inputData, encryptedData);
            mbedtls_aes_free(&aes);

            // -----------------------------------------------------
            // 📡 TRANSMIT ENCRYPTED BYTES
            // -----------------------------------------------------
            // Notice we transmit `encryptedData` as a byte array, NOT a string.
            // Encrypted data contains random zeroes (null terminators) which break strings!
            int transmissionState = radio.transmit(encryptedData, paddedLen);
            
            if (transmissionState == RADIOLIB_ERR_NONE) {
                Serial.print(F("📡 [LoRa TX] Securely Sent "));
                Serial.print(paddedLen);
                Serial.println(F(" bytes."));
            } else {
                Serial.println(F("⚠️ [LoRa TX] Transmission Failed."));
            }
        }
    }
}