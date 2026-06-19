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
    static uint32_t discoveryStartTime; 
    
    // 🎯 NEW: Links the UI menu to the CAN Engine
    static uint8_t activeDiagnosticNode; 

    static void init();
    static void startDiscoveryCycle();
    static void broadcastEndCycle();
    static void sendCommand(uint8_t targetId, uint8_t instructionId, uint8_t* payload, uint8_t dlc);
    
    static void requestFreshDiagnostics(uint8_t targetLmpId); 
    static void parseIncomingFrame(struct can_frame& frame);
    static void runNetworkWorker(void* pvParameters);
    
    static NetworkState getBusState() { return currentBusState; }
};

#endif