#include "LMP_Hardware.h"
#include <Wire.h>

// ============================================================================
// 🔥 PROFILE GROUP 1: 1x IR Sensor Only (1 Object Temp, 1 Casing Ambient Temp)
// ============================================================================
#if (LMP_GROUP == 1)
#include "MLX_driver.h"

// Fully isolated variables for Group 1 profile memory space
static float panel_obj1 = 0.0f;
static float panel_amb  = 0.0f;
static uint8_t global_error_register = 0x00; // Bit 0 = Primary MLX Fault

void LMP_Hardware::init() {
    Wire.begin();
    if (!MLX_sens::initMLX(0x5A)) {
        global_error_register |= (1 << 0); // Flag Bit 0 high if missing
    }
    refresh(); 
}

void LMP_Hardware::refresh() {
    bool ir1_fault = false;
    MLX_sens::readMLX(0x5A, panel_obj1, panel_amb, ir1_fault);
    
    if (ir1_fault) global_error_register |= (1 << 0); 
    else global_error_register &= ~(1 << 0);
}

void LMP_Hardware::packTelemetry(uint8_t* buffer, uint8_t& dlcOut) {
    int16_t q_obj1 = (int16_t)(panel_obj1 * 10.0f);
    int16_t q_amb  = (int16_t)(panel_amb  * 10.0f);

    dlcOut = 6; // TargetID(1B) + InstID(1B) + Obj1(2B) + Amb(2B)
    buffer[2] = (q_obj1 >> 8) & 0xFF; 
    buffer[3] = q_obj1 & 0xFF;
    buffer[4] = (q_amb >> 8) & 0xFF;  
    buffer[5] = q_amb & 0xFF;
}

void LMP_Hardware::executeAction(uint8_t instructionId, uint8_t* payload, uint8_t dlc) {
    if (instructionId == 9) {
        Serial.println(F("⚠️ G1 Safety Override Triggered"));
    }
}

uint8_t LMP_Hardware::getErrorCode() {
    return global_error_register;
}

// ============================================================================
// 🔥 PROFILE GROUP 2: 1x IR Sensor + 1x Humidity Probe (Environmental Overwrite)
// ============================================================================
#elif (LMP_GROUP == 2)
#include "MLX_driver.h"
#include "AHT20_driver.h"

// Fully isolated variables for Group 2 profile memory space
static float panel_obj1 = 0.0f;
static float panel_amb  = 0.0f;
static float panel_hum  = 0.0f;
static uint8_t global_error_register = 0x00; // Bit 0 = MLX Fault, Bit 2 = AHT Fault

void LMP_Hardware::init() {
    Wire.begin();
    if (!MLX_sens::initMLX(0x5A))  global_error_register |= (1 << 0);
    if (!AHT20_sens::initAHT())    global_error_register |= (1 << 2);
    refresh(); 
}

void LMP_Hardware::refresh() {
    bool ir1_fault = false;
    bool aht_fault = false;

    // 1. Process Infrared Sensor Metrics
    MLX_sens::readMLX(0x5A, panel_obj1, panel_amb, ir1_fault);
    if (ir1_fault) global_error_register |= (1 << 0); 
    else global_error_register &= ~(1 << 0);

    // 2. Process High-Precision Ambient Metrics
    float clear_temp = 0.0f;
    AHT20_sens::readAHT(clear_temp, panel_hum, aht_fault);
    
    if (aht_fault) {
        global_error_register |= (1 << 2);
    } else {
        global_error_register &= ~(1 << 2);
        panel_amb = clear_temp; // High-precision environmental ambient overwrite rule
    }
}

void LMP_Hardware::packTelemetry(uint8_t* buffer, uint8_t& dlcOut) {
    int16_t q_obj1 = (int16_t)(panel_obj1 * 10.0f);
    int16_t q_amb  = (int16_t)(panel_amb  * 10.0f);
    uint8_t q_hum  = (uint8_t)(panel_hum  * 2.0f);

    dlcOut = 7; // TargetID(1B) + InstID(1B) + Obj1(2B) + Amb(2B) + Hum(1B)
    buffer[2] = (q_obj1 >> 8) & 0xFF; 
    buffer[3] = q_obj1 & 0xFF;
    buffer[4] = (q_amb >> 8) & 0xFF;  
    buffer[5] = q_amb & 0xFF;
    buffer[6] = q_hum;
}

void LMP_Hardware::executeAction(uint8_t instructionId, uint8_t* payload, uint8_t dlc) {
    if (instructionId == 9) {
        Serial.println(F("⚠️ G2 Safety Override Triggered"));
    }
}

uint8_t LMP_Hardware::getErrorCode() {
    return global_error_register;
}

// ============================================================================
// 🔥 PROFILE GROUP 3: Dual-Zone IR Sensors (2 Object Readings, 1 Shared Casing Ambient)
// ============================================================================
#elif (LMP_GROUP == 3)
#include "MLX_driver.h"

// Fully isolated variables for Group 3 profile memory space
static float panel_obj1 = 0.0f;
static float panel_obj2 = 0.0f;
static float panel_amb  = 0.0f;
static uint8_t global_error_register = 0x00; // Bit 0 = IR1 Fault, Bit 1 = IR2 Fault

void LMP_Hardware::init() {
    Wire.begin();
    if (!MLX_sens::initMLX(0x5A)) global_error_register |= (1 << 0);
    // PRECONFIGURATION REMINDER: Sensor 2 must be flashed to address 0x5B in advance!
    if (!MLX_sens::initMLX(0x5B)) global_error_register |= (1 << 1); 
    refresh(); 
}

void LMP_Hardware::refresh() {
    bool ir1_fault = false;
    bool ir2_fault = false;
    float dummy_amb2 = 0.0f;

    MLX_sens::readMLX(0x5A, panel_obj1, panel_amb, ir1_fault);
    MLX_sens::readMLX(0x5B, panel_obj2, dummy_amb2, ir2_fault);
    
    if (ir1_fault) global_error_register |= (1 << 0); else global_error_register &= ~(1 << 0);
    if (ir2_fault) global_error_register |= (1 << 1); else global_error_register &= ~(1 << 1);
}

void LMP_Hardware::packTelemetry(uint8_t* buffer, uint8_t& dlcOut) {
    int16_t q_obj1 = (int16_t)(panel_obj1 * 10.0f);
    int16_t q_obj2 = (int16_t)(panel_obj2 * 10.0f);
    int16_t q_amb  = (int16_t)(panel_amb  * 10.0f);

    dlcOut = 8; // TargetID(1B) + InstID(1B) + Obj1(2B) + Obj2(2B) + Amb(2B)
    buffer[2] = (q_obj1 >> 8) & 0xFF; 
    buffer[3] = q_obj1 & 0xFF;
    buffer[4] = (q_obj2 >> 8) & 0xFF; 
    buffer[5] = q_obj2 & 0xFF;
    buffer[6] = (q_amb >> 8) & 0xFF;  
    buffer[7] = q_amb & 0xFF;
}

void LMP_Hardware::executeAction(uint8_t instructionId, uint8_t* payload, uint8_t dlc) {
    if (instructionId == 9) {
        Serial.println(F("⚠️ G3 Safety Override Triggered"));
    }
}

uint8_t LMP_Hardware::getErrorCode() {
    return global_error_register;
}

// ============================================================================
// 🔥 PROFILE GROUP 4: Custom Actuator Control Block (Actuator Control Space: 161-240)
// ============================================================================
#elif (LMP_GROUP == 4)
const int RELAY_PIN_1 = 5;
const int RELAY_PIN_2 = 6;

void LMP_Hardware::init() {
    pinMode(RELAY_PIN_1, OUTPUT);
    pinMode(RELAY_PIN_2, OUTPUT);
    digitalWrite(RELAY_PIN_1, LOW);
    digitalWrite(RELAY_PIN_2, LOW);
}

void LMP_Hardware::refresh() {
    // Actuators do not sample background sensor registries
}

void LMP_Hardware::packTelemetry(uint8_t* buffer, uint8_t& dlcOut) {
    dlcOut = 4; // TargetID(1B) + InstID(1B) + Relay1(1B) + Relay2(1B)
    buffer[2] = digitalRead(RELAY_PIN_1);
    buffer[3] = digitalRead(RELAY_PIN_2);
}

void LMP_Hardware::executeAction(uint8_t instructionId, uint8_t* payload, uint8_t dlc) {
    if (instructionId == 8) { // Regular Actuate Relay Target Command
        uint8_t relaySelection = payload[2];
        uint8_t relayState     = payload[3];
        int activePin = (relaySelection == 1) ? RELAY_PIN_1 : RELAY_PIN_2;
        digitalWrite(activePin, (relayState == 1) ? HIGH : LOW);
    }
    
    if (instructionId == 9) { // EMERGENCY STOP INTERCEPT LINE
        digitalWrite(RELAY_PIN_1, LOW); // Force hard cut default states instantly
        digitalWrite(RELAY_PIN_2, LOW);
        Serial.println(F("🚨 SYSTEM SHUTDOWN FORCED VIA SUBSTATION BUS OVERRIDE"));
    }
}

uint8_t LMP_Hardware::getErrorCode() {
    return 0x00; // Actuators always return status baseline clear
}

#endif