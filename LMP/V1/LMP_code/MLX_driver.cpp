#include "MLX_driver.h"
#include <Wire.h>
#include <Adafruit_MLX90614.h>

static Adafruit_MLX90614 mlxPrimary;   
static Adafruit_MLX90614 mlxSecondary; 

// Internal helper to verify I2C physical connection before querying
bool checkI2CAddress(uint8_t address) {
    Wire.beginTransmission(address);
    return (Wire.endTransmission() == 0);
}

bool MLX_sens::initMLX(uint8_t addr) {
    bool setupSuccess = false;
    
    if (!checkI2CAddress(addr)) {
        Serial.print(F("🚨 MLX90614 Hardware MISSING at 0x"));
        Serial.println(addr, HEX);
        return false; // Fast fail, prevents locking
    }

    if (addr == 0x5A) {
        setupSuccess = mlxPrimary.begin(0x5A); 
    } else if (addr == 0x5B) {
        setupSuccess = mlxSecondary.begin(0x5B); 
    }

    if (!setupSuccess) {
        Serial.print(F("🚨 MLX90614 Driver: Logic handshake failure at 0x"));
        Serial.println(addr, HEX);
        return false;
    }
    return true;
}

void MLX_sens::readMLX(uint8_t addr, float& objOut, float& ambOut, bool& faultOut) {
    // 🔥 FAULT TOLERANCE: Check if physical wires disconnected mid-operation
    if (!checkI2CAddress(addr)) {
        faultOut = true;
        objOut = 0.0f;
        ambOut = 0.0f;
        return;
    }

    if (addr == 0x5A) {
        objOut = mlxPrimary.readObjectTempC();
        ambOut = mlxPrimary.readAmbientTempC();
    } else if (addr == 0x5B) {
        objOut = mlxSecondary.readObjectTempC();
        ambOut = mlxSecondary.readAmbientTempC();
    } else {
        faultOut = true;
        return;
    }

    // Trap library software failures (NaN)
    if (isnan(objOut) || isnan(ambOut) || objOut < -70.0f || objOut > 380.0f) {
        faultOut = true;
        objOut = 0.0f; 
        ambOut = 0.0f;
    } else {
        faultOut = false;
        if (ambOut >= 80.0f) {
            Serial.print(F("⚠️ CRITICAL ALERT: Casing Ambience Reaching Thermal Bounds: "));
            Serial.print(ambOut);
            Serial.println(F(" C"));
        }
    }
}