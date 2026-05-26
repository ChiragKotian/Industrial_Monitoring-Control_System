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
    STATE_OPERATIONAL_MODE
};

class NodeCAN {
private:
    static SPIClass hspiCAN;
    static MCP2515 mcp2515;
    static NetworkState currentBusState;
    
    static void parseIncomingFrame(struct can_frame& frame);

public:
    static void init();
    static void startDiscoveryCycle();
    static void sendUnicastACK(uint8_t targetLmpId);
    static void broadcastEndCycle();
    static void sendCommand(uint8_t targetId, uint8_t instructionId, uint8_t* payload, uint8_t dlc);
    
    // The main execution loops running inside FreeRTOS Task Wrappers
    static void runNetworkWorker(void* pvParameters);
};

#endif