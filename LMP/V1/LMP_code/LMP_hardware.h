#ifndef LMP_HARDWARE_H
#define LMP_HARDWARE_H

#include <Arduino.h>

// =========================================================
// 🛠️ DESIGNATED PANEL STRUCTURAL PROFILES
// =========================================================
#define LMP_ID       14   // Unique device identification number (1 to 160)
#define LMP_GROUP    2    // Profile Layout Matrix: 1=1xIR, 2=1xIR+Hum, 3=2xIR, 4=Actuators

class LMP_Hardware {
public:
    static void init();
    static void refresh();
    static void packTelemetry(uint8_t* buffer, uint8_t& dlcOut);
    static void executeAction(uint8_t instructionId, uint8_t* payload, uint8_t dlc);
    static uint8_t getErrorCode();

    static bool isActuator() { return (LMP_ID >= 161 && LMP_ID <= 240); }
};

#endif