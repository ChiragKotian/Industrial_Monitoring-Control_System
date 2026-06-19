#include "Node_CAN.h"
#include "Node_Registry.h"
#include "Node_Storage.h"

#define DATA_STREAM         0x04
#define CMD_REQ_RESEND      0x05
#define CMD_REQ_DIAG        0x06
#define MAX_RETRIES_ALLOWED 3

struct LMPAssemblyBuffer {
    uint8_t  rawPayload[32];       
    uint8_t  bytesWritten;         
    uint8_t  expectedNextCount;    
    bool     inProgress;           
    uint8_t  retryCounter;         
};

static LMPAssemblyBuffer assemblyLine[MAX_NODE_ID + 1];

uint32_t NodeCAN::currentPollingInterval = 1000; 
uint32_t NodeCAN::discoveryStartTime = 0;
SPIClass NodeCAN::hspiCAN(HSPI); 
MCP2515 NodeCAN::mcp2515(CAN_CS, 10000000, &hspiCAN);
NetworkState NodeCAN::currentBusState = STATE_STANDBY;

void NodeCAN::init() {
    hspiCAN.begin(CAN_SCK, CAN_MISO, CAN_MOSI, CAN_CS);
    mcp2515.reset();
    mcp2515.setBitrate(CAN_250KBPS, MCP_8MHZ); 
    mcp2515.setNormalMode();
    currentBusState = STATE_STANDBY;
    Serial.println(F("[CAN Engine] MCP2515 Online (250KBPS)."));
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
        if((2 + i) < 8) txFrame.data[2 + i] = payload[i];
    }
    mcp2515.sendMessage(&txFrame);
}

void NodeCAN::broadcastEndCycle() {
    sendCommand(0x00, 0x02, NULL, 0); 
    Serial.println(F("[CAN Engine] Broadcasted End-of-Cycle (0x02)."));
}

void NodeCAN::requestFreshDiagnostics(uint8_t targetLmpId) {
    sendCommand(targetLmpId, CMD_REQ_DIAG, NULL, 0);
}

void NodeCAN::parseIncomingFrame(struct can_frame& frame) {
    uint32_t rawId = frame.can_id; 
    
    // 1. Structural Sanity Filters
    if (rawId < 1 || rawId > MAX_NODE_ID) return;
    if (frame.can_dlc < 3) return; 
    
    uint8_t instructionId = frame.data[1]; 
    LMPAssemblyBuffer& session = assemblyLine[rawId];

    // 2. Master Instruction Router
    switch (instructionId) {
        
        // ====================================================================
        // 🔍 CASE 1: DISCOVERY SWEEP HANDSHAKE
        // ====================================================================
        case 0x01: 
            if (currentBusState == STATE_COLLECTING_REPLIES || 
                currentBusState == STATE_INIT_DISCOVERY || 
                currentBusState == STATE_RECHECK_WINDOW) {
                
                uint8_t groupType = frame.data[3]; // Group ID packed in Byte 3
                
                uint8_t activeNodes[MAX_NODE_ID];
                uint8_t totalFound = NodeRegistry::getActiveNodesList(activeNodes);
                bool alreadyRegistered = false;
                
                for(int i = 0; i < totalFound; i++) {
                    if (activeNodes[i] == rawId) alreadyRegistered = true;
                }

                if (!alreadyRegistered) {
                    NodeRegistry::registerNode(rawId, groupType);
                    Serial.print(F("🎉 DISCOVERED: Node ")); Serial.print(rawId);
                    Serial.print(F(" mapped into Group ")); Serial.println(groupType);
                }
            }
            break;
            
        // ====================================================================
        // 📊 CASE 2 & 3: ROUTINE TELEMETRY POLL & ASYNC UNPOLLED EVENT OVERRIDES
        // ====================================================================
        case DATA_STREAM: 
        case 0x09: { 
            // Step A: Handle Emergency Fault Registrations Immediately
            if (instructionId == 0x09) {
                Serial.print(F("🚨 EMERGENCY FROM NODE: ")); Serial.println(rawId);
                NodeRegistry::updateNodeError(rawId, 0xFF); 
            }

            // Step B: Extract Segmented Frame Protocol Controls
            uint8_t packetsLeft    = frame.data[2]; // Byte 2 = Sequential frame tracking countdown
            uint8_t dataBytesInMsg = frame.can_dlc - 3; // Strip Target ID, Opcode, and Control Byte
            
            // Step C: Initialize Buffer Staging Layout if this is the initial packet block
            if (!session.inProgress) {
                session.bytesWritten = 0;
                session.expectedNextCount = packetsLeft;
                session.inProgress = true;
            } 
            
            // Step D: Stream data payload directly into localized RAM array
            for (uint8_t i = 0; i < dataBytesInMsg; i++) {
                if (session.bytesWritten < 32) {
                    session.rawPayload[session.bytesWritten] = frame.data[3 + i];
                    session.bytesWritten++;
                }
            }
            
            // Step E: Trigger structural de-quantization only if the terminal frame (0) is complete
            if (packetsLeft == 0) { 
                session.inProgress = false;
                LMPDataRecord nodeInfo;
                
                // Self-Healing Fallback: If discovery handshake dropped out, map node layout dynamically
                if (!NodeRegistry::getNodeSnapshot(rawId, nodeInfo)) {
                    uint8_t defaultGroup = (rawId >= 161) ? 4 : 1;
                    NodeRegistry::registerNode(rawId, defaultGroup);
                    NodeRegistry::getNodeSnapshot(rawId, nodeInfo);
                }

                uint32_t currentRuntimeMs = millis();
                String logLine = String(currentRuntimeMs) + "," + String(rawId) + "," + String(nodeInfo.groupType) + ",";

                // Step F: Group Profile De-quantization Matrix (Fixed-Point to Float)
                switch (nodeInfo.groupType) {
                    
                    case 1: { // PROFILE 1: Single IR Target Subsystem Panel [ObjH, ObjL, AmbH, AmbL]
                        if (session.bytesWritten >= 4) {
                            int16_t rawObj = (session.rawPayload[0] << 8) | session.rawPayload[1];
                            int16_t rawAmb = (session.rawPayload[2] << 8) | session.rawPayload[3];
                            float objectTemp1 = rawObj / 10.0f;
                            float ambientTemp = rawAmb / 10.0f;
                            
                            NodeRegistry::updateTelemetry(rawId, objectTemp1, 0.0f, ambientTemp, 0.0f);
                            logLine += "OBJ1:" + String(objectTemp1, 1) + ";AMB:" + String(ambientTemp, 1) + "\n";
                            if (NodeStorage::isSystemReady) NodeStorage::logStringPacket(logLine);
                        }
                        break;
                    }
                    
                    case 2: { // PROFILE 2: IR Target + High-Precision Environmental Probe [ObjH, ObjL, AmbH, AmbL, Hum]
                        if (session.bytesWritten >= 5) {
                            int16_t rawObj = (session.rawPayload[0] << 8) | session.rawPayload[1];
                            int16_t rawAmb = (session.rawPayload[2] << 8) | session.rawPayload[3];
                            uint8_t rawHum = session.rawPayload[4];
                            float objectTemp1 = rawObj / 10.0f;
                            float ambientTemp = rawAmb / 10.0f;
                            float humidity    = rawHum / 2.0f;
                            
                            NodeRegistry::updateTelemetry(rawId, objectTemp1, 0.0f, ambientTemp, humidity);
                            logLine += "OBJ1:" + String(objectTemp1, 1) + ";AMB:" + String(ambientTemp, 1) + ";RH:" + String(humidity, 1) + "%\n";
                            if (NodeStorage::isSystemReady) NodeStorage::logStringPacket(logLine);
                        }
                        break;
                    }
                    
                    case 3: { // PROFILE 3: Dual-Zone Fragmented Bushing/Transformer Segment Panel
                        if (session.bytesWritten >= 6) {
                            int16_t rawObj1 = (session.rawPayload[0] << 8) | session.rawPayload[1];
                            int16_t rawObj2 = (session.rawPayload[2] << 8) | session.rawPayload[3];
                            int16_t rawAmb  = (session.rawPayload[4] << 8) | session.rawPayload[5];
                            float objectTemp1 = rawObj1 / 10.0f;
                            float objectTemp2 = rawObj2 / 10.0f;
                            float ambientTemp = rawAmb / 10.0f;
                            
                            NodeRegistry::updateTelemetry(rawId, objectTemp1, objectTemp2, ambientTemp, 0.0f);
                            logLine += "PHASE_A:" + String(objectTemp1, 1) + ";PHASE_B:" + String(objectTemp2, 1) + ";SHARED_AMB:" + String(ambientTemp, 1) + "\n";
                            if (NodeStorage::isSystemReady) NodeStorage::logStringPacket(logLine);
                        }
                        break;
                    }
                    
                    case 4: { // PROFILE 4: Remote Actuator Switched Breaker Relay Panels [SwitchMask]
                        if (session.bytesWritten >= 1) {
                            uint8_t switchStatusMask = session.rawPayload[0];
                            NodeRegistry::updateTelemetry(rawId, 0.0f, 0.0f, 0.0f, 0.0f); 
                            logLine += "ACTUATOR_MASK:0x" + String(switchStatusMask, HEX) + "\n";
                            if (NodeStorage::isSystemReady) NodeStorage::logStringPacket(logLine);
                        }
                        break;
                    }
                } // End of de-quantization switch matrix
            } // End of packet completed structural checks
            break;
        } // End of Case DATA_STREAM / 0x09 segment wrapper
        
        // ====================================================================
        // ⚙️ CASE 4: OUT-OF-BAND LIVE ON-DEMAND USER DIAGNOSTICS LINK
        // ====================================================================
        case CMD_REQ_DIAG: { 
            uint8_t freshErrorMask = frame.data[2]; // Target registers packed cleanly into Byte 2
            NodeRegistry::updateNodeError(rawId, freshErrorMask);
            break;
        }
    } // End of master instruction id switch router Matrix
}

void NodeCAN::runNetworkWorker(void* pvParameters) {
    struct can_frame incomingFrame;
    uint32_t recheckStartTime = 0;
    static uint8_t discoveryPulseCount = 0;
    uint32_t lastPollTime = 0;
    uint8_t  pollIndex = 0;

    for (;;) { 
        switch (currentBusState) {
            case STATE_STANDBY:
                break;
                
            case STATE_INIT_DISCOVERY:
                discoveryPulseCount = 0;
                discoveryStartTime = millis(); 
                Serial.println(F("[CAN Engine] Phase 1/4: Launching Discovery..."));
                while (discoveryPulseCount < 3) {
                    sendCommand(0x00, 0x01, NULL, 0);
                    discoveryPulseCount++;
                    vTaskDelay(pdMS_TO_TICKS(100)); 
                }
                currentBusState = STATE_COLLECTING_REPLIES;
                break;
                
            case STATE_COLLECTING_REPLIES:
                if (millis() - discoveryStartTime >= DISCOVERY_WINDOW) {
                    currentBusState = STATE_SEND_ACK_SEQUENTIAL;
                }
                break;
                
            case STATE_SEND_ACK_SEQUENTIAL: {
                uint8_t activeNodes[MAX_NODE_ID];
                uint8_t totalFound = NodeRegistry::getActiveNodesList(activeNodes);
                for (int i = 0; i < totalFound; i++) {
                    sendCommand(activeNodes[i], 0x01, NULL, 0); 
                    vTaskDelay(pdMS_TO_TICKS(15)); 
                }
                broadcastEndCycle(); 
                recheckStartTime = millis();
                currentBusState = STATE_RECHECK_WINDOW;
                break;
            }
                
            case STATE_RECHECK_WINDOW:
                if (millis() - recheckStartTime >= 3000) {
                    NodeRegistry::finalizeDiscoveryRegistry();
                    currentBusState = STATE_OPERATIONAL_MODE;
                    Serial.println(F("[CAN Engine] Polling Mode Active."));
                }
                break;
                
            case STATE_OPERATIONAL_MODE: {
                uint8_t activeNodes[MAX_NODE_ID];
                uint8_t totalFound = NodeRegistry::getActiveNodesList(activeNodes);
                
                if (totalFound > 0 && (millis() - lastPollTime >= currentPollingInterval)) {
                    lastPollTime = millis();
                    if (pollIndex >= totalFound) pollIndex = 0;
                    
                    uint8_t nextTarget = activeNodes[pollIndex];
                    uint8_t runtimeConfigPayload[2] = { 85, 2 }; 
                    sendCommand(nextTarget, 0x04, runtimeConfigPayload, 2); 
                    pollIndex++;
                }
                break;
            }
        }

        if (mcp2515.readMessage(&incomingFrame) == MCP2515::ERROR_OK) {
            parseIncomingFrame(incomingFrame);
        }
        vTaskDelay(pdMS_TO_TICKS(2)); 
    }
}