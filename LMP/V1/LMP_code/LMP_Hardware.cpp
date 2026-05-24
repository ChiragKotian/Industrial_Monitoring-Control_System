#include "LMP_Hardware.h"
#include <Wire.h>

// =========================================================
// 📦 HIDDEN VENDOR LIBRARIES & STATIC INSTANCES
// =========================================================
#if (LMP_GROUP == 1)
  //#include <Adafruit_MLX90614.h>
  //Adafruit_MLX90614 irSensor;

#elif (LMP_GROUP == 2)
  #include <AHT20.h> //AHT20 humiditySensor;
  //#include <Adafruit_MLX90614.h> //Adafruit_MLX90614 irSensor;

  AHT20 aht20;

  bool sensorFault_AHT20 = false;

#elif (LMP_GROUP == 3)
  //#include <Adafruit_MLX90614.h>
  //Adafruit_MLX90614 irSensor1(0x5A);
  //Adafruit_MLX90614 irSensor2(0x5B);
#endif


#if (LMP_GROUP == 4) // Example Future Custom Actuator Module
  const int RELAY_PIN_1 = 5;
  const int RELAY_PIN_2 = 6;
#endif

// Local operational cache
static float val_obj1 = 0.0f;
static float val_obj2 = 0.0f;
static float val_amb  = 0.0f;
static float val_hum  = 0.0f;
static uint8_t error_byte = 0x00;

// =========================================================
// ⚙️ INITIALIZATION IMPLEMENTATION
// =========================================================
void LMP_Hardware::init() {
    Wire.begin();

    #if (LMP_GROUP == 1)
      // irSensor.begin();
    #elif (LMP_GROUP == 2)
      // irSensor1.begin(); irSensor2.begin();
      if (aht20.begin() == false) {
        Serial.println(F("AHT20 Error: Check Wiring")); 
        while(true);}
    #elif (LMP_GROUP == 3)
      // irSensor1.begin(); irSensor2.begin();
    #endif


    #if (LMP_GROUP == 4)
      pinMode(RELAY_PIN_1, OUTPUT);
      pinMode(RELAY_PIN_2, OUTPUT);
      digitalWrite(RELAY_PIN_1, LOW);
      digitalWrite(RELAY_PIN_2, LOW);
    #endif

    refresh(); // Run initial collection cycle
}

// =========================================================
// 🔄 HARDWARE REFRESH (ADC/I2C Sampling)
// =========================================================
void LMP_Hardware::refresh() {
    #if (LMP_GROUP == 1)
      val_obj1 = 45.2f; val_amb = 30.1f; // Mocked hardware registers
    #elif (LMP_GROUP == 2)
      val_obj1 = 52.6f; val_amb = 29.8f; val_hum = 58.0f;
      float humidity = aht20.getHumidity();
    #elif (LMP_GROUP == 3)
      val_obj1 = 71.4f; val_obj2 = 73.2f; val_amb = 31.0f;
    #elif (LMP_GROUP == 4)
      // Actuators might read dynamic feedback or shunt current measurements here
    #endif
}

// =========================================================
// 🛡️ FIXED-POINT DATA PACKING (To Master Node)
// =========================================================
void LMP_Hardware::packTelemetry(uint8_t* buffer, uint8_t& dlcOut) {
    int16_t q_obj1 = (int16_t)(val_obj1 * 10.0f);
    int16_t q_obj2 = (int16_t)(val_obj2 * 10.0f);
    int16_t q_amb  = (int16_t)(val_amb  * 10.0f);
    uint8_t q_hum  = (uint8_t)(val_hum  * 2.0f);

    #if (LMP_GROUP == 1)
        dlcOut = 6;
        buffer[2] = (q_obj1 >> 8) & 0xFF; buffer[3] = q_obj1 & 0xFF;
        buffer[4] = (q_amb  >> 8) & 0xFF; buffer[5] = q_amb  & 0xFF;
    #elif (LMP_GROUP == 2)
        dlcOut = 7;
        buffer[2] = (q_obj1 >> 8) & 0xFF; buffer[3] = q_obj1 & 0xFF;
        buffer[4] = (q_amb  >> 8) & 0xFF; buffer[5] = q_amb  & 0xFF;
        buffer[6] = q_hum;
    #elif (LMP_GROUP == 3)
        dlcOut = 8;
        buffer[2] = (q_obj1 >> 8) & 0xFF; buffer[3] = q_obj1 & 0xFF;
        buffer[4] = (q_obj2 >> 8) & 0xFF; buffer[5] = q_obj2 & 0xFF;
        buffer[6] = (q_amb  >> 8) & 0xFF; buffer[7] = q_amb  & 0xFF;
    #elif (LMP_GROUP == 4)
        dlcOut = 2; // Actuators might only reply with echo frames
    #endif
}

// =========================================================
// 🚀 COMMAND/ACTUATOR EXECUTION ENGINE
// =========================================================
void LMP_Hardware::executeAction(uint8_t instructionId, uint8_t* payload, uint8_t dlc) {
    // Handle standard configuration tasks
    if (instructionId == 5) {
        // e.g., Set local thresholds or change configurations
        return;
    }

    // Handle future custom control or physical actuation triggers
    #if (LMP_GROUP == 4)
      if (instructionId == 8) { // Regular Actuate Command
          uint8_t targetRelay  = payload[2];
          uint8_t relayCommand = payload[3];
          int targetPin = (targetRelay == 1) ? RELAY_PIN_1 : RELAY_PIN_2;
          digitalWrite(targetPin, (relayCommand == 1) ? HIGH : LOW);
      }
    #endif

    // Global Safe State Handling
    if (instructionId == 9) { // EMERGENCY STOP
        #if (LMP_GROUP == 4)
          digitalWrite(RELAY_PIN_1, LOW); // Instantly cut power to substation switchgears
          digitalWrite(RELAY_PIN_2, LOW);
        #endif
    }
}

uint8_t LMP_Hardware::getErrorCode() {
    return error_byte;
}