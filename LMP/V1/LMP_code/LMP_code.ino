// LMP code

// #include <SPI.h>
// #include <mcp2515.h>
// #include "LMP_Hardware.h"

// // Instantiate the CAN controller driver on SPI Chip Select hardware Pin 10
// MCP2515 mcp2515(10); 

// void setup() {
//     Serial.begin(115200);
    
//     // 1. Initialize the localized multi-file Hardware Abstraction Layer
//     LMP_Hardware::init();

//     // 2. Drive SPI core lines high to establish bus communication stability
//     SPI.begin();
    
//     // 3. Clear MCP2515 system registers and align network transmission frequency
//     mcp2515.reset();
//     mcp2515.setBitrate(CAN_500KBPS, MCP_8MHZ); // Locked to your 500KBPS field bus requirement
//     mcp2515.setNormalMode();

//     Serial.print(F("🔒 LMP Substation Node Fully Sealed. Listening on Node ID: "));
//     Serial.println(LMP_ID);
// }

// void loop() {
//     struct can_frame rxFrame;

//     // Check physical hardware buffer pipelines for incoming messages
//     if (mcp2515.readMessage(&rxFrame) == MCP2515::ERROR_OK) {
        
//         uint8_t targetId    = rxFrame.data[0]; // Byte 0 dictates recipient destination
//         uint8_t instruction = rxFrame.data[1]; // Byte 1 houses protocol instruction
        
//         // Apply Custom Broadcast Mapping Rule: If Sender ID matches Target ID slot
//         bool isBroadcast = (rxFrame.can_id == targetId);

//         // Substation Traffic Filter: Drop package instantly if it isn't for us or a broadcast
//         if (targetId != LMP_ID && !isBroadcast) return;

//         // Route configuration, test patterns, or physical relay operations to HAL execution blocks
//         if (instruction == 5 || instruction == 6 || instruction == 8 || instruction == 9) {
//             LMP_Hardware::executeAction(instruction, rxFrame.data, rxFrame.can_dlc);
            
//             // Critical Safety Rule: Emergency Stop (9) must pass through to network checks,
//             // all other configuration/actuation instructions return early to protect bandwidth.
//             if (instruction != 9) return; 
//         }

//         // Establish uniform network return configurations
//         struct can_frame txFrame;
//         txFrame.can_id = LMP_ID; // Identify this device as the response sender
//         txFrame.data[0] = 0;      // Route return vector directly back to Heltec Node Master
//         txFrame.data[1] = instruction; // Mirror instruction back to simplify master parser maps

//         // Protocol Tree Execution Branch
//         switch (instruction) {
            
//             case 0: // Master Network Discovery Sweep Request
//                 if (isBroadcast) {
//                     // Let Master map your sensor configuration layout dynamically without code updates
//                     txFrame.data[2] = LMP_GROUP; 
//                     txFrame.can_dlc = 3;
//                     mcp2515.sendMessage(&txFrame);
//                 }
//                 break;

//             case 2: // Asynchronous Performance / Health Poll
//                 txFrame.data[2] = LMP_Hardware::getErrorCode();
//                 txFrame.can_dlc = 3;
//                 mcp2515.sendMessage(&txFrame);
//                 break;

//             case 3: // Force Physical Sampling Conversion Now & Stream
//                 LMP_Hardware::refresh(); // Break out to physical I2C lines for a fresh capture
//                 LMP_Hardware::packTelemetry(txFrame.data, txFrame.can_dlc); // Apply fixed-point math
//                 mcp2515.sendMessage(&txFrame);
//                 break;

//             case 4: // Stream Last Cached Core Values Instantly
//                 // Bypasses sensor read latency by instantly dumping background RAM values
//                 LMP_Hardware::packTelemetry(txFrame.data, txFrame.can_dlc);
//                 mcp2515.sendMessage(&txFrame);
//                 break;
                
//             default:
//                 // Gracefully ignore unknown protocol markers under transient EMI
//                 break;
//         }
//     }

//     // Rugged Non-Blocking Background Maintenance loop
//     // Keeps local sensor cache fresh every 4 seconds to enable instantaneous delivery for Command 4
//     static unsigned long pastSamplingMark = 0;
//     unsigned long currentMark = millis();
    
//     if (currentMark - pastSamplingMark >= 4000) {
//         pastSamplingMark = currentMark;
        
//         // Actuators only modify physical state on command; avoid querying sensor lines
//         if (!LMP_Hardware::isActuator()) {
//             LMP_Hardware::refresh(); 
//         }
//     }
// }


// Diagnostic code

#include <SPI.h>
#include <mcp2515.h>
#include "LMP_Hardware.h"

MCP2515 mcp2515(10); // SPI Chip Select pinned to D10 on Arduino Nano

const unsigned long DIAGNOSTIC_INTERVAL = 3000; // Sample and display metrics every 3 seconds
unsigned long lastDiagnosticMark = 0;

void setup() {
    Serial.begin(115200);
    while (!Serial); // Wait for Serial Monitor line connection
    
    Serial.println(F("=================================================="));
    Serial.println(F("🚀 HPCL INDUSTRIAL LMP STANDALONE BENCH TESTER    "));
    Serial.println(F("=================================================="));

    // 1. Boot Hardware Management Layers
    Serial.print(F("[1/3] Initializing Abstraction HAL Layers... "));
    LMP_Hardware::init();
    Serial.println(F("DONE"));

    // 2. Initialize SPI Core Bus
    Serial.print(F("[2/3] Activating High-Speed SPI Wire Bus... "));
    SPI.begin();
    Serial.println(F("DONE"));

    // 3. Configure CAN Controller Hardware
    Serial.print(F("[3/3] Aligning MCP2515 Protocol Registers... "));
    mcp2515.reset();
    mcp2515.setBitrate(CAN_500KBPS, MCP_8MHZ); // Adjusted for standard 8MHz crystal modules
    mcp2515.setNormalMode();
    Serial.println(F("ONLINE"));

    Serial.println(F("\n--- System Operational Specifications ---"));
    Serial.print(F(" Active Designated Board LMP_ID    : ")); Serial.println(LMP_ID);
    Serial.print(F(" Active Component Assembly GROUP   : ")); Serial.println(LMP_GROUP);
    Serial.println(F("--------------------------------------------------\n"));
}

void loop() {
    unsigned long currentRuntime = millis();

    // Autonomous test sequencer: Simulates master scheduling triggers internally
    if (currentRuntime - lastDiagnosticMark >= DIAGNOSTIC_INTERVAL) {
        lastDiagnosticMark = currentRuntime;

        Serial.println(F("[⚡ START RUNTIME CONVERSION CYCLE]"));

        // Step 1: Force low-level hardware line sweep
        Serial.print(F(" -> Invoking physical register read... "));
        LMP_Hardware::refresh();
        uint8_t faults = LMP_Hardware::getErrorCode();
        Serial.print(F("Active Error Bitmask: 0x")); 
        Serial.println(faults, HEX);

        // Step 2: Assemble localized mock communication frame
        struct can_frame testFrame;
        testFrame.can_id = LMP_ID;
        testFrame.data[0] = 0; // Target: 0 (Heltec Node Gateway master ID)
        testFrame.data[1] = 4; // Sub-command: 4 (Send Last Cached Telemetry Value)

        // Step 3: Package data using Fixed-Point Quantization
        LMP_Hardware::packTelemetry(testFrame.data, testFrame.can_dlc);

        // Step 4: Output raw telemetry stream definitions to serial for inspection
        Serial.print(F(" -> Package Sealed! Packet Frame DLC size: "));
        Serial.println(testFrame.can_dlc);
        
        Serial.print(F(" -> Vector Footprint on Bus: ["));
        for (int i = 0; i < testFrame.can_dlc; i++) {
            Serial.print(F(" 0x"));
            if (testFrame.data[i] < 0x10) Serial.print(F("0")); // Alignment pad logic
            Serial.print(testFrame.data[i], HEX);
        }
        Serial.println(F(" ]"));

        // Step 5: Route frame onto physical differential transceiver lines
        Serial.print(F(" -> Dispatching differential CAN packet... "));
        MCP2515::ERROR status = mcp2515.sendMessage(&testFrame);

        if (status == MCP2515::ERROR_OK) {
            Serial.println(F("SUCCESS"));
        } else {
            Serial.print(F("FAILED (Status: "));
            Serial.print(status);
            Serial.println(F(")"));
            Serial.println(F("    💡 R&D Note: Physical transceivers require electrical termination."));
            Serial.println(F("                 Verify your 120Ω resistors are bridged across CAN-H and CAN-L."));
        }

        Serial.println(F("[🏁 END RUNTIME CONVERSION CYCLE]\n"));
    }
}