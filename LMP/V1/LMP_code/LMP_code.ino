#include <SPI.h>
#include <mcp2515.h>
#include "LMP_Hardware.h"

// Instantiate the CAN controller driver on SPI Chip Select hardware Pin 10
MCP2515 mcp2515(10); 

// Runtime Operational Flags
bool runtimeStreamActive = false;
uint32_t telemetryInterval = 4000; // 🎯 Default auto-stream rate: 4000ms (4 seconds)

void setup() {
    Serial.begin(115200);
    LMP_Hardware::init();

    SPI.begin();
    mcp2515.reset();
    mcp2515.setBitrate(CAN_250KBPS, MCP_8MHZ); 
    mcp2515.setNormalMode();

    Serial.print(F("🔒 LMP Substation Node Active. Listening on Node ID: "));
    Serial.println(LMP_ID);
}

void loop() {
    struct can_frame rxFrame;

    if (mcp2515.readMessage(&rxFrame) == MCP2515::ERROR_OK) {
        
        uint8_t targetId    = rxFrame.data[0]; 
        uint8_t instruction = rxFrame.data[1]; 
        Serial.println(targetId);
        
        bool isBroadcast = (rxFrame.can_id == targetId);

        if (targetId != LMP_ID && !isBroadcast) return;

        if (instruction == 0x08 || instruction == 0x09) {
            LMP_Hardware::executeAction(instruction, rxFrame.data, rxFrame.can_dlc);
            if (instruction != 0x09) return; 
        }

        switch (instruction) {
            
            case 0x01: // CMD_DISCOVER 
                if (isBroadcast) {
                    struct can_frame txFrame;
                    txFrame.can_id = LMP_ID; 
                    txFrame.can_dlc = 4;
                    txFrame.data[0] = 0x00;        
                    txFrame.data[1] = 0x01;        
                    txFrame.data[2] = LMP_ID;      
                    txFrame.data[3] = LMP_GROUP;   
                    mcp2515.sendMessage(&txFrame);
                    Serial.println(F("📡 Handshake Sent."));
                }
                break;

            case 0x02: // CMD_SHIFT_MODE 
                runtimeStreamActive = true;
                Serial.println(F("📈 Handshake Closed. Transitioning to Autonomous Telemetry Space."));
                break;

            case 0x04: // Explicit Data Request Sweep
            case 0x05: // Fault Request Line (Master dropped a fragment)
                LMP_Hardware::refresh(); 
                LMP_Hardware::sendStream(mcp2515, 0x04);
                break;

            case 0x06: // CMD_REQ_DIAG
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
                
            // 🎯 NEW: DYNAMIC POLLING RATE CONFIGURATION
            case 0x07: // CMD_SET_POLL_RATE
                if (rxFrame.can_dlc >= 4) {
                    // Unpack the incoming interval (Master sends it divided by 100 to fit in 2 bytes)
                    telemetryInterval = (rxFrame.data[2] << 8 | rxFrame.data[3]) * 100;
                }
                // Fall-through instantly to dispatch an acknowledgment back to the Master!
                
            case 0x0A: { // CMD_GET_POLL_RATE
                struct can_frame txRate;
                txRate.can_id = LMP_ID;
                txRate.can_dlc = 4;
                txRate.data[0] = 0x00;           // Target Master Gateway
                txRate.data[1] = 0x0A;           // Opcode: Config Reply
                
                uint16_t q_rate = telemetryInterval / 100;
                txRate.data[2] = (q_rate >> 8) & 0xFF; 
                txRate.data[3] = q_rate & 0xFF;
                mcp2515.sendMessage(&txRate);
                
                Serial.print(F("⚙️ Telemetry Rate synced: "));
                Serial.print(telemetryInterval);
                Serial.println(F(" ms"));
                break;
            }
                
            default:
                break;
        }
    }

    // Rugged Non-Blocking Background Maintenance loop
    static unsigned long pastSamplingMark = 0;
    unsigned long currentMark = millis();
    
    // 🎯 FIX: Auto-stream data using the dynamic telemetryInterval!
    if (currentMark - pastSamplingMark >= telemetryInterval) {
        pastSamplingMark = currentMark;
        
        if (!LMP_Hardware::isActuator()) {
            LMP_Hardware::refresh(); 
            if (runtimeStreamActive) {
                LMP_Hardware::sendStream(mcp2515, 0x04);
            }
        }
    }
}