#include <SPI.h>
#include <mcp2515.h>

// Isolated hardware SPI mapping to prevent internal LoRa clashing
#define CAN_SCK   4
#define CAN_MISO  5
#define CAN_MOSI  6
#define CAN_CS    7
#define CAN_INT   2

SPIClass canSPI(HSPI); 
// Corrected Instantiation:
MCP2515 mcp2515(CAN_CS, 10000000, &canSPI);

void setup() {
  Serial.begin(115200);
  while(!Serial);
  
  Serial.println(F("========================================"));
  Serial.println(F("   External MCP2515 SPI Register Test   "));
  Serial.println(F("========================================"));
  
  // Power up the Heltec external device rail
  pinMode(Vext, OUTPUT);
  digitalWrite(Vext, LOW); 
  delay(100); 

  // Direct secondary bus configuration
  canSPI.begin(CAN_SCK, CAN_MISO, CAN_MOSI, CAN_CS);
  
  // Test 1: Reset Command Integrity
  Serial.print(F("[TEST 1/3] Issuing SPI Reset Command... "));
  if (mcp2515.reset() == MCP2515::ERROR_OK) {
    Serial.println(F("SUCCESS!"));
  } else {
    Serial.println(F("FAILED. Check logic level shifter wiring on MISO/MOSI."));
    while(1);
  }

  // Test 2: Bitrate Configuration Write Check
  Serial.print(F("[TEST 2/3] Writing Bitrate Config... "));
  if (mcp2515.setBitrate(CAN_500KBPS, MCP_8MHZ) == MCP2515::ERROR_OK) {
    Serial.println(F("SUCCESS!"));
  } else {
    Serial.println(F("FAILED. Register validation error."));
    while(1);
  }

  // Test 3: Operational State Controller Transition
  Serial.print(F("[TEST 3/3] Setting Loopback Mode... "));
  if (mcp2515.setLoopbackMode() == MCP2515::ERROR_OK) {
    Serial.println(F("SUCCESS!"));
    Serial.println(F("\n>>> SPI VERIFICATION COMPLETE: Heltec <--> MCP2515 link is stable! <<<"));
  } else {
    Serial.println(F("FAILED. Check 5V VCC power rail stability to the module."));
    while(1);
  }
}

void loop() {}