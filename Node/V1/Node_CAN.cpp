#include "Node_CAN.h"
#include "Node_Registry.h"

// 📦 Define a robust staging footprint for multi-packet reconstruction
struct LMPAssemblyBuffer {
    uint8_t  rawPayload[32];       // Handles up to 32 bytes of compiled sensor data
    uint8_t  bytesWritten;         // Tracks current insertion pointer index
    uint8_t  expectedNextCount;    // Enforces strict descending countdown checking
    bool     inProgress;           // Guards against orphaned mid-stream fragments
    uint8_t  retryCounter;
};

// Allocate an assembly line slot for every possible node on the bus
static LMPAssemblyBuffer assemblyLine[MAX_NODE_ID + 1];

// 🔌 Instantiate static class members
SPIClass NodeCAN::hspiCAN(HSPI); // Use ESP32's secondary hardware SPI bus unit
MCP2515 NodeCAN::mcp2515(CAN_CS);
NetworkState NodeCAN::currentBusState = STATE_STANDBY;

/**
 * @brief Sets up physical SPI lines and configures the MCP2515 registers.
 */
void NodeCAN::init() {
    // 1. Fire up custom hardware SPI pins for the CAN controller
    hspiCAN.begin(CAN_SCK, CAN_MISO, CAN_MOSI, CAN_CS);
    
    // 2. Pass our custom SPI reference into the MCP2515 library configuration
    // (Note: Depending on your exact library version modification, standard libraries 
    // might require passing custom SPI handles or using default SPI. We assume HSPI isolation here)
    mcp2515.reset();
    mcp2515.setBitrate(CAN_250KBPS, MCP_8MHZ); // Industrial standard rate for long cable runs
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
 * @brief Sends a directed confirmation (ACK) frame to a newly discovered LMP.
 */
void NodeCAN::sendUnicastACK(uint8_t targetLmpId) {
    struct can_frame ackFrame;
    ackFrame.can_id = targetLmpId; // Directed back to the LMP's unique ID
    ackFrame.can_dlc = 1;
    ackFrame.data[0] = 0xAC;        // 0xAC code tells the LMP it is verified
    
    mcp2515.sendMessage(&ackFrame);
}

/**
 * @brief Global broadcast command to transition all LMPs from Discovery to Operational mode.
 */
void NodeCAN::broadcastEndCycle() {
    struct can_frame frame;
    frame.can_id = 0x000; // Master Broadcast ID
    frame.can_dlc = 2;
    frame.data[0] = 0x00; // Target ID 0 (All)
    frame.data[1] = 0x02; // Operational Mode Command Code
    
    mcp2515.sendMessage(&frame);
    Serial.println(F("[CAN Engine] Operational mode broadcast transmitted to network."));
}

/**
 * @brief Core decoding engine. Translates raw CAN bytes into true physical engineering units.
 */
void NodeCAN::parseIncomingFrame(struct can_frame& frame) {
    uint32_t rawId = frame.can_id; 
    
    if (rawId < 1 || rawId > MAX_NODE_ID) return;
    if (frame.can_dlc < 3 || frame.data[0] != 0x00) return; // Standard target check
    
    uint8_t instructionId = frame.data[1]; 
    uint8_t packetsLeft   = frame.data[2]; 
    uint8_t dataBytesInMsg = frame.can_dlc - 3; 
    
    LMPAssemblyBuffer& session = assemblyLine[rawId];

    switch (instructionId) {
        
        case 0x04: // 📊 Processing Segmented Data Chunks
            if (currentBusState != STATE_OPERATIONAL_MODE) return;
            
            // Initializing a pristine stream sequence
            if (!session.inProgress) {
                session.bytesWritten = 0;
                session.expectedNextCount = packetsLeft;
                session.inProgress = true;
            } 
            // 🚨 SEQUENCE GAP DETECTED! (e.g., expected 1, but received 0)
            else if (packetsLeft != session.expectedNextCount) {
                
                if (session.retryCounter < MAX_RETRIES_ALLOWED) {
                    session.retryCounter++;
                    session.inProgress = false; // Reset this broken session stream
                    
                    Serial.print(F("[Protocol Recovery] Gap detected on Node "));
                    Serial.print(rawId);
                    Serial.print(F(". Issuing NACK Retry #"));
                    Serial.println(session.retryCounter);
                    
                    // 🚀 Execute Active Error Correction! Request the LMP to resend last data stream
                    sendCommand(rawId, CMD_REQ_RESEND);
                } 
                else {
                    // 🛑 Exhausted all retries. The line is physically too noisy or disconnected.
                    session.inProgress = false;
                    session.retryCounter = 0; // Clear counter for next cycle
                    
                    // Flag a high-level network communication alert bitmask in the registry
                    NodeRegistry::updateNodeError(rawId, 0x04); // 0x04 = Packet Loss Fault
                    
                    Serial.print(F("[CRITICAL FAULT] Node "));
                    Serial.define(rawId);
                    Serial.println(F(" failed retransmission bounds. Tagging Comms Error."));
                }
                return;
            }
            
            // If sequence check passes, stream bytes into staging array as normal
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
                // 🎉 Final segment received successfully!
                session.inProgress = false; 
                session.retryCounter = 0; // Reset retry counter on full completion success
                
                // Execute standard bitwise reconstruction...
                if (session.bytesWritten >= 5) {
                    int16_t rawObj = (session.rawPayload[0] << 8) | session.rawPayload[1];
                    int16_t rawAmb = (session.rawPayload[2] << 8) | session.rawPayload[3];
                    uint8_t rawHum = session.rawPayload[4];
                    
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
    
    // Explicit confirmation loop to watch our task launch state
    Serial.print(F("[CAN Task] Running on Core: "));
    Serial.println(xPortGetCoreID());

    for (;;) { // Infinite RTOS Task Execution Loop
        
        // 🎛️ SYSTEM STATE MACHINE LOGIC
        switch (currentBusState) {
            
            case STATE_STANDBY:
                // Do nothing. Wait for HMI module to change state to trigger discovery.
                break;
                
            case STATE_INIT_DISCOVERY:
                Serial.println(F("[CAN Engine] Launching Network Discovery Broadcast..."));
                
                // Assemble the discovery ping (ID: 0x000, Data: [0x00, 0x01])
                struct can_frame discPing;
                discPing.can_id = 0x000;
                discPing.can_dlc = 2;
                discPing.data[0] = 0x00; // Target: All
                discPing.data[1] = 0x01; // Instruction: Identify Yourshelves
                
                mcp2515.sendMessage(&discPing);
                
                discoveryStartTime = millis();
                currentBusState = STATE_COLLECTING_REPLIES;
                break;
                
            case STATE_COLLECTING_REPLIES:
                // Check if our 3-second discovery time window has run out
                if (millis() - discoveryStartTime >= DISCOVERY_WINDOW) {
                    Serial.println(F("[CAN Engine] Discovery window closed. Sorting data..."));
                    
                    // Sort found items sequentially
                    NodeRegistry::finalizeDiscoveryRegistry();
                    
                    // Order all slave nodes to lock down and start sampling metrics
                    broadcastEndCycle();
                    
                    currentBusState = STATE_OPERATIONAL_MODE;
                }
                break;
                
            case STATE_OPERATIONAL_MODE:
                // Standard streaming state. Task stays here handling telemetry.
                break;
        }

        // 📥 PHYSICAL HARDWARE RX HANDLER
        // Poll the MCP2515 buffer to check if a new frame has dropped into the register
        if (mcp2515.readMessage(&incomingFrame) == MCP2515::ERROR_OK) {
            parseIncomingFrame(incomingFrame);
        }

        // ⏳ THE GOLDEN RTOS RULE: Yield CPU Control
        // Pause this specific task for 2 milliseconds to let the background network stacks breathe
        vTaskDelay(pdMS_TO_TICKS(2));
    }
}