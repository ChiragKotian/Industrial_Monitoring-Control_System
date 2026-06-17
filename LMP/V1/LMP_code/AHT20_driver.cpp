#include "AHT20_driver.h"
#include <Wire.h>
#include <AHT20.h> 

static AHT20 ahtDevice;

bool AHT20_sens::initAHT() {
    if (ahtDevice.begin() == false) {
        Serial.println(F("🚨 AHT20 Driver Error: Device not acknowledged on I2C bus"));
        return false;
    }
    return true;
}

void AHT20_sens::readAHT(float& tempOut, float& humOut, bool& faultOut) {
    // 🔥 FAULT TOLERANCE: The AHT library can sometimes lock if I2C fails mid-read.
    // Ensure the bus is alive before requesting data.
    Wire.beginTransmission(0x38); // Standard AHT20 address
    if (Wire.endTransmission() != 0) {
        faultOut = true;
        tempOut = 0.0f;
        humOut = 0.0f;
        return;
    }

    tempOut = ahtDevice.getTemperature();
    humOut = ahtDevice.getHumidity();

    if ((tempOut == -50.00f) && (humOut == 0.0f)) {
        faultOut = true;
        tempOut = 0.0f; 
        humOut = 0.0f;
    } else {
        faultOut = false;
    }
}