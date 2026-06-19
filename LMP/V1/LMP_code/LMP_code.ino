#include <SPI.h>
#include <mcp2515.h>
#include "LMP_Hardware.h"

// Instantiate the CAN controller driver on SPI Chip Select hardware Pin 10
MCP2515 mcp2515(10); 

enum SlaveState {
    STATE_IDLE_UNREGISTERED,
    STATE_ACTIVE_BURST,
    STATE_POST_BURST_LISTEN,
    STATE_OPERATIONAL_POLLING
};

SlaveState currentLmpState = STATE_IDLE_UNREGISTERED;
unsigned long windowTimer = 0;
unsigned long transmissionTimer = 0;
unsigned long structuralJitterDelay = 150;
bool registrationConfirmed = false;

// Dynamic configuration registers set across the fieldbus by the Gateway Node
uint8_t dynamicAlertThresholdCelsius = 80;
uint8_t dynamicSamplingIntervalMode = 1;

void setup() {
    Serial.begin(115200);
    
    // 1. Initialize the localized multi-file Hardware Abstraction Layer
    LMP_Hardware::init();

    // 2. Drive SPI core lines high to establish bus communication stability
    SPI.begin();
    
    // 3. Clear MCP2515 system registers and align network transmission frequency
    mcp2515.reset();
    
    // 🚨 CODESYNC: Set to 250KBPS rate to perfectly match the Substation Gateway baseline
    // Note: If you encounter serial quiet periods, modify MCP_8MHZ to MCP_16MHZ depending on crystal
    mcp2515.setBitrate(CAN_250KBPS, MCP_8MHZ); 
    mcp2515.setNormalMode();

    Serial.print(F("🔒 Hybrid Protocol LMP Online. Hardware Profile Assigned ID: "));
    Serial.println(LMP_ID);
}

void loop() {
    struct can_frame rxFrame;

    // 📥 STAGE 1: HARNESSED FIELD BUS FRAME RECEPTION INTERCEPTOR
    if (mcp2515.readMessage(&rxFrame) == MCP2515::ERROR_OK) {
        uint8_t targetId    = rxFrame.data[0]; // Byte 0 dictates recipient destination 
        uint8_t instruction = rxFrame.data[1]; // Byte 1 houses protocol instruction operation code
        
        // Generalized Broadcast Mapping Rule: If Sender ID matches Target ID slot
        bool isBroadcast = (rxFrame.can_id == targetId);

        // Traffic Filter: Process frame only if addressed explicitly to our ID or global broadcast
        if (targetId == LMP_ID || isBroadcast) {
            
            switch (currentLmpState) {
                case STATE_IDLE_UNREGISTERED:
                    if (instruction == 0x01 && isBroadcast) { // Discovery broadcast sweep tracked
                        Serial.println(F("[START] Discovery invite tracked. Entering state matrix burst phase."));
                        registrationConfirmed = false;
                        windowTimer = millis();
                        transmissionTimer = millis();
                        structuralJitterDelay = 150 + random(0, 45); 
                        currentLmpState = STATE_ACTIVE_BURST;
                    }
                    break;

                case STATE_ACTIVE_BURST:
                    // Handled inside passive state-machine emission countdown loops down below...
                    break;

                case STATE_POST_BURST_LISTEN:
                    if (instruction == 0x01 && targetId == LMP_ID) { // Unicast confirmation matches
                        registrationConfirmed = true;
                        Serial.println(F(" -> ACK MATCHED: Central Registry verification locked."));
                    }
                    if (instruction == 0x02 && isBroadcast) { // Global switch command received from Gateway
                        if (registrationConfirmed) {
                            Serial.println(F(">>> SUCCESS: Network Topology Finalized. Operational Polling Layer Active. <<<"));
                            currentLmpState = STATE_OPERATIONAL_POLLING;
                        } else {
                            Serial.println(F(" -> Handshake verification sequence failed. Resetting loop state tracking..."));
                            currentLmpState = STATE_IDLE_UNREGISTERED;
                        }
                    }
                    break;

                case STATE_OPERATIONAL_POLLING:
                    if (instruction == 0x04) { // Routine Polled Telemetry Request
                        // Check if the master passed dynamic runtime settings inside trailing envelope bytes
                        if (rxFrame.can_dlc >= 4) { 
                            dynamicAlertThresholdCelsius = rxFrame.data[2];
                            dynamicSamplingIntervalMode   = rxFrame.data[3];
                        }

                        LMP_Hardware::refresh(); 
                        LMP_Hardware::sendStream(mcp2515, 0x04); // Stream data packet matrix directly back
                    }
                    
                    // 🚀 ON-DEMAND LIVE USER DIAGNOSTICS HANDLER (OPCODE 0x06)
                    // Bypasses cyclic polling pipelines. Fires instantly when operator hits PROG button on gateway.
                    if (instruction == 0x06) {
                        LMP_Hardware::refresh(); // Fresh poll I2C lines to locate real-time physical errors
                        
                        struct can_frame txFrame;
                        txFrame.can_id = LMP_ID;
                        txFrame.can_dlc = 3;
                        txFrame.data[0] = 0x00;           // Route straight to Central Hub Master Core
                        txFrame.data[1] = 0x06;           // Echo diagnostic operation identity reference
                        txFrame.data[2] = LMP_Hardware::getErrorCode(); // Fetch raw bitmask registry (MLX/AHT faults)
                        
                        mcp2515.sendMessage(&txFrame);
                        Serial.println(F("⚙️ Fresh diagnostic status register compiled and dispatched on command."));
                    }
                    
                    if (instruction == 0x09 && isBroadcast) { // Peer-to-Peer Interlock / Override Intercept Trigger
                        LMP_Hardware::executeAction(0x09, rxFrame.data, rxFrame.can_dlc);
                    }
                    break;
            }
        }
    }

    // 🔄 STAGE 2: TIMED EMISSION ARCHITECTURE & VOLATILE SAFETY MARGIN EVALUATIONS
    switch (currentLmpState) {
        case STATE_ACTIVE_BURST:
            // Flood identification blocks back using random jitter distribution slots to evade wire collisions
            if (millis() - transmissionTimer >= structuralJitterDelay) {
                struct can_frame txFrame;
                txFrame.can_id = LMP_ID;
                txFrame.can_dlc = 4;
                txFrame.data[0] = 0x00;        // Address point to Central Hub Master Node
                txFrame.data[1] = 0x01;        // Handshake validation sequence identity signature
                txFrame.data[2] = LMP_ID;
                txFrame.data[3] = LMP_GROUP;
                mcp2515.sendMessage(&txFrame);
                
                structuralJitterDelay = 150 + random(0, 45);
                transmissionTimer = millis();
            }

            if (millis() - windowTimer >= 3000) { // 3-second initialization phase boundary wrap
                currentLmpState = STATE_POST_BURST_LISTEN;
            }
            break;

        case STATE_OPERATIONAL_POLLING: {
            static unsigned long backgroundClock = 0;
            if (millis() - backgroundClock >= 1500) {
                backgroundClock = millis();
                
                if (!LMP_Hardware::isActuator()) {
                    LMP_Hardware::refresh();
                    
                    // ✨ FIXED: Variable names perfectly aligned
                    float currentBoxThermalSnapshot = LMP_Hardware::getCoreObjectTemp(); 
                    
                    if (currentBoxThermalSnapshot >= (float)dynamicAlertThresholdCelsius) {
                        Serial.print(F("🚨 LOCAL MONITOR EVENT FLAGGED: Object Temp Breached dynamic constraint: "));
                        Serial.println(currentBoxThermalSnapshot);
                        
                        LMP_Hardware::sendStream(mcp2515, 0x09); 
                        
                        struct can_frame p2pFrame;
                        p2pFrame.can_id = LMP_ID;
                        p2pFrame.can_dlc = 4;
                        p2pFrame.data[0] = 165;  
                        p2pFrame.data[1] = 0x09; 
                        mcp2515.sendMessage(&p2pFrame);
                    }
                }
            }
            break;
        }
        default:
            break;
    }
}