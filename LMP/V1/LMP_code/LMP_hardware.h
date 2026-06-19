#ifndef LMP_HARDWARE_H
#define LMP_HARDWARE_H

#include <Arduino.h>
#include <mcp2515.h> // Required so the header knows what the MCP2515 object is

// =========================================================
// 🛠️ DESIGNATED PANEL STRUCTURAL PROFILES
// =========================================================
#define LMP_ID       14   // Unique device identification number (1 to 160)
#define LMP_GROUP    1    // Profile Layout Matrix: 1=1xIR, 2=1xIR+Hum, 3=2xIR, 4=Actuators

class LMP_Hardware {
public:
    static void init();
    static void refresh();
    
    // 🚀 FIXED: Added the declaration so the main loop can access the thermal snapshot
    static float getCoreObjectTemp(); 
    
    static void sendStream(MCP2515& canBus, uint8_t opcode); 
    static float getCoreObjectTemp(); // Returns live float reading to background checking registers
    
    static void executeAction(uint8_t instructionId, uint8_t* payload, uint8_t dlc);
    static uint8_t getErrorCode();

    static bool isActuator() { return (LMP_ID >= 161 && LMP_ID <= 240); }
};

#endif