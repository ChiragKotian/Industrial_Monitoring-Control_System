#ifndef NODE_CAN_H
#define NODE_CAN_H

#include <Arduino.h>
#include <SPI.h>
#include <mcp2515.h>
#include "Node_Config.h" 

// 🚦 Network State Machine
enum NetworkState {
    STATE_STANDBY,
    STATE_INIT_DISCOVERY,
    STATE_COLLECTING_REPLIES,
    STATE_SEND_ACK_SEQUENTIAL,
    STATE_RECHECK_WINDOW,
    STATE_OPERATIONAL_MODE
};

class NodeCAN {
public:
    // Core Lifecycle
    static void init();
    static void runNetworkWorker(void* pvParameters);
    
    // UI Triggers & Commands
    static void startDiscoveryCycle();
    static void requestFreshDiagnostics(uint8_t targetLmpId);
    static void requestPollRate(uint8_t targetLmpId);
    static void setPollRate(uint8_t targetLmpId, uint16_t intervalMs);

    // 🎯 Public State Variables (Accessible to Node_UI)
    static uint16_t lmpPollRates[MAX_NODE_ID + 1];
    static uint8_t activeDiagnosticNode;
    static NetworkState currentBusState;
    static uint32_t discoveryStartTime; // Moved to public for the UI progress bar

    // 🎯 Added getter to satisfy Node_UI.cpp's request
    static NetworkState getBusState() { return currentBusState; }

private:
    // Hardware Components
    static MCP2515 mcp2515;
    
    // 🔄 HARDWARE ARBITRATION HELPERS 
    static bool switchToCAN();
    static void drainCanRx();

    // Internal Mechanics
    static void sendCommand(uint8_t targetId, uint8_t instructionId, uint8_t* payload, uint8_t dlc);
    static void broadcastEndCycle();
    static void parseIncomingFrame(struct can_frame& frame);
};

#endif