#include "Node_CAN.h"
#include "Node_Registry.h"

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
    
    // Check if it's a standard data frame from a valid LMP range (1 to 240)
    if (rawId >= 1 && rawId <= MAX_NODE_ID) {
        
        // CASE A: Processing a response during Discovery Mode
        if (currentBusState == STATE_COLLECTING_REPLIES) {
            // Check if the packet is explicitly targeted to the Hub
            if (frame.can_dlc >= 3 && frame.data[0] == 0x00) { 
                
                // Check if the instruction code is a Discovery Response (0x01)
                if (frame.data[1] == 0x01) {
                    
                    // ✅ Corrected: Extract additional info from Byte 2 per your standard
                    uint8_t groupType = frame.data[2]; 
                    
                    // Register unit in our thread-safe system registry
                    NodeRegistry::registerNode(rawId, groupType);
                    
                    // Acknowledge by sending the Operational Mode command (0x02)
                    sendCommand(rawId, 0x02); 
                }   
            }
        }
        // CASE B: Processing streaming metrics during Operational Mode
        else if (currentBusState == STATE_OPERATIONAL_MODE) {
            if (frame.can_dlc == 8) { // Expecting full 8-byte telemetry payload
                
                // Extract and reverse the mathematical fixed-point values back into floats
                // Formula: Original float = (HighByte << 8 | LowByte) / 100.0
                int16_t rawObj1 = (frame.data[0] << 8) | frame.data[1];
                int16_t rawObj2 = (frame.data[2] << 8) | frame.data[3];
                int16_t rawAmb  = (frame.data[4] << 8) | frame.data[5];
                uint16_t rawHum = (frame.data[6] << 8) | frame.data[7];
                
                float objTemp1  = rawObj1 / 100.0f;
                float objTemp2  = rawObj2 / 100.0f;
                float ambient   = rawAmb  / 100.0f;
                float humidity  = rawHum  / 100.0f;
                
                // Direct injection into our thread-safe system registry
                NodeRegistry::updateTelemetry(rawId, objTemp1, objTemp2, ambient, humidity);
            }
        }
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