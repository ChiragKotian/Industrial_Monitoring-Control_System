#include "AHT20_driver.h"
#include <Wire.h>
#include <AHT20.h> // The core vendor register-map library

// Keep the hardware instance static and hidden within this compilation unit
static AHT20 ahtDevice;

bool AHT20_sens::initAHT() {
    // Non-blocking initialization. Return false if hardware is missing on the I2C bus.
    if (ahtDevice.begin() == false) {
        Serial.println(F("🚨 AHT20 Driver Error: Device not acknowledged on I2C bus"));
        return false;
    }
    return true;
}

void AHT20_sens::readAHT(float& tempOut, float& humOut, bool& faultOut) {
    tempOut = ahtDevice.getTemperature();
    humOut = ahtDevice.getHumidity();

    // Rugged firmware validation check for substation safety
    // Standard Asair AHT chips hard-drop to exactly -50.00C and 0.0% RH on line failure
    if ((tempOut == -50.00f) && (humOut == 0.0f)) {
        faultOut = true;
        tempOut = 0.0f; // Force safe defaults to protect downstream fixed-point quantization math
        humOut = 0.0f;
        Serial.println(F("⚠️ ALERT: AHT20 Sensor Offline / Transient EMI Interference"));
    } else {
        faultOut = false;
    }
}