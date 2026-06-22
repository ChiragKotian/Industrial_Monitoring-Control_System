#ifndef NODE_CONFIG_H
#define NODE_CONFIG_H

#include <Arduino.h>

// 🔌 Custom Hardware SPI Pin Allocation for CAN Module
#define CAN_SCK     4
#define CAN_MISO    5
#define CAN_MOSI    6
#define CAN_CS      7
#define CAN_INT     2

// 💾 Custom Physical Pins for SD Storage Module
#define SD_MOSI     48
#define SD_MISO     19
#define SD_SCK      20
#define SD_CS       26

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

#endif