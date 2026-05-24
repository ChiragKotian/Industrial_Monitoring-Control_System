#include <SPI.h>
#include <mcp2515.h>
#include "LMP_Hardware.h"

MCP2515 mcp2515(10); // SPI Chip Select Pin

void setup() {
    Serial.begin(115200);
    
    // Initialize our custom hardware abstraction framework
    LMP_Hardware::init();

    SPI.begin();
    mcp2515.reset();
    mcp2515.setBitrate(CAN_500KBPS, MCP_8MHZ);
    mcp2515.setNormalMode();

    Serial.println(F("LMP Network Node Online. Ready for commands..."));
}

void loop() {
    struct can_frame rxFrame;

    if (mcp2515.readMessage(&rxFrame) == MCP2515::ERROR_OK) {
        uint8_t targetId    = rxFrame.data[0];
        uint8_t instruction = rxFrame.data[1];
        bool isBroadcast    = (rxFrame.can_id == targetId);

        // Discard data frames not targeted to this node
        if (targetId != LMP_ID && !isBroadcast) return;

        // Route commands instantly to the execution engine if it's an Actuator Action
        if (instruction == 6 || instruction == 8 || instruction == 9 || instruction == 5) {
            LMP_Hardware::executeAction(instruction, rxFrame.data, rxFrame.can_dlc);
            if (instruction != 9) return; // Emergency stop drops down to let network logic run
        }

        // Handle Standard Network Protocol Tree
        struct can_frame txFrame;
        txFrame.can_id = LMP_ID;
        txFrame.data[0] = 0; // Target is always the Master Gateway
        txFrame.data[1] = instruction;

        switch (instruction) {
            case 0: // Discovery Request
                if (isBroadcast) {
                    txFrame.data[2] = LMP_GROUP; // Dynamically identify profile format
                    txFrame.can_dlc = 3;
                    mcp2515.sendMessage(&txFrame);
                }
                break;

            case 2: // Comm Status / Health Polling
                txFrame.data[2] = LMP_Hardware::getErrorCode();
                txFrame.can_dlc = 3;
                mcp2515.sendMessage(&txFrame);
                break;

            case 3: // Force Measure Now & Stream Data
                LMP_Hardware::refresh();
                LMP_Hardware::packTelemetry(txFrame.data, txFrame.can_dlc);
                mcp2515.sendMessage(&txFrame);
                break;

            case 4: // Instantly Send Cached Data Buffer
                LMP_Hardware::packTelemetry(txFrame.data, txFrame.can_dlc);
                mcp2515.sendMessage(&txFrame);
                break;
        }
    }

    // Keep background sensors refreshed periodically (Non-blocking)
    static unsigned long pastTime = 0;
    if (millis() - pastTime >= 4000) {
        pastTime = millis();
        if (!LMP_Hardware::isActuator()) {
            LMP_Hardware::refresh(); 
        }
    }
}