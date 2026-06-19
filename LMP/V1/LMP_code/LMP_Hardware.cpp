#include "LMP_Hardware.h"
#include <Wire.h>

// ============================================================================
// 🔥 PROFILE GROUP 1: 1x IR Sensor Only
// ============================================================================
#if (LMP_GROUP == 1)
#include "MLX_driver.h"

static float panel_obj1 = 0.0f;
static float panel_amb  = 0.0f;
static uint8_t global_error_register = 0x00;

void LMP_Hardware::init() {
    Wire.begin();
    if (!MLX_sens::initMLX(0x5A)) global_error_register |= (1 << 0);
    refresh(); 
}

float LMP_Hardware::getCoreObjectTemp() { return panel_obj1; }

void LMP_Hardware::refresh() {
    bool ir1_fault = false;
    MLX_sens::readMLX(0x5A, panel_obj1, panel_amb, ir1_fault);
    if (ir1_fault) global_error_register |= (1 << 0); 
    else global_error_register &= ~(1 << 0);
}

void LMP_Hardware::sendStream(MCP2515& canBus, uint8_t opcode) {
    struct can_frame frame;
    frame.can_id = LMP_ID;
    frame.can_dlc = 7;
    frame.data[0] = 0x00;   // Target: Gateway
    frame.data[1] = opcode; 
    frame.data[2] = 0x00;   // PacketsLeft = 0 (Terminal Frame)

    int16_t q_obj1 = (int16_t)(panel_obj1 * 10.0f);
    int16_t q_amb  = (int16_t)(panel_amb  * 10.0f);

    frame.data[3] = (q_obj1 >> 8) & 0xFF; 
    frame.data[4] = q_obj1 & 0xFF;
    frame.data[5] = (q_amb >> 8) & 0xFF;  
    frame.data[6] = q_amb & 0xFF;

    canBus.sendMessage(&frame);
}

void LMP_Hardware::executeAction(uint8_t instructionId, uint8_t* payload, uint8_t dlc) {}
uint8_t LMP_Hardware::getErrorCode() { return global_error_register; }

// ============================================================================
// 🔥 PROFILE GROUP 2: 1x IR Sensor + 1x Humidity Probe
// ============================================================================
#elif (LMP_GROUP == 2)
#include "MLX_driver.h"
#include "AHT20_driver.h"

static float panel_obj1 = 0.0f;
static float panel_amb  = 0.0f;
static float panel_hum  = 0.0f;
static uint8_t global_error_register = 0x00;

void LMP_Hardware::init() {
    Wire.begin();
    if (!MLX_sens::initMLX(0x5A))  global_error_register |= (1 << 0);
    if (!AHT20_sens::initAHT())    global_error_register |= (1 << 2);
    refresh(); 
}

float LMP_Hardware::getCoreObjectTemp() { return panel_obj1; }

void LMP_Hardware::refresh() {
    bool ir1_fault = false;
    bool aht_fault = false;

    MLX_sens::readMLX(0x5A, panel_obj1, panel_amb, ir1_fault);
    if (ir1_fault) global_error_register |= (1 << 0); 
    else global_error_register &= ~(1 << 0);

    float clear_temp = 0.0f;
    AHT20_sens::readAHT(clear_temp, panel_hum, aht_fault);
    
    if (aht_fault) {
        global_error_register |= (1 << 2);
    } else {
        global_error_register &= ~(1 << 2);
        panel_amb = clear_temp; 
    }
}

void LMP_Hardware::sendStream(MCP2515& canBus, uint8_t opcode) {
    struct can_frame frame;
    frame.can_id = LMP_ID;
    frame.can_dlc = 8;
    frame.data[0] = 0x00;   
    frame.data[1] = opcode; 
    frame.data[2] = 0x00;   // Terminal Frame

    int16_t q_obj1 = (int16_t)(panel_obj1 * 10.0f);
    int16_t q_amb  = (int16_t)(panel_amb  * 10.0f);
    uint8_t q_hum  = (uint8_t)(panel_hum  * 2.0f);

    frame.data[3] = (q_obj1 >> 8) & 0xFF; 
    frame.data[4] = q_obj1 & 0xFF;
    frame.data[5] = (q_amb >> 8) & 0xFF;  
    frame.data[6] = q_amb & 0xFF;
    frame.data[7] = q_hum;

    canBus.sendMessage(&frame);
}

void LMP_Hardware::executeAction(uint8_t instructionId, uint8_t* payload, uint8_t dlc) {}
uint8_t LMP_Hardware::getErrorCode() { return global_error_register; }

// ============================================================================
// 🔥 PROFILE GROUP 3: Dual-Zone IR Sensors (Fragmented Packet Engine)
// ============================================================================
#elif (LMP_GROUP == 3)
#include "MLX_driver.h"

static float panel_obj1 = 0.0f;
static float panel_obj2 = 0.0f;
static float panel_amb  = 0.0f;
static uint8_t global_error_register = 0x00;

void LMP_Hardware::init() {
    Wire.begin();
    if (!MLX_sens::initMLX(0x5A)) global_error_register |= (1 << 0);
    if (!MLX_sens::initMLX(0x5B)) global_error_register |= (1 << 1); 
    refresh(); 
}

float LMP_Hardware::getCoreObjectTemp() { 
    // Return the highest critical threshold of your two monitored transformer segments
    return (panel_obj1 > panel_obj2) ? panel_obj1 : panel_obj2; 
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

void LMP_Hardware::sendStream(MCP2515& canBus, uint8_t opcode) {
    int16_t q_obj1 = (int16_t)(panel_obj1 * 10.0f);
    int16_t q_obj2 = (int16_t)(panel_obj2 * 10.0f);
    int16_t q_amb  = (int16_t)(panel_amb  * 10.0f);

    // FRAGMENT 1
    struct can_frame frag1;
    frag1.can_id = LMP_ID;
    frag1.can_dlc = 8;
    frag1.data[0] = 0x00;
    frag1.data[1] = opcode;
    frag1.data[2] = 0x01; // 1 Fragment trailing behind

    frag1.data[3] = (q_obj1 >> 8) & 0xFF;
    frag1.data[4] = q_obj1 & 0xFF;
    frag1.data[5] = (q_obj2 >> 8) & 0xFF;
    frag1.data[6] = q_obj2 & 0xFF;
    frag1.data[7] = (q_amb  >> 8) & 0xFF;

    canBus.sendMessage(&frag1);
    delayMicroseconds(500); // Guarding against receiver buffer overflow

    // FRAGMENT 2
    struct can_frame frag2;
    frag2.can_id = LMP_ID;
    frag2.can_dlc = 8;
    frag2.data[0] = 0x00;
    frag2.data[1] = opcode;
    frag2.data[2] = 0x00; // Terminal Fragment

    frag2.data[3] = q_amb & 0xFF;
    frag2.data[4] = 0x00;
    frag2.data[5] = 0x00;
    frag2.data[6] = 0x00;
    frag2.data[7] = 0x00;

    canBus.sendMessage(&frag2);
}

void LMP_Hardware::executeAction(uint8_t instructionId, uint8_t* payload, uint8_t dlc) {}
uint8_t LMP_Hardware::getErrorCode() { return global_error_register; }

// ============================================================================
// 🔥 PROFILE GROUP 4: Actuator Space
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

float LMP_Hardware::getCoreObjectTemp() { return 0.0f; } // Actuators hold no thermal traces

void LMP_Hardware::refresh() {}

void LMP_Hardware::sendStream(MCP2515& canBus, uint8_t opcode) {
    struct can_frame frame;
    frame.can_id = LMP_ID;
    frame.can_dlc = 6;
    frame.data[0] = 0x00;
    frame.data[1] = opcode; 
    frame.data[2] = 0x00;
    
    // Relay packing matrix aligns with Gateway Switch Mask parsing
    uint8_t switchMask = 0;
    if (digitalRead(RELAY_PIN_1)) switchMask |= (1 << 0);
    if (digitalRead(RELAY_PIN_2)) switchMask |= (1 << 1);

    frame.data[3] = switchMask;
    frame.data[4] = 0x00;
    frame.data[5] = 0x00; 

    canBus.sendMessage(&frame);
}

void LMP_Hardware::executeAction(uint8_t instructionId, uint8_t* payload, uint8_t dlc) {
    if (instructionId == 0x08) {
        uint8_t relaySelection = payload[2];
        uint8_t relayState     = payload[3];
        int activePin = (relaySelection == 1) ? RELAY_PIN_1 : RELAY_PIN_2;
        digitalWrite(activePin, (relayState == 1) ? HIGH : LOW);
    }
    
    if (instructionId == 0x09) { 
        digitalWrite(RELAY_PIN_1, LOW); 
        digitalWrite(RELAY_PIN_2, LOW);
        Serial.println(F("🚨 EMERGENCY STOP INTERCEPTED"));
    }
}

uint8_t LMP_Hardware::getErrorCode() { return 0x00; }

#endif