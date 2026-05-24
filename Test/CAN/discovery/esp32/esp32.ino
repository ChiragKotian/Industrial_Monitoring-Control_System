#include <SPI.h>
#include <mcp2515.h>

// Isolated SPI bus definitions to avoid clashing with internal LoRa
#define CAN_SCK   4
#define CAN_MISO  5   // Ensure your 1k/2k resistor voltage divider is present
#define CAN_MOSI  6
#define CAN_CS    7
#define CAN_INT   2

SPIClass canSPI(HSPI);
MCP2515 mcp2515(CAN_CS, 4000000, &canSPI); // 4MHz clock limits timing mismatches

// System State Machine Architecture
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
const unsigned long TIMEOUT_3S = 3000; // Strict 3-second industrial window

// Registry Specifications
#define MAX_POSSIBLE_DEVICES 240
uint8_t lmpRegistry[MAX_POSSIBLE_DEVICES];
uint8_t activeNodeCount = 0;
bool stragglersFoundInRecheck = false;

// Function Prototypes
void sendCanFrame(uint8_t targetId, uint8_t instructionId);
bool registerNodeAttempt(uint8_t id);
void sortAndCompactRegistry();

void setup() {
  Serial.begin(115200);
  while(!Serial);

  pinMode(Vext, OUTPUT);
  digitalWrite(Vext, LOW); // Activate Heltec external Vext rail
  delay(100);

  canSPI.begin(CAN_SCK, CAN_MISO, CAN_MOSI, CAN_CS);
  mcp2515.reset();
  mcp2515.setBitrate(CAN_500KBPS, MCP_8MHZ);
  mcp2515.setNormalMode();

  Serial.println(F("============================================="));
  Serial.println(F("   HPCL GATEWAY: REGISTRATION STATE MACHINE  "));
  Serial.println(F("============================================="));
}

void loop() {
  struct can_frame rxFrame;

  switch (currentSystemState) {

    case STATE_INIT_DISCOVERY:
      Serial.println(F("\n[INIT] Clearing registry. Broadcasting (0,0)..."));
      activeNodeCount = 0;
      // Step 1: Initialize all array spaces to 255 placeholder as specified
      for (int i = 0; i < MAX_POSSIBLE_DEVICES; i++) {
        lmpRegistry[i] = 255;
      }
      
      sendCanFrame(0, 0); // Broadcast Discovery Invitation
      stateTimer = millis();
      currentSystemState = STATE_LISTEN_WINDOW;
      break;

    case STATE_LISTEN_WINDOW:
      // Collect all incoming nodes for the full 3 seconds
      if (mcp2515.readMessage(&rxFrame) == MCP2515::ERROR_OK) {
        if (rxFrame.data[0] == 0 && rxFrame.data[1] == 0) { // Target=0, Instruction=0
          if (registerNodeAttempt(rxFrame.can_id)) {
            Serial.print(F(" -> Staged Node ID: "));
            Serial.println(rxFrame.can_id);
          }
        }
      }

      if (millis() - stateTimer >= TIMEOUT_3S) {
        Serial.println(F("[TIMEOUT] 3 Seconds expired. Advancing to sequential ACKs."));
        currentSystemState = STATE_SEND_ACK_SEQUENTIAL;
      }
      break;

    case STATE_SEND_ACK_SEQUENTIAL:
      Serial.println(F("[ACK PHASE] Dispatching individual unicast confirmations..."));
      // Process only valid staged elements in the buffer
      for (int i = 0; i < MAX_POSSIBLE_DEVICES; i++) {
        if (lmpRegistry[i] != 255) {
          Serial.print(F(" -> Direct ACK to Node: "));
          Serial.println(lmpRegistry[i]);
          sendCanFrame(lmpRegistry[i], 0); // 1st Byte: Node ID, 2nd Byte: 0
          delay(15); // Prevents buffer saturation on the transceiver line
        }
      }
      currentSystemState = STATE_SEND_EOC;
      break;

    case STATE_SEND_EOC:
      Serial.println(F("[EOC] Broadcasting End of Cycle (0,1)..."));
      sendCanFrame(0, 1); // Target=0, Instruction=1
      
      stragglersFoundInRecheck = false;
      stateTimer = millis();
      currentSystemState = STATE_RECHECK_WINDOW;
      break;

    case STATE_RECHECK_WINDOW:
      // Listen for another 3 seconds to capture any un-ACK'd nodes
      if (mcp2515.readMessage(&rxFrame) == MCP2515::ERROR_OK) {
        if (rxFrame.data[0] == 0 && rxFrame.data[1] == 0) {
          Serial.print(F(" -> ALERT: Straggler identified in recheck: ID "));
          Serial.println(rxFrame.can_id);
          if (registerNodeAttempt(rxFrame.can_id)) {
            stragglersFoundInRecheck = true;
          }
        }
      }

      if (millis() - stateTimer >= TIMEOUT_3S) {
        if (stragglersFoundInRecheck) {
          Serial.println(F("[RECHECK FAILED] Unverified traffic remains. Cycling back to ACKs."));
          currentSystemState = STATE_SEND_ACK_SEQUENTIAL;
        } else {
          Serial.println(F("\n>>> SUCCESS: Bus is verified silent. Finalizing Registry... <<<"));
          sortAndCompactRegistry();
          currentSystemState = STATE_OPERATIONAL;
        }
      }
      break;

    case STATE_OPERATIONAL:
      // Safe, localized monitoring state execution loop.
      // A manual trigger from your UI Menu task will set currentSystemState = STATE_INIT_DISCOVERY;
      break;
  }
}

void sendCanFrame(uint8_t targetId, uint8_t instructionId) {
  struct can_frame txFrame;
  txFrame.can_id = 0; // Master Node standard header
  txFrame.can_dlc = 2; // Strict DLC 2 constraint to maximize bandwidth health
  txFrame.data[0] = targetId;
  txFrame.data[1] = instructionId;
  mcp2515.sendMessage(&txFrame);
}

bool registerNodeAttempt(uint8_t id) {
  // Prevent duplicate allocations in the buffer
  for (int i = 0; i < MAX_POSSIBLE_DEVICES; i++) {
    if (lmpRegistry[i] == id) return false;
  }
  // Store the node in the first available slot containing placeholder 255
  for (int i = 0; i < MAX_POSSIBLE_DEVICES; i++) {
    if (lmpRegistry[i] == 255) {
      lmpRegistry[i] = id;
      return true;
    }
  }
  return false;
}

void sortAndCompactRegistry() {
  // Safe In-Place Insertion Sort & Compaction
  activeNodeCount = 0;
  
  // 1. Sort valid entries and shift 255 placeholders to the back
  for (int i = 1; i < MAX_POSSIBLE_DEVICES; i++) {
    uint8_t key = lmpRegistry[i];
    int j = i - 1;
    while (j >= 0 && lmpRegistry[j] > key) {
      lmpRegistry[j + 1] = lmpRegistry[j];
      j = j - 1;
    }
    lmpRegistry[j + 1] = key;
  }

  // 2. Count active registered items before encountering placeholders
  for (int i = 0; i < MAX_POSSIBLE_DEVICES; i++) {
    if (lmpRegistry[i] != 255) {
      activeNodeCount++;
    } else {
      break; // Sorting has pushed all 255 elements behind this ceiling
    }
  }

  // Log clean system inventory map
  Serial.print(F("Total Active Connected Nodes Locked: "));
  Serial.println(activeNodeCount);
  for (uint8_t i = 0; i < activeNodeCount; i++) {
    Serial.print(F(" -> Position ["));
    Serial.print(i);
    Serial.print(F("]: Connected LMP Node Address: ID "));
    Serial.println(lmpRegistry[i]);
  }
  Serial.println(F("=============================================\n"));
}