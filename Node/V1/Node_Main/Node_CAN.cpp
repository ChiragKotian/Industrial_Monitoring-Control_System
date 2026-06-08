#include "Node_CAN.h"
#include "Node_Registry.h"
#include "Node_Storage.h"

// extern TaskHandle_t xCanTaskHandle;

// 📦 Define local constants if not explicitly loaded from headers
#define CMD_REQ_RESEND      0x05
#define MAX_RETRIES_ALLOWED 3

//  Staging footprint array matching sender can_id slots exactly
struct LMPAssemblyBuffer {
    uint8_t  rawPayload[32];       // Handles up to 32 bytes of compiled sensor data
    uint8_t  bytesWritten;         // Tracks current insertion pointer index
    uint8_t  expectedNextCount;    // Enforces strict descending countdown checking
    bool     inProgress;           // Guards against orphaned mid-stream fragments
    uint8_t  retryCounter;         // Fault-tolerance tracking engine
};

// Allocate static tracking structures directly to avoid heap fragmentation
static LMPAssemblyBuffer assemblyLine[MAX_NODE_ID + 1];

// 🔌 Instantiate static class variables
SPIClass NodeCAN::hspiCAN(HSPI); 
MCP2515 NodeCAN::mcp2515(CAN_CS);
NetworkState NodeCAN::currentBusState = STATE_STANDBY;

/**
 * @brief Sets up physical SPI lines and configures the MCP2515 registers.
 */
void NodeCAN::init() {
    hspiCAN.begin(CAN_SCK, CAN_MISO, CAN_MOSI, CAN_CS);
    
    mcp2515.reset();
    mcp2515.setBitrate(CAN_250KBPS, MCP_8MHZ); // Optimized industrial baseline
    mcp2515.setNormalMode();
    
    currentBusState = STATE_STANDBY;
    Serial.println(F("[CAN Engine] MCP2515 Hardware Initialized Successfully."));
}

/**
 * @brief Triggers the non-blocking network discovery sequence.
 */
void NodeCAN::startDiscoveryCycle() {
    currentBusState = STATE_INIT_DISCOVERY;
}

/**
 * @brief Unified Command Dispatcher matching your standard protocol rules:
 * Byte 0 = Target destination node ID
 * Byte 1 = Instruction code
 * Byte 2+ = Additional data payloads
 */
void NodeCAN::sendCommand(uint8_t targetId, uint8_t instructionId, uint8_t* payload, uint8_t dlc) {
    struct can_frame txFrame;
    txFrame.can_id = 0x000;          // Transmitter is always Central Hub (0x000)
    txFrame.can_dlc = 2 + dlc;       // Overhead bytes (2) + any extra info trailing
    txFrame.data[0] = targetId;      // 🎯 Byte 0: Destination
    txFrame.data[1] = instructionId; // ⚙️ Byte 1: Instruction
    
    // Copy any optional initialization arrays safely into the packet payload stream
    for(uint8_t i = 0; i < dlc; i++) {
        if((2 + i) < 8) {
            txFrame.data[2 + i] = payload[i];
        }
    }
    mcp2515.sendMessage(&txFrame);
}

/**
 * @brief Global broadcast command to transition all LMPs from Discovery to Operational mode.
 */
void NodeCAN::broadcastEndCycle() {
    // Target = 0x00 (All), Instruction = 0x02 (Operational Mode Shift)
    sendCommand(0x00, 0x02, NULL, 0);
    Serial.println(F("[CAN Engine] Operational mode broadcast transmitted to network."));
}

/**
 * @brief Core decoding engine. Translates raw CAN bytes into true physical engineering units.
 */
void NodeCAN::parseIncomingFrame(struct can_frame& frame) {
uint32_t rawId = frame.can_id; 
    
    // Validate standard tracking range
    if (rawId < 1 || rawId > MAX_NODE_ID) {
        Serial.print(F("[CAN Diagnostic] Rejected Node ID Out of Bounds: "));
        Serial.println(rawId);
        return;
    }
    
    // Standard Layout Enforcer
    if (frame.can_dlc < 3 || frame.data[0] != 0x00) {
        Serial.print(F("[CAN Diagnostic] Frame rejected on overhead layouts from ID: "));
        Serial.println(rawId);
        return;
    } 
    
    uint8_t instructionId  = frame.data[1]; 
    LMPAssemblyBuffer& session = assemblyLine[rawId];

    switch (instructionId) {
        
        // 🔍 CASE 1: Processing responses during Discovery Phase
        case 0x01:
            Serial.print(F("[CAN Diagnostic] Discovery Packet received from Node: "));
            Serial.print(rawId);
            Serial.print(F(" | Current Bus State: "));
            Serial.println(currentBusState); // Enforces tracking verification
            
            if (currentBusState == STATE_COLLECTING_REPLIES) {
                uint8_t groupType = frame.data[2]; 
                NodeRegistry::registerNode(rawId, groupType);
                
                Serial.print(F("🎉 SUCCESS: Node "));
                Serial.print(rawId);
                Serial.print(F(" registered into Group "));
                Serial.println(groupType);
                
                sendCommand(rawId, 0x02, NULL, 0); 
            } else {
                Serial.print(F("❌ REJECTED: Discovery packet ignored because bus state is not COLLECTING_REPLIES\n"));
            }
            break;
            
        // 📊 CASE 2: Processing Segmented Data Telemetry Streams
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
                
                // 🕵️‍♂️ Step 1: Query the Thread-Safe Registry to find out who this node is
                LMPDataRecord nodeInfo;
                if (NodeRegistry::getNodeSnapshot(rawId, nodeInfo)) {

                    // 🚀 STEP 1.5: Ship raw bytes straight to the background SD card logging queue!
                    // This is completely polymorphic. It hands off the raw buffer payload and length
                    // without doing any math here on Core 1.
                    NodeStorage::queueRawRow(rawId, nodeInfo.groupType, session.rawPayload, session.bytesWritten);
                    
                    // 🎛️ Step 2: Branch the parsing logic dynamically based on the verified Group Type
                    switch (nodeInfo.groupType) {
                        
                        // ==========================================================
                        // 🌡️ GROUP 1: Single Infrared Subsystem Panel
                        // Expected buffer size: 4 bytes -> [ObjHigh, ObjLow, AmbHigh, AmbLow]
                        // ==========================================================
                        case 1: {
                            if (session.bytesWritten >= 4) {
                                int16_t rawObj  = (session.rawPayload[0] << 8) | session.rawPayload[1];
                                int16_t rawAmb  = (session.rawPayload[2] << 8) | session.rawPayload[3];
                                
                                float objectTemp1 = rawObj / 10.0f;
                                float ambientTemp = rawAmb / 10.0f;
                                
                                // Group 1 does not possess a secondary IR target or a humidity sensor (defaults to 0.0f)
                                NodeRegistry::updateTelemetry(rawId, objectTemp1, 0.0f, ambientTemp, 0.0f);
                                
                                Serial.print(F("[Unified Parser] GRP 1 Node "));
                                Serial.print(rawId);
                                Serial.print(F(" -> Obj1: ")); Serial.print(objectTemp1, 1);
                                Serial.print(F("C | Amb: ")); Serial.print(ambientTemp, 1);
                                Serial.println(F("C"));
                            }
                            break;
                        }

                        // ==========================================================
                        // 🌡️ GROUP 2: Infrared + Environmental Subsystem (Bench Setup)
                        // Expected buffer size: 5 bytes -> [ObjHigh, ObjLow, AmbHigh, AmbLow, HumRaw]
                        // ==========================================================
                        case 2: {
                            if (session.bytesWritten >= 5) {
                                int16_t rawObj  = (session.rawPayload[0] << 8) | session.rawPayload[1];
                                int16_t rawAmb  = (session.rawPayload[2] << 8) | session.rawPayload[3];
                                uint8_t rawHum  = session.rawPayload[4];
                                
                                float objectTemp1 = rawObj / 10.0f;
                                float ambientTemp = rawAmb / 10.0f;
                                float humidity    = rawHum / 2.0f; 
                                
                                NodeRegistry::updateTelemetry(rawId, objectTemp1, 0.0f, ambientTemp, humidity);
                                
                                Serial.print(F("[Unified Parser] GRP 2 Node "));
                                Serial.print(rawId);
                                Serial.print(F(" -> Obj1: ")); Serial.print(objectTemp1, 1);
                                Serial.print(F("C | Amb: ")); Serial.print(ambientTemp, 1);
                                Serial.print(F("C | Hum: ")); Serial.print(humidity, 1);
                                Serial.println(F("%"));
                            }
                            break;
                        }
                            
                        // ==========================================================
                        // 🌡️ GROUP 3: Dual-Zone Infrared Subsystem Panel
                        // Expected buffer size: 6 bytes -> [Obj1H, Obj1L, Obj2H, Obj2L, AmbH, AmbL]
                        // ==========================================================
                        case 3: {
                            if (session.bytesWritten >= 6) {
                                int16_t rawObj1 = (session.rawPayload[0] << 8) | session.rawPayload[1];
                                int16_t rawObj2 = (session.rawPayload[2] << 8) | session.rawPayload[3];
                                int16_t rawAmb  = (session.rawPayload[4] << 8) | session.rawPayload[5];
                                
                                float objectTemp1 = rawObj1 / 10.0f;
                                float objectTemp2 = rawObj2 / 10.0f;
                                float ambientTemp = rawAmb  / 10.0f;
                                
                                // Pass both physical object temperatures safely to the registry memory slots
                                NodeRegistry::updateTelemetry(rawId, objectTemp1, objectTemp2, ambientTemp, 0.0f);
                                
                                Serial.print(F("[Unified Parser] GRP 3 Node "));
                                Serial.print(rawId);
                                Serial.print(F(" -> Obj1: ")); Serial.print(objectTemp1, 1);
                                Serial.print(F("C | Obj2: ")); Serial.print(objectTemp2, 1);
                                Serial.print(F("C | Amb: ")); Serial.print(ambientTemp, 1);
                                Serial.println(F("C"));
                            }
                            break;
                        }
                            
                        // ==========================================================
                        // 🎚️ GROUP 4: Actuator & Relay Control Status Panel
                        // ==========================================================
                        case 4: {
                            if (session.bytesWritten >= 5) {
                                uint8_t switchStatusMask = session.rawPayload[0]; 
                                uint8_t breakerFeedback  = session.rawPayload[1];
                                uint8_t interlockState   = session.rawPayload[2];
                                uint8_t warningRegister  = session.rawPayload[3];
                                uint8_t localCounter     = session.rawPayload[4];
                                
                                NodeRegistry::updateNodeError(rawId, warningRegister);
                                
                                Serial.print(F("[Unified Parser] GRP 4 Node "));
                                Serial.print(rawId);
                                Serial.print(F(" Switch mask: 0x"));
                                Serial.println(switchStatusMask, HEX);
                            }
                            break;
                        }
                            
                        // ==========================================================
                        // 📈 GROUP 5: Future Expansion (e.g., Vibration / Current Transformers)
                        // ==========================================================
                        case 5:
                            // Future engineers can drop their specialized conversion math right here
                            break;
                            
                        default:
                            Serial.print(F("[Protocol Error] Unknown Group Type "));
                            Serial.print(nodeInfo.groupType);
                            Serial.print(F(" for Node ID: "));
                            Serial.println(rawId);
                            break;
                    }
                }
            }
            break;
        }   
        default:
            break;
    }
}

/**
 * @brief The Continuous Execution Thread run by the FreeRTOS Scheduler on Core 1.
 */
void NodeCAN::runNetworkWorker(void* pvParameters) {
    struct can_frame incomingFrame;
    uint32_t discoveryStartTime = 0;
    
    Serial.print(F("[CAN Task] Running on Core: "));
    Serial.println(xPortGetCoreID());

    for (;;) { 
        // 🎛️ SYSTEM STATE MACHINE CONTROL INTERACTION
        switch (currentBusState) {
            case STATE_STANDBY:
                break;
                
            case STATE_INIT_DISCOVERY:
                Serial.println(F("[CAN Engine] Launching Network Discovery Broadcast..."));
                
                // Target: 0x00 (All Devices), Instruction: 0x01 (Identify Yourself)
                sendCommand(0x00, 0x01, NULL, 0);
                
                discoveryStartTime = millis();
                currentBusState = STATE_COLLECTING_REPLIES;
                break;
                
            case STATE_COLLECTING_REPLIES:
                if (millis() - discoveryStartTime >= DISCOVERY_WINDOW) {
                    Serial.println(F("[CAN Engine] Discovery window closed. Sorting data..."));
                    
                    NodeRegistry::finalizeDiscoveryRegistry();
                    broadcastEndCycle(); // Tell them to shift to Operational mode
                    
                    currentBusState = STATE_OPERATIONAL_MODE;
                }
                break;
                
            case STATE_OPERATIONAL_MODE:
                break;
        }

        // 📥 HARDWARE RX STORAGE EXTRACTION LAYER
        if (mcp2515.readMessage(&incomingFrame) == MCP2515::ERROR_OK) {
            parseIncomingFrame(incomingFrame);
        }

        // ⏳ Yield block thread to background operations for 2 ticks
        vTaskDelay(pdMS_TO_TICKS(2));
    }
}