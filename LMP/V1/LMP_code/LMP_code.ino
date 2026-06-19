#include <SPI.h>
#include <mcp2515.h>
#include "LMP_Hardware.h"

// Instantiate the CAN controller driver on SPI Chip Select hardware Pin 10
MCP2515 mcp2515(10); 

// Runtime Operational Flags
bool runtimeStreamActive = false;

void setup() {
    Serial.begin(115200);
    
    // 1. Initialize the localized multi-file Hardware Abstraction Layer
    LMP_Hardware::init();

    // 2. Drive SPI core lines high to establish bus communication stability
    SPI.begin();
    
    // 3. Clear MCP2515 system registers and align network transmission frequency
    mcp2515.reset();
    // 🚨 PROTOCOL SYNC: Locked to 250KBPS to match the Master Gateway
    // If your hardware fails to talk, change MCP_8MHZ to MCP_16MHZ based on your crystal!
    mcp2515.setBitrate(CAN_250KBPS, MCP_8MHZ); 
    mcp2515.setNormalMode();

    Serial.print(F("🔒 LMP Substation Node Active. Listening on Node ID: "));
    Serial.println(LMP_ID);
}

void loop() {
    struct can_frame rxFrame;

    // Check physical hardware buffer pipelines for incoming messages
    if (mcp2515.readMessage(&rxFrame) == MCP2515::ERROR_OK) {
        
        uint8_t targetId    = rxFrame.data[0]; // Byte 0 dictates recipient destination
        uint8_t instruction = rxFrame.data[1]; // Byte 1 houses protocol instruction
        
        // 🔥 YOUR FIX: Generalized Broadcast Mapping (Sender ID == Target ID)
        bool isBroadcast = (rxFrame.can_id == targetId);

        // Substation Traffic Filter: Drop package instantly if it isn't for us or a broadcast
        if (targetId != LMP_ID && !isBroadcast) return;

        // Route physical relay operations directly to HAL execution blocks
        if (instruction == 0x08 || instruction == 0x09) {
            LMP_Hardware::executeAction(instruction, rxFrame.data, rxFrame.can_dlc);
            // Actuator commands drop out here to save bandwidth unless explicitly polled
            if (instruction != 0x09) return; 
        }

        // Protocol Tree Execution Branch
        switch (instruction) {
            
            case 0x01: // CMD_DISCOVER (Master Network Discovery Sweep)
                if (isBroadcast) {
                    struct can_frame txFrame;
                    txFrame.can_id = LMP_ID; 
                    txFrame.can_dlc = 4;
                    txFrame.data[0] = 0x00;        // Target: Master Gateway
                    txFrame.data[1] = 0x01;        // Opcode: Identity Response
                    txFrame.data[2] = LMP_ID;      // Assigned Identifier
                    txFrame.data[3] = LMP_GROUP;   // Static operational group type
                    mcp2515.sendMessage(&txFrame);
                    Serial.println(F("📡 Handshake Sent."));
                }
                break;

            case 0x02: // CMD_SHIFT_MODE (Transition to telemetry space)
                runtimeStreamActive = true;
                Serial.println(F("📈 Handshake Closed. Transitioning to Autonomous Telemetry Space."));
                break;

            case 0x04: // Explicit Data Request Sweep
            case 0x05: // Fault Request Line (Master dropped a fragment)
                LMP_Hardware::refresh(); 
                LMP_Hardware::sendStream(mcp2515, 0x04);
                break;

            case 0x06: // CMD_REQ_DIAG
                    // Safely respond to the Master's background round-robin request
                    struct can_frame txDiag;
                    txDiag.can_id = LMP_ID;
                    txDiag.can_dlc = 3;
                    txDiag.data[0] = 0x00;           
                    txDiag.data[1] = 0x06;           
                    txDiag.data[2] = LMP_Hardware::getErrorCode(); 
                    mcp2515.sendMessage(&txDiag);
 
                    
                    Serial.print(F("⚙️ Polled diagnostic byte dispatched: 0x"));
                    Serial.println(txDiag.data[2], HEX);
                    break;
                
            default:
                break;
        }
    }

    // Rugged Non-Blocking Background Maintenance loop
    static unsigned long pastSamplingMark = 0;
    unsigned long currentMark = millis();
    
    // Auto-stream data every 4 seconds if Shift Mode was activated
    if (currentMark - pastSamplingMark >= 4000) {
        pastSamplingMark = currentMark;
        
        if (!LMP_Hardware::isActuator()) {
            LMP_Hardware::refresh(); 
            if (runtimeStreamActive) {
                LMP_Hardware::sendStream(mcp2515, 0x04);
            }
        }
    }
}