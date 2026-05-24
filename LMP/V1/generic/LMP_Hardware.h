#ifndef LMP_HARDWARE_H
#define LMP_HARDWARE_H

#include <Arduino.h>

// ==========================================
// 🛠️ HARDWARE PROFILE CONFIGURATION SWITCHES
// ==========================================
#define LMP_ID       14   // Unique device address (1 to 240)
#define LMP_GROUP    2    // 1=1xIR, 2=1xIR+Hum, 3=2xIR, 4=Actuator Relay Board

class LMP_Hardware {
public:
    // Standard Interface Methods
    static void init();
    static void refresh();
    static void packTelemetry(uint8_t* buffer, uint8_t& dlcOut);
    static void executeAction(uint8_t instructionId, uint8_t* payload, uint8_t dlc);
    static uint8_t getErrorCode();

    // Helper to determine if this module is a sensor or an actuator
    static bool isActuator() { return (LMP_ID >= 161 && LMP_ID <= 240); }
};

#endif