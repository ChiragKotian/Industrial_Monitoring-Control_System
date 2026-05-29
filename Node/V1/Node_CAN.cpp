#include "Node_CAN.h"
#include "Node_Registry.h"

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
    if (rawId < 1 || rawId > MAX_NODE_ID) return;
    
    // 🛡️ Standard Layout Enforcer: All frames must target the Central Hub (data[0] == 0x00)
    if (frame.can_dlc < 2 || frame.data[0] != 0x00) return; 
    
    uint8_t instructionId  = frame.data[1]; 
    
    // Pull references to our isolated sequence tracker table slot
    LMPAssemblyBuffer& session = assemblyLine[rawId];

    switch (instructionId) {
        
        // 🔍 CASE 1: Processing responses during Discovery Phase
        case 0x01:
            if (currentBusState == STATE_COLLECTING_REPLIES && frame.can_dlc >= 3) {
                // Per protocol standard: additional metadata fields trail after instruction index
                uint8_t groupType = frame.data[2]; 
                
                NodeRegistry::registerNode(rawId, groupType);
                
                // Send immediate validation command acknowledging this specific LMP (CMD: 0x02)
                sendCommand(rawId, 0x02, NULL, 0); 
            }
            break;
            
        // 📊 CASE 2: Processing Segmented Data Telemetry Streams
        case 0x04:
            if (currentBusState != STATE_OPERATIONAL_MODE) return;
            if (frame.can_dlc < 3) return; // Ensure countdown indicator exists
            
            uint8_t packetsLeft    = frame.data[2]; 
            uint8_t dataBytesInMsg = frame.can_dlc - 3; 
            
            // Step A: Initialize tracker state on initial segment entry
            if (!session.inProgress) {
                session.bytesWritten = 0;
                session.expectedNextCount = packetsLeft;
                session.inProgress = true;
            } 
            // 🚨 Step B: Identify and trap broken segment progressions (EMI Protection)
            else if (packetsLeft != session.expectedNextCount) {
                if (session.retryCounter < MAX_RETRIES_ALLOWED) {
                    session.retryCounter++;
                    session.inProgress = false; // Reset broken window buffer state
                    
                    Serial.print(F("[Protocol Recovery] Gap detected on Node "));
                    Serial.print(rawId);
                    Serial.print(F(". Issuing NACK Retry #"));
                    Serial.println(session.retryCounter);
                    
                    // 🚀 Execute Active ARQ Correction request loop
                    uint8_t dummy = 0;
                    sendCommand(rawId, CMD_REQ_RESEND, &dummy, 0);
                } 
                else {
                    // 🛑 Exceeded retry window ceiling limit. Stop traffic generation.
                    session.inProgress = false;
                    session.retryCounter = 0; 
                    
                    NodeRegistry::updateNodeError(rawId, 0x04); // Flag telemetry loss context
                    
                    Serial.print(F("[CRITICAL FAULT] Node "));
                    Serial.print(rawId); // ✅ Bug Fixed: Replaced compilation breaker .define()
                    Serial.println(F(" failed retransmission bounds. Tagging Comms Error."));
                }
                return;
            }
            
            // Step C: Push bytes into structural segment buffer array
            for (uint8_t i = 0; i < dataBytesInMsg; i++) {
                if (session.bytesWritten < 32) {
                    session.rawPayload[session.bytesWritten] = frame.data[3 + i];
                    session.bytesWritten++;
                }
            }
            
            // Step D: Calculate step validation target counts
            if (packetsLeft > 0) {
                session.expectedNextCount--; 
            } 
            // Step E: Target Zero Hit! Sequence execution complete. Unpack database numbers.
            else {
                session.inProgress = false; 
                session.retryCounter = 0; // Clear error tracker on transmission success
                
                if (session.bytesWritten >= 5) {
                    int16_t rawObj = (session.rawPayload[0] << 8) | session.rawPayload[1];
                    int16_t rawAmb = (session.rawPayload[2] << 8) | session.rawPayload[3];
                    uint8_t rawHum = session.rawPayload[4];
                    
                    // Decode structural formats accurately matching physical quantizations
                    float objectTemp1 = rawObj / 10.0f;
                    float ambientTemp = rawAmb / 10.0f;
                    float humidity    = rawHum / 2.0f;
                    
                    NodeRegistry::updateTelemetry(rawId, objectTemp1, 0.0f, ambientTemp, humidity);
                }
            }
            break;
            
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