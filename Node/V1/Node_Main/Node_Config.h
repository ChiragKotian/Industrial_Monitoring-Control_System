#ifndef NODE_CONFIG_H
#define NODE_CONFIG_H

#include <Arduino.h>


// 🔌 SHARED HARDWARE SPI BUS (CAN + SD Card)
#define SHARED_SCK    4
#define SHARED_MOSI   6

// SEPARATE MISO PINS
#define CAN_MISO      5
#define SD_MISO       48

#define CAN_CS        7
#define SD_CS         26
#define CAN_INT     2

#define STORAGE_BATCH_SIZE 20  // Write to SD only after 20 packets
#define STORAGE_TIMEOUT_MS 5000 // Or every 5 seconds, whichever comes first

// 📊 Protocol & Registry Allocations
#define MAX_NODE_ID       240
#define EMPTY_SLOT_MARK   255
#define DISCOVERY_WINDOW  3000 

// ⚙️ ALLOT PROTOCOL CODES v1.0
#define CMD_DISCOVER      0x01
#define CMD_SHIFT_MODE    0x02
#define DATA_STREAM       0x04
#define CMD_REQ_RESEND    0x05
#define CMD_REQ_DIAG      0x06  // 🚀 NEW: Diagnostic request opcode

// 🏎️ Deterministic FreeRTOS Priority Escalation Matrix
#define PRIORITY_CAN      4   
#define PRIORITY_HMI      3   
#define PRIORITY_SD       2   
#define PRIORITY_LORA     1   

// 📺 EXTERNAL I2C OLED PINS (Recycled from old SD SPI)
#define EXT_OLED_SDA  19
#define EXT_OLED_SCL  20
#define EXT_OLED_RST  21 

#endif