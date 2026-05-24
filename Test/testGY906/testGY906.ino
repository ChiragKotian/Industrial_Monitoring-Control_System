#include <Wire.h>
#include <Adafruit_MLX90614.h>

Adafruit_MLX90614 mlx = Adafruit_MLX90614();

// Industrial Flag for "Rugged" Firmware
bool irSensorFault = false;

void setup() {
  Serial.begin(115200);
  Serial.println(F("Initializing MLX90614..."));

  if (!mlx.begin()) {
    Serial.println(F("Error: MLX90614 not found. Check I2C/EMI."));
    while (1); // Halt during Phase 1 testing
  }
}

void loop() {
  // Reading both Ambient and Object (Equipment) Temperature
  float ambientTemp = mlx.readAmbientTempC();
  float objectTemp = mlx.readObjectTempC();

  // "Pro-level" Validation: MLX sensors return 0 or NAN on failure
  check_ir_sensor(objectTemp);

  if (!irSensorFault) {
    Serial.print(F("Ambient: ")); Serial.print(ambientTemp);
    Serial.print(F(" C | Object: ")); Serial.print(objectTemp);
    Serial.println(F(" C"));
  }

  delay(2000); // 2-second polling to manage CAN bus traffic
}

void check_ir_sensor(float t) {
  if (isnan(t) || t < -70.0 || t > 380.0) { // Range boundaries for DCI model
    irSensorFault = true;
    Serial.println(F("ALERT: IR Sensor Fault - Data Invalid"));
  } else {
    irSensorFault = false;
  }
}