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
    // 🛡️ HARDWARE SAFEGUARD: Configure the I2C timing register (TWBR)
    // On the Arduino Nano (ATmega328P), this forces the I2C clock rate 
    // and prevents the library from hanging if the SDA line is left floating.
    TWBR = ((F_CPU / 100000L) - 16) / 2; 

    // 1. Verify bus health BEFORE calling library methods
    if (!checkI2CAddress(addr)) {
        faultOut = true;
        objOut = 0.0f;
        ambOut = 0.0f;
        return;
    }

    // 2. Perform acquisition
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

    // 3. Post-acquisition validation check
    // We check for NAN and also ensure the values make sense for a refinery environment
    if (isnan(objOut) || isnan(ambOut) || objOut < -70.0f || objOut > 380.0f) {
        faultOut = true;
        objOut = 0.0f; 
        ambOut = 0.0f;
    } else {
        faultOut = false;
        // Thermal boundary check
        if (ambOut >= 80.0f) {
            // This alert prints to local LMP serial only, not the CAN bus
            Serial.print(F("⚠️ ALERT: Casing Thermal Bound: "));
            Serial.println(ambOut);
        }
    }
}