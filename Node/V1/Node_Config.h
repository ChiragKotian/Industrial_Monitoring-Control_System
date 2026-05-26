#ifndef NODE_CONFIG_H
#define NODE_CONFIG_H

#include <Arduino.h>

// 🔌 Custom Hardware SPI Pin Allocation for CAN Module
#define CAN_SCK     4
#define CAN_MISO    5
#define CAN_MOSI    6
#define CAN_CS      7
#define CAN_INT     2

// 📊 Protocol & Registry Allocations
#define MAX_NODE_ID       240
#define EMPTY_SLOT_MARK   255
#define DISCOVERY_WINDOW  3000 // 3-second aggregation loop

// 🏎️ Deterministic FreeRTOS Priority Escalation Matrix
#define PRIORITY_CAN      4   // High: Real-time packet parsing
#define PRIORITY_HMI      3   // Med-High: Responsive Local HMI
#define PRIORITY_SD       2   // Med: Asynchronous CSV storage
#define PRIORITY_LORA     1   // Low: Background cloud uplink

#endif