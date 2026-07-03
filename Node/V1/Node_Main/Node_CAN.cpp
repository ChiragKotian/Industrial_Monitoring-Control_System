#include "Node_CAN.h"
#include "Node_Registry.h"
#include "Node_Storage.h"
#include "Node_UI.h"
#include <time.h>
#include "Node_LoRa.h"

// 📦 Protocol Definition Layer
#define DATA_STREAM         0x04
#define CMD_REQ_RESEND      0x05
#define CMD_REQ_DIAG        0x06
#define CMD_SET_POLL        0x07
#define CMD_GET_POLL        0x0A
#define MAX_RETRIES_ALLOWED 3

// 🔒 Shared SPI Globals
extern SemaphoreHandle_t hSpiMutex; 
extern SPIClass hspiShared;

// Staging footprint structure for fragmented multi-frame assembly 
struct LMPAssemblyBuffer {
    uint8_t  rawPayload[32];       
    uint8_t  bytesWritten;         
    uint8_t  expectedNextCount;    
    bool     inProgress;           
    uint8_t  retryCounter;         
};

static LMPAssemblyBuffer assemblyLine[MAX_NODE_ID + 1];

// 🔌 Static Member Variables Instantiation
uint16_t NodeCAN::lmpPollRates[MAX_NODE_ID + 1];
uint32_t NodeCAN::discoveryStartTime = 0; 
uint8_t NodeCAN::activeDiagnosticNode = 0; // 🎯 Tracks UI focus state

MCP2515 NodeCAN::mcp2515(CAN_CS, 4000000, &hspiShared); // Initialized with shared bus at 4MHz
NetworkState NodeCAN::currentBusState = STATE_STANDBY;

// ---------------------------------------------------------
// 🔄 SPI ARBITRATION: Switch bus to CAN (Full Re-Init)
// ---------------------------------------------------------
bool NodeCAN::switchToCAN() {
    digitalWrite(SD_CS, HIGH); // Force SD to sleep first
    hspiShared.end();
    delayMicroseconds(50);
    hspiShared.begin(SHARED_SCK, CAN_MISO, SHARED_MOSI, CAN_CS);

    // mcp2515.reset();
    bool ok = (mcp2515.setBitrate(CAN_250KBPS, MCP_8MHZ) == MCP2515::ERROR_OK);
    if (ok) mcp2515.setNormalMode();
    return ok;
}

// ---------------------------------------------------------
// 🧹 OVERFLOW PROTECTION: Drain CAN RX Buffer
// ---------------------------------------------------------
void NodeCAN::drainCanRx() {
    struct can_frame rxFrame;
    while (mcp2515.readMessage(&rxFrame) == MCP2515::ERROR_OK) {
        // If packets built up while SD was writing, we catch them here.
        parseIncomingFrame(rxFrame);
    }
    uint8_t err = mcp2515.getErrorFlags();
    if (err & 0xC0) { // RX0OVR (bit6) | RX1OVR (bit7)
        Serial.print(F("🚨 [CAN Engine] Hardware Buffer Overflow! Clearing... "));
        mcp2515.clearRXnOVRFlags();
        mcp2515.clearMERR();
    }
}

void NodeCAN::init() {
    for(int i = 0; i <= MAX_NODE_ID; i++) lmpPollRates[i] = 4000;

    // We do NOT call hspiShared.begin() here because setup() manages the first mounts.
    currentBusState = STATE_STANDBY;
    Serial.println(F("[CAN Engine] Memory Allocated. Waiting for Network Worker to start."));
}

void NodeCAN::startDiscoveryCycle() {
    currentBusState = STATE_INIT_DISCOVERY;
}

// ⚠️ INTERNAL HELPER: Assumes Mutex is already held by the calling function!
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
    Serial.println(F("[CAN Engine] Broadcasted End-of-Cycle (0x02) to Network."));
}

void NodeCAN::requestFreshDiagnostics(uint8_t targetLmpId) {
    // We must grab the Mutex for asynchronous commands like this
    if (xSemaphoreTake(hSpiMutex, pdMS_TO_TICKS(500))) {
        if (switchToCAN()) {
            digitalWrite(CAN_CS, LOW);
            sendCommand(targetLmpId, CMD_REQ_DIAG, NULL, 0);
            digitalWrite(CAN_CS, HIGH);
        }
        xSemaphoreGive(hSpiMutex);
        Serial.print(F("[CAN Engine] Force-Polled Diagnostic Byte from Node ID: "));
        Serial.println(targetLmpId);
    }
}

void NodeCAN::requestPollRate(uint8_t targetLmpId) {
    if (xSemaphoreTake(hSpiMutex, pdMS_TO_TICKS(500))) {
        if (switchToCAN()) {
            digitalWrite(CAN_CS, LOW);
            sendCommand(targetLmpId, CMD_GET_POLL, NULL, 0);
            digitalWrite(CAN_CS, HIGH);
        }
        xSemaphoreGive(hSpiMutex);
    }
}

void NodeCAN::setPollRate(uint8_t targetLmpId, uint16_t intervalMs) {
    uint8_t payload[2];
    uint16_t q_rate = intervalMs / 100;
    payload[0] = (q_rate >> 8) & 0xFF;
    payload[1] = q_rate & 0xFF;
    
    if (xSemaphoreTake(hSpiMutex, pdMS_TO_TICKS(500))) {
        if (switchToCAN()) {
            digitalWrite(CAN_CS, LOW);
            sendCommand(targetLmpId, CMD_SET_POLL, payload, 2);
            digitalWrite(CAN_CS, HIGH);
        }
        xSemaphoreGive(hSpiMutex);
    }
}

void NodeCAN::parseIncomingFrame(struct can_frame& frame) {
    uint32_t rawId = frame.can_id; 
    Serial.println(frame.data[1]);
    
    if (rawId < 1 || rawId > MAX_NODE_ID) return;
    if (frame.can_dlc < 3) return; 
    
    uint8_t instructionId = frame.data[1]; 
    LMPAssemblyBuffer& session = assemblyLine[rawId];

    switch (instructionId) {
        
        case 0x01: 
            if (currentBusState == STATE_COLLECTING_REPLIES || 
                currentBusState == STATE_INIT_DISCOVERY || 
                currentBusState == STATE_RECHECK_WINDOW) {
                
                uint8_t groupType = frame.data[3]; 
                
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
            
        case DATA_STREAM: 
        case 0x09: { 
            if (instructionId == 0x09) {
                Serial.print(F("🚨 EMERGENCY FROM NODE: ")); Serial.println(rawId);
                NodeRegistry::updateNodeError(rawId, 0xFF); 
            }

            uint8_t packetsLeft    = frame.data[2]; 
            uint8_t dataBytesInMsg = frame.can_dlc - 3; 
            
            if (!session.inProgress) {
                session.bytesWritten = 0;
                session.expectedNextCount = packetsLeft;
                session.inProgress = true;
            } 
            
            for (uint8_t i = 0; i < dataBytesInMsg; i++) {
                if (session.bytesWritten < 32) {
                    session.rawPayload[session.bytesWritten] = frame.data[3 + i];
                    session.bytesWritten++;
                }
            }
            
            if (packetsLeft == 0) { 
                session.inProgress = false;
                LMPDataRecord nodeInfo;
                
                if (!NodeRegistry::getNodeSnapshot(rawId, nodeInfo)) {
                    uint8_t defaultGroup = (rawId >= 161) ? 4 : 1;
                    NodeRegistry::registerNode(rawId, defaultGroup);
                    NodeRegistry::getNodeSnapshot(rawId, nodeInfo);
                }

                // 🕒 GENERATE THE INDUSTRIAL CSV TIMESTAMP
                struct tm timeinfo;
                String timeStamp = String(millis()); 
                if(getLocalTime(&timeinfo, 10)) {
                    char dtBuff[25];
                    strftime(dtBuff, sizeof(dtBuff), "%Y-%m-%d %H:%M:%S", &timeinfo);
                    timeStamp = String(dtBuff);
                }

                String logLine = timeStamp + "," + String(rawId) + "," + String(nodeInfo.groupType) + ",";
                switch (nodeInfo.groupType) {
                    case 1: { 
                        if (session.bytesWritten >= 4) {
                            int16_t rawObj = (session.rawPayload[0] << 8) | session.rawPayload[1];
                            int16_t rawAmb = (session.rawPayload[2] << 8) | session.rawPayload[3];
                            float objectTemp1 = rawObj / 10.0f;
                            float ambientTemp = rawAmb / 10.0f;
                            
                            NodeRegistry::updateTelemetry(rawId, objectTemp1, 0.0f, ambientTemp, 0.0f);
                            logLine += "OBJ1:" + String(objectTemp1, 1) + ";AMB:" + String(ambientTemp, 1) + "\n";
                            if (NodeStorage::isSystemReady) NodeStorage::logStringPacket(logLine);
                            NodeLoRa::transmitTelemetry(logLine);
                        }
                        break;
                    }
                    
                    case 2: { 
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
                            NodeLoRa::transmitTelemetry(logLine);
                        }
                        break;
                    }
                    
                    case 3: { 
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
                            NodeLoRa::transmitTelemetry(logLine);
                        }
                        break;
                    }
                    
                    case 4: { 
                        if (session.bytesWritten >= 1) {
                            uint8_t switchStatusMask = session.rawPayload[0];
                            NodeRegistry::updateTelemetry(rawId, 0.0f, 0.0f, 0.0f, 0.0f); 
                            logLine += "ACTUATOR_MASK:0x" + String(switchStatusMask, HEX) + "\n";
                            if (NodeStorage::isSystemReady) NodeStorage::logStringPacket(logLine);
                            NodeLoRa::transmitTelemetry(logLine);
                        }
                        break;
                    }
                } 
            } 
            break;
        } 
        
        case CMD_REQ_DIAG: { 
            uint8_t freshErrorMask = frame.data[2]; 
            NodeRegistry::updateNodeError(rawId, freshErrorMask);
            
            Serial.print(F("[CAN Diagnostic] Fresh Diagnostic Register Sync from Node "));
            Serial.print(rawId); Serial.print(F(" -> Mask Byte: 0x"));
            Serial.println(freshErrorMask, HEX);
            break;
        }

        case CMD_GET_POLL: { 
            uint16_t fetchedRate = (frame.data[2] << 8 | frame.data[3]) * 100;
            lmpPollRates[rawId] = fetchedRate;
            
            NodeUI::updateLivePollRate(rawId, fetchedRate);
            
            Serial.print(F("[CAN Config] LMP ")); Serial.print(rawId);
            Serial.print(F(" Streaming Rate Confirmed: ")); 
            Serial.print(fetchedRate); Serial.println(F(" ms"));
            break;
        }
    } 
}

// ---------------------------------------------------------
// 🧠 MAIN FREE-RTOS NETWORK WORKER TASK
// ---------------------------------------------------------
void NodeCAN::runNetworkWorker(void* pvParameters) {
    uint32_t recheckStartTime = 0;
    static uint8_t discoveryPulseCount = 0;

    for (;;) { 
        // 2. Drain all waiting messages from the MCP2515 hardware buffer
                drainCanRx();
        // 🛑 GRAB THE SPI MUTEX FOR THE ENTIRE CYCLE
        // This ensures CAN has uninterrupted access to the hardware for both State logic and RX draining.
        if (xSemaphoreTake(hSpiMutex, pdMS_TO_TICKS(500))) {
            
            // 🔄 Safely hop the physical pins to the CAN transceiver
            if (switchToCAN()) {
                digitalWrite(CAN_CS, LOW); // Wake CAN hardware
                
                // 1. Process State Machine Logic
                switch (currentBusState) {
                    case STATE_STANDBY:
                        break;
                        
                    case STATE_INIT_DISCOVERY:
                        discoveryPulseCount = 0;
                        discoveryStartTime = millis(); 
                        Serial.println(F("[CAN Engine] Phase 1/4: Flooding bus with network discovery requests..."));
                        while (discoveryPulseCount < 3) {
                            sendCommand(0x00, 0x01, NULL, 0);
                            discoveryPulseCount++;
                            vTaskDelay(pdMS_TO_TICKS(100)); // We yield inside the while loop to prevent watchdog panics
                        }
                        currentBusState = STATE_COLLECTING_REPLIES;
                        break;
                        
                    case STATE_COLLECTING_REPLIES:
                        if (millis() - discoveryStartTime >= DISCOVERY_WINDOW) {
                            Serial.println(F("[CAN Engine] Phase 2/4 Closed. Shifting to Sequential Ack Validation."));
                            currentBusState = STATE_SEND_ACK_SEQUENTIAL;
                        }
                        break;
                        
                    case STATE_SEND_ACK_SEQUENTIAL: {
                        uint8_t activeNodes[MAX_NODE_ID];
                        uint8_t totalFound = NodeRegistry::getActiveNodesList(activeNodes);
                        
                        Serial.print(F("[CAN Engine] Phase 3/4: Issuing unicast confirmations to ")); 
                        Serial.print(totalFound); Serial.println(F(" nodes..."));
                        
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
                            Serial.println(F("[CAN Engine] Phase 4/4 Complete: Topology verified stable. Locking Operational Listening."));
                            NodeRegistry::finalizeDiscoveryRegistry();
                            currentBusState = STATE_OPERATIONAL_MODE;
                        }
                        break;
                        
                    case STATE_OPERATIONAL_MODE: {
                        static uint32_t lastFocusedPollTime = 0;
                        if (activeDiagnosticNode > 0 && (millis() - lastFocusedPollTime >= 2000)) {
                            lastFocusedPollTime = millis();
                            sendCommand(activeDiagnosticNode, CMD_REQ_DIAG, NULL, 0); 
                        }
                        break;
                    }
                }

                
                
                digitalWrite(CAN_CS, HIGH); // Sleep CAN hardware
            } else {
                Serial.println(F("🚨 [CAN Engine] Hardware Resync Failed. Check Wiring."));
            }
            
            // 🟢 Release the SPI bus so the Storage Task can flush its batch if needed
            xSemaphoreGive(hSpiMutex); 
        }
        
        // Yield heavily to allow other tasks (like UI and LoRa) to process
        vTaskDelay(pdMS_TO_TICKS(20)); 
    }
}