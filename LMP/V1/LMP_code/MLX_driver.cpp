#include "MLX_driver.h"
#include <Wire.h>
#include <Adafruit_MLX90614.h>

// FIX: The Adafruit library constructor takes 0 arguments at global scope.
// The custom I2C addresses are passed to the .begin() function inside setup instead!
static Adafruit_MLX90614 mlxPrimary;   // Standard instantiation for primary tracking
static Adafruit_MLX90614 mlxSecondary; // Standard instantiation for dual-zone tracking

bool MLX_sens::initMLX(uint8_t addr) {
    bool setupSuccess = false;

    // Route the explicit address runtime configurations straight to the initialization calls
    if (addr == 0x5A) {
        setupSuccess = mlxPrimary.begin(0x5A); // Pass address parameter here!
    } else if (addr == 0x5B) {
        setupSuccess = mlxSecondary.begin(0x5B); // Pass address parameter here!
    }

    if (!setupSuccess) {
        Serial.print(F("🚨 MLX90614 Driver: Chip handshake failure on address 0x"));
        Serial.println(addr, HEX);
        return false;
    }
    return true;
}

void MLX_sens::readMLX(uint8_t addr, float& objOut, float& ambOut, bool& faultOut) {
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

    // Rugged Verification Bounds: Trap library failures (NaN) or operational boundaries
    if (isnan(objOut) || isnan(ambOut) || objOut < -70.0f || objOut > 380.0f) {
        faultOut = true;
        objOut = 0.0f; // Force safe defaults to safeguard fixed-point quantization math
        ambOut = 0.0f;
        Serial.print(F("⚠️ ALERT: MLX90614 Sensor Malfunction / Frame Loss at 0x"));
        Serial.println(addr, HEX);
    } else {
        faultOut = false;

        // Diagnostic verification check for high-temperature switchgear spaces
        if (ambOut >= 80.0f) {
            Serial.print(F("⚠️ CRITICAL ALERT: Casing Ambience Reaching Thermal Bounds: "));
            Serial.print(ambOut);
            Serial.println(F(" C"));
        }
    }
}