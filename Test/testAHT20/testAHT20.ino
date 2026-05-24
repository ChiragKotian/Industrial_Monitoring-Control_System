#include <Wire.h>
#include <AHT20.h>

AHT20 aht20;

// Industrial Metadata for Discovery
#define LMP_ID 0x01
#define LMP_TYPE "TEMP_HUM"

bool sensorFault = false; 

void setup() {
  Serial.begin(115200);

  // Use F() to keep strings out of the 2KB SRAM

  Wire.begin();
  if (aht20.begin() == false) {
    Serial.println(F("AHT20 Error: Check Wiring")); 
    while(true);
  }
}

void loop() { 
  float temperature = aht20.getTemperature();
  float humidity = aht20.getHumidity();
  
  // Validation logic for "Rugged Firmware" [cite: 12, 29]
  check_sensor(temperature, humidity);

  if (!sensorFault) {
    Serial.print("T: "); Serial.print(temperature, 2);
    Serial.print(" C\t H: "); Serial.print(humidity, 2);
    Serial.println("% RH");
  }

  delay(2000); // 2s polling is standard for non-critical monitoring [cite: 418]
}

void check_sensor(float t, float h) {
  // AHT2x sensors often return -50/0 on communication failure
  if((t == -50.00) && (h == 0.0)) {
    sensorFault = true;
    Serial.println(F("ALERT: Sensor Offline/EMI Interference"));
    // NEXT: Send "Sensor Fault" CAN message to Heltec Node [cite: 29]
  } else {
    sensorFault = false;
  }
}