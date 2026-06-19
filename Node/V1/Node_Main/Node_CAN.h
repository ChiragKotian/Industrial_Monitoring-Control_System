#ifndef NODE_CAN_H
#define NODE_CAN_H

#include <Arduino.h>
#include <SPI.h>
#include <mcp2515.h>
#include "Node_Config.h"

enum NetworkState {
    STATE_STANDBY,
    STATE_INIT_DISCOVERY,
    STATE_COLLECTING_REPLIES,
    STATE_SEND_ACK_SEQUENTIAL,  
    STATE_RECHECK_WINDOW,       
    STATE_OPERATIONAL_MODE
};

class NodeCAN {
private:
    static SPIClass hspiCAN;
    static MCP2515 mcp2515;
    static NetworkState currentBusState;

public:
    static uint32_t currentPollingInterval; 
    static uint32_t discoveryStartTime; // Exposed for UI progress bar calculation

    static void init();
    static void startDiscoveryCycle();
    static void broadcastEndCycle();
    static void sendCommand(uint8_t targetId, uint8_t instructionId, uint8_t* payload, uint8_t dlc);
    static void requestFreshDiagnostics(uint8_t targetLmpId); 
    static void parseIncomingFrame(struct can_frame& frame);
    static void runNetworkWorker(void* pvParameters);
    
    // 🚀 NEW: Expose network engine state directly to the HMI layer
    static NetworkState getBusState() { return currentBusState; }
};

#endif