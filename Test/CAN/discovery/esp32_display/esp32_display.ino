#include <SPI.h>
#include <Wire.h>
#include <mcp2515.h>
#include "HT_SSD1306Wire.h" 

// Independent HSPI Hardware Mapping to protect against internal LoRa clashing
#define CAN_SCK   4
#define CAN_MISO  5   // Ensure the 1k/2k passive resistor divider is implemented
#define CAN_MOSI  6
#define CAN_CS    7
#define CAN_INT   2

SPIClass canSPI(HSPI);
MCP2515 mcp2515(CAN_CS, 4000000, &canSPI);

// Instantiate the OLED Display using Heltec's pre-mapped I2C pins
static SSD1306Wire display(0x3c, 500000, SDA_OLED, SCL_OLED, GEOMETRY_128_64, RST_OLED);

// State Machine Definition
enum MasterState {
  STATE_INIT_DISCOVERY,
  STATE_LISTEN_WINDOW,
  STATE_SEND_ACK_SEQUENTIAL,
  STATE_SEND_EOC,
  STATE_RECHECK_WINDOW,
  STATE_OPERATIONAL
};

MasterState currentSystemState = STATE_INIT_DISCOVERY;
unsigned long stateTimer = 0;
const unsigned long TIMEOUT_3S = 3000;

// Registry Allocation Bounds
#define MAX_POSSIBLE_DEVICES 240
uint8_t lmpRegistry[MAX_POSSIBLE_DEVICES];
uint8_t activeNodeCount = 0;
bool stragglersFoundInRecheck = false;

// UI Interface Text Helper
String currentStatusText = "Booting...";

void sendCanFrame(uint8_t targetId, uint8_t instructionId);
bool registerNodeAttempt(uint8_t id);
void sortAndCompactRegistry();
void updateOledUI(bool showProgressBar, uint8_t forcedProgressValue = 0);

void setup() {
  Serial.begin(115200);

  // Power up Heltec Vext rail to enable display & internal routing MOSFETs
  pinMode(Vext, OUTPUT);
  digitalWrite(Vext, LOW); 
  delay(100); 

  // Initialize display subsystem
  display.init();
  display.clear();
  display.flipScreenVertically();
  
  // Splash Screen
  display.setFont(ArialMT_Plain_16);
  display.setTextAlignment(TEXT_ALIGN_CENTER);
  display.drawString(64, 10, "HPCL MUMBAI");
  display.setFont(ArialMT_Plain_10);
  display.drawString(64, 32, "Substation Gateway v1.1");
  display.drawString(64, 48, "Dev: Chirag Kotian");
  display.display();
  delay(2000); 

  // Initialize the CAN Controller hardware link
  canSPI.begin(CAN_SCK, CAN_MISO, CAN_MOSI, CAN_CS);
  mcp2515.reset();
  mcp2515.setBitrate(CAN_500KBPS, MCP_8MHZ);
  mcp2515.setNormalMode();

  Serial.println(F(">>> HPCL OLED-Integrated Gateway Online <<<"));
}

void loop() {
  struct can_frame rxFrame;

  switch (currentSystemState) {

    case STATE_INIT_DISCOVERY:
      currentStatusText = "STARTING SCAN";
      updateOledUI(true, 0);
      
      activeNodeCount = 0;
      for (int i = 0; i < MAX_POSSIBLE_DEVICES; i++) {
        lmpRegistry[i] = 255;
      }
      
      sendCanFrame(0, 0); 
      stateTimer = millis();
      currentSystemState = STATE_LISTEN_WINDOW;
      break;

    case STATE_LISTEN_WINDOW:
      currentStatusText = "SCANNING BUS...";
      
      if (mcp2515.readMessage(&rxFrame) == MCP2515::ERROR_OK) {
        if (rxFrame.data[0] == 0 && rxFrame.data[1] == 0) {
          if (registerNodeAttempt(rxFrame.can_id)) {
            int temporaryCount = 0;
            for (int i=0; i<MAX_POSSIBLE_DEVICES; i++) {
              if (lmpRegistry[i] != 255) temporaryCount++;
            }
            activeNodeCount = temporaryCount;
          }
        }
      }

      // Progress bar calculation
      {
        unsigned long elapsed = millis() - stateTimer;
        uint8_t percentage = (elapsed * 100) / TIMEOUT_3S;
        if (percentage > 100) percentage = 100;
        updateOledUI(true, percentage);
      }

      if (millis() - stateTimer >= TIMEOUT_3S) {
        currentSystemState = STATE_SEND_ACK_SEQUENTIAL;
      }
      break;

    case STATE_SEND_ACK_SEQUENTIAL:
      currentStatusText = "ISSUING ACKS";
      updateOledUI(true, 100);

      for (int i = 0; i < MAX_POSSIBLE_DEVICES; i++) {
        if (lmpRegistry[i] != 255) {
          sendCanFrame(lmpRegistry[i], 0);
          delay(15); 
        }
      }
      currentSystemState = STATE_SEND_EOC;
      break;

    case STATE_SEND_EOC:
      currentStatusText = "BROADCASTING EOC";
      updateOledUI(true, 100);
      
      sendCanFrame(0, 1); 
      stragglersFoundInRecheck = false;
      stateTimer = millis();
      currentSystemState = STATE_RECHECK_WINDOW;
      break;

    case STATE_RECHECK_WINDOW:
      currentStatusText = "RECHECK ACTIVE";
      
      if (mcp2515.readMessage(&rxFrame) == MCP2515::ERROR_OK) {
        if (rxFrame.data[0] == 0 && rxFrame.data[1] == 0) {
          if (registerNodeAttempt(rxFrame.can_id)) {
            stragglersFoundInRecheck = true;
          }
        }
      }

      {
        unsigned long elapsed = millis() - stateTimer;
        uint8_t percentage = 100 - ((elapsed * 100) / TIMEOUT_3S); 
        updateOledUI(true, percentage);
      }

      if (millis() - stateTimer >= TIMEOUT_3S) {
        if (stragglersFoundInRecheck) {
          currentSystemState = STATE_SEND_ACK_SEQUENTIAL;
        } else {
          sortAndCompactRegistry();
          currentSystemState = STATE_OPERATIONAL;
        }
      }
      break;

    case STATE_OPERATIONAL:
      currentStatusText = "SYSTEM RUNNING";
      updateOledUI(false); 
      break;
  }
}

void sendCanFrame(uint8_t targetId, uint8_t instructionId) {
  struct can_frame txFrame;
  txFrame.can_id = 0;
  txFrame.can_dlc = 2;
  txFrame.data[0] = targetId;
  txFrame.data[1] = instructionId;
  mcp2515.sendMessage(&txFrame);
}

bool registerNodeAttempt(uint8_t id) {
  for (int i = 0; i < MAX_POSSIBLE_DEVICES; i++) {
    if (lmpRegistry[i] == id) return false;
  }
  for (int i = 0; i < MAX_POSSIBLE_DEVICES; i++) {
    if (lmpRegistry[i] == 255) {
      lmpRegistry[i] = id;
      return true;
    }
  }
  return false;
}

void sortAndCompactRegistry() {
  activeNodeCount = 0;
  for (int i = 1; i < MAX_POSSIBLE_DEVICES; i++) {
    uint8_t key = lmpRegistry[i];
    int j = i - 1;
    while (j >= 0 && lmpRegistry[j] > key) {
      lmpRegistry[j + 1] = lmpRegistry[j];
      j = j - 1;
    }
    lmpRegistry[j + 1] = key;
  }
  for (int i = 0; i < MAX_POSSIBLE_DEVICES; i++) {
    if (lmpRegistry[i] != 255) {
      activeNodeCount++;
    } else {
      break; 
    }
  }
}

void updateOledUI(bool showProgressBar, uint8_t forcedProgressValue) {
  display.clear();
  
  // Static System Header Area
  display.setFont(ArialMT_Plain_10);
  display.setTextAlignment(TEXT_ALIGN_LEFT);
  display.drawString(0, 0, "HPCL NODE: 0x00");
  
  display.setTextAlignment(TEXT_ALIGN_RIGHT);
  display.drawString(128, 0, "500 KBPS");
  display.drawHorizontalLine(0, 12, 128);

  if (showProgressBar) {
    display.setTextAlignment(TEXT_ALIGN_CENTER);
    display.setFont(ArialMT_Plain_10);
    display.drawString(64, 18, currentStatusText);
    
    String counterMsg = "Staged LMPs: " + String(activeNodeCount);
    display.setFont(ArialMT_Plain_16);
    display.drawString(64, 30, counterMsg);
    
    display.drawProgressBar(4, 50, 120, 8, forcedProgressValue);
  } 
  else {
    // FIXED: Non-overflowing horizontal aggregation layout
    display.setTextAlignment(TEXT_ALIGN_LEFT);
    display.setFont(ArialMT_Plain_10);
    display.drawString(0, 14, "DISCOVERY LOCKED");
    
    String countString = "Total: " + String(activeNodeCount);
    display.setTextAlignment(TEXT_ALIGN_RIGHT);
    display.drawString(128, 14, countString);
    display.drawHorizontalLine(0, 25, 128);

    if (activeNodeCount == 0) {
      display.setTextAlignment(TEXT_ALIGN_CENTER);
      display.drawString(64, 40, "[NO DEVICES ONLINE]");
    } 
    else {
      String sensorList = "Sens: ";
      String actuatorList = "Acts: ";
      bool hasSensors = false;
      bool hasActuators = false;

      // Build text streams directly from the sorted repository blocks
      for (uint8_t i = 0; i < activeNodeCount; i++) {
        if (lmpRegistry[i] <= 160) {
          if (hasSensors) sensorList += ", ";
          sensorList += String(lmpRegistry[i]);
          hasSensors = true;
        } else {
          if (hasActuators) actuatorList += ", ";
          actuatorList += String(lmpRegistry[i]);
          hasActuators = true;
        }
      }

      if (!hasSensors) sensorList += "None";
      if (!hasActuators) actuatorList += "None";

      // Render with auto-wrap parameters to step down cleanly if lines grow long
      display.setTextAlignment(TEXT_ALIGN_LEFT);
      display.drawStringMaxWidth(0, 28, 128, sensorList);
      
      // Calculate start position of next row based on typical sensor list footprints
      display.drawStringMaxWidth(0, 48, 128, actuatorList);
    }
  }

  display.display();
}