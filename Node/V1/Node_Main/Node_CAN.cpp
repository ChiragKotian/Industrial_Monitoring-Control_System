#include "Node_CAN.h"
#include "Node_Registry.h"
#include "Node_Storage.h"

#define CMD_REQ_RESEND      0x05
#define MAX_RETRIES_ALLOWED 3

struct LMPAssemblyBuffer {
    uint8_t  rawPayload[32];       
    uint8_t  bytesWritten;         
    uint8_t  expectedNextCount;    
    bool     inProgress;           
    uint8_t  retryCounter;         
};

static LMPAssemblyBuffer assemblyLine[MAX_NODE_ID + 1];

SPIClass NodeCAN::hspiCAN(HSPI); 
MCP2515 NodeCAN::mcp2515(CAN_CS, 10000000, &hspiCAN); // Enforce the correct constructor!
NetworkState NodeCAN::currentBusState = STATE_STANDBY;

void NodeCAN::init() {
    hspiCAN.begin(CAN_SCK, CAN_MISO, CAN_MOSI, CAN_CS);
    
    mcp2515.reset();
    mcp2515.setBitrate(CAN_250KBPS, MCP_8MHZ); // Ensure LMP matches this EXACTLY
    mcp2515.setNormalMode();
    
    currentBusState = STATE_STANDBY;
    Serial.println(F("[CAN Engine] MCP2515 Hardware Initialized Successfully."));
}

void NodeCAN::startDiscoveryCycle() {
    currentBusState = STATE_INIT_DISCOVERY;
}

void NodeCAN::sendCommand(uint8_t targetId, uint8_t instructionId, uint8_t* payload, uint8_t dlc) {
    struct can_frame txFrame;
    txFrame.can_id = 0x000;          
    txFrame.can_dlc = 2 + dlc;       
    txFrame.data[0] = targetId;      
    txFrame.data[1] = instructionId; 
    
    for(uint8_t i = 0; i < dlc; i++) {
        if((2 + i) < 8) {
            txFrame.data[2 + i] = payload[i];
        }
    }
    mcp2515.sendMessage(&txFrame);
}

void NodeCAN::broadcastEndCycle() {
    sendCommand(0x00, 0x02, NULL, 0);
    Serial.println(F("[CAN Engine] Operational mode broadcast transmitted to network."));
}

void NodeCAN::parseIncomingFrame(struct can_frame& frame) {
    uint32_t rawId = frame.can_id; 
    
    if (rawId < 1 || rawId > MAX_NODE_ID) return;
    
    // ALLOW target 0x00 (Broadcast Responses) but reject malformed payloads
    if (frame.can_dlc < 3) return;
    
    uint8_t instructionId  = frame.data[1]; 
    LMPAssemblyBuffer& session = assemblyLine[rawId];

    switch (instructionId) {
        case 0x01:
            Serial.print(F("[CAN Diagnostic] Discovery Packet from Node: ")); Serial.println(rawId);
            
            // 🔥 THE FIX: Accept registrations during both the Pulsing and Listening phases!
            if (currentBusState == STATE_COLLECTING_REPLIES || currentBusState == STATE_INIT_DISCOVERY) {
                
                uint8_t groupType = frame.data[3]; 
                
                // Only print the SUCCESS banner once per node to keep logs clean
                uint8_t activeNodes[MAX_NODE_ID];
                uint8_t totalFound = NodeRegistry::getActiveNodesList(activeNodes);
                bool alreadyRegistered = false;
                for(int i=0; i<totalFound; i++) {
                    if (activeNodes[i] == rawId) alreadyRegistered = true;
                }

                if (!alreadyRegistered) {
                    NodeRegistry::registerNode(rawId, groupType);
                    Serial.print(F("🎉 SUCCESS: Node ")); Serial.print(rawId);
                    Serial.print(F(" registered into Group ")); Serial.println(groupType);
                }
            }
            break;
            
        case 0x04: {
            if (currentBusState != STATE_OPERATIONAL_MODE) return;
            
            uint8_t packetsLeft    = frame.data[2]; 
            uint8_t dataBytesInMsg = frame.can_dlc - 3; 
            
            if (!session.inProgress) {
                session.bytesWritten = 0;
                session.expectedNextCount = packetsLeft;
                session.inProgress = true;
            } 
            else if (packetsLeft != session.expectedNextCount) {
                if (session.retryCounter < MAX_RETRIES_ALLOWED) {
                    session.retryCounter++;
                    session.inProgress = false; 
                    uint8_t dummy = 0;
                    sendCommand(rawId, CMD_REQ_RESEND, &dummy, 0);
                } else {
                    session.inProgress = false;
                    session.retryCounter = 0; 
                    NodeRegistry::updateNodeError(rawId, 0x04); 
                }
                return;
            }
            
            for (uint8_t i = 0; i < dataBytesInMsg; i++) {
                if (session.bytesWritten < 32) {
                    session.rawPayload[session.bytesWritten] = frame.data[3 + i];
                    session.bytesWritten++;
                }
            }
            
            if (packetsLeft > 0) {
                session.expectedNextCount--; 
            } 
            else {
                session.inProgress = false; 
                session.retryCounter = 0;
                
                LMPDataRecord nodeInfo;
                if (NodeRegistry::getNodeSnapshot(rawId, nodeInfo)) {
                    uint32_t currentRuntimeMs = millis();
                    String logLine = String(currentRuntimeMs) + "," + String(rawId) + "," + String(nodeInfo.groupType) + ",";

                    switch (nodeInfo.groupType) {
                        case 1: {
                            if (session.bytesWritten >= 4) {
                                int16_t rawObj  = (session.rawPayload[0] << 8) | session.rawPayload[1];
                                int16_t rawAmb  = (session.rawPayload[2] << 8) | session.rawPayload[3];
                                float objectTemp1 = rawObj / 10.0f;
                                float ambientTemp = rawAmb / 10.0f;
                                
                                NodeRegistry::updateTelemetry(rawId, objectTemp1, 0.0f, ambientTemp, 0.0f);
                                logLine += "OBJ_T1:" + String(objectTemp1, 1) + ";CASE_AMB:" + String(ambientTemp, 1) + "\n";
                                
                                if (NodeStorage::isSystemReady) NodeStorage::logStringPacket(logLine);
                            }
                            break;
                        }
                        // Add Case 2, 3, and 4 here exactly as you had them written...
                    } 
                } 
            } 
            break;
        }
    } 
} 

void NodeCAN::runNetworkWorker(void* pvParameters) {
    struct can_frame incomingFrame;
    uint32_t discoveryStartTime = 0;
    static uint8_t discoveryPulseCount = 0;
    
    Serial.println(F("[CAN Task] Engine Live."));

    for (;;) { 
        switch (currentBusState) {
            case STATE_STANDBY:
                break;
                
            case STATE_INIT_DISCOVERY:
                // 🚀 PULSE BROADCAST: Send 3 times to ensure LMPs boot in time
                if (discoveryPulseCount < 3) {
                    sendCommand(0x00, 0x01, NULL, 0);
                    discoveryPulseCount++;
                    vTaskDelay(pdMS_TO_TICKS(500)); 
                } else {
                    discoveryStartTime = millis();
                    currentBusState = STATE_COLLECTING_REPLIES;
                    Serial.println(F("[CAN Engine] Discovery broadcast finished. Listening..."));
                }
                break;
                
            case STATE_COLLECTING_REPLIES:
                if (millis() - discoveryStartTime >= DISCOVERY_WINDOW) {
                    Serial.println(F("[CAN Engine] Discovery window closed."));
                    NodeRegistry::finalizeDiscoveryRegistry();
                    
                    // Replaced standard delay(15) from your logic with non-blocking RTOS delay
                    uint8_t activeNodes[MAX_NODE_ID];
                    uint8_t totalFound = NodeRegistry::getActiveNodesList(activeNodes);
                    for (int i = 0; i < totalFound; i++) {
                        sendCommand(activeNodes[i], 0, NULL, 0); // Send ACK with 0 payload bytes
                        vTaskDelay(pdMS_TO_TICKS(15));  // Yield while maintaining pacing
                    }
                    
                    broadcastEndCycle(); 
                    currentBusState = STATE_OPERATIONAL_MODE;
                }
                break;
                
            case STATE_OPERATIONAL_MODE:
                break;
        }

        // 📥 PURE HARDWARE RX LAYER (No Mutex Required because SPI is independent)
        if (mcp2515.readMessage(&incomingFrame) == MCP2515::ERROR_OK) {
            parseIncomingFrame(incomingFrame);
        }

        vTaskDelay(pdMS_TO_TICKS(5)); // Clean 5ms yield to prevent watchdog resets
    }
}