#include <SPI.h>
#include <mcp2515.h>

// Static Local Panel Identification Configurations
#define MY_LMP_ID         50    // Set unique hardware ID for each substation deployment
#define VOLTAGE_CHECK_PIN A0    // Local hardware monitoring pin example

MCP2515 mcp2515(10); // Default CS connected to Digital Pin 10 on Nano

enum SlaveState {
  STATE_IDLE_UNREGISTERED,
  STATE_ACTIVE_BURST,
  STATE_POST_BURST_LISTEN,
  STATE_OPERATIONAL_TELEMETRY
};

SlaveState currentLmpState = STATE_IDLE_UNREGISTERED;
unsigned long windowTimer = 0;
unsigned long transmissionTimer = 0;
unsigned long structuralJitterDelay = 150;
bool registrationConfirmed = false;

void setup() {
  Serial.begin(115200);
  SPI.begin();
  
  // Initialize internal random number generator using floating analog noise
  randomSeed(analogRead(A7)); 

  mcp2515.reset();
  mcp2515.setBitrate(CAN_500KBPS, MCP_8MHZ);
  mcp2515.setNormalMode();

  Serial.print(F(">>> LMP Substation Panel Initialized. ID: "));
  Serial.println(MY_LMP_ID);
}

void loop() {
  struct can_frame rxFrame;

  switch (currentLmpState) {

    case STATE_IDLE_UNREGISTERED:
      // Wait for master's initial invitation broadcast (0,0)
      if (mcp2515.readMessage(&rxFrame) == MCP2515::ERROR_OK) {
        if (rxFrame.can_id == 0 && rxFrame.data[0] == 0 && rxFrame.data[1] == 0) {
          Serial.println(F("[START] Discovery invite validated. Entering burst transmission..."));
          registrationConfirmed = false;
          windowTimer = millis();
          transmissionTimer = millis();
          
          // Generate an initial random offset to prevent simultaneous bus assertions
          structuralJitterDelay = 150 + random(0, 40); 
          currentLmpState = STATE_ACTIVE_BURST;
        }
      }
      break;

    case STATE_ACTIVE_BURST:
      // Periodically transmit identification frame with structured electrical jitter
      if (millis() - transmissionTimer >= structuralJitterDelay) {
        struct can_frame txFrame;
        txFrame.can_id = MY_LMP_ID;
        txFrame.can_dlc = 2; // Match strict DLC 2 requirement
        txFrame.data[0] = 0; // Intended Target: Master Node 0
        txFrame.data[1] = 0; // Protocol Command: Discovery Response
        
        mcp2515.sendMessage(&txFrame);
        
        // Recalculate unique jitter bounds for next cyclic transmission frame
        structuralJitterDelay = 150 + random(0, 40);
        transmissionTimer = millis();
      }

      // Check for strict 3-second cycle boundary timeout drop out
      if (millis() - windowTimer >= 3000) {
        Serial.println(F("[TIMEOUT] Window closed. Switching to verification loop..."));
        currentLmpState = STATE_POST_BURST_LISTEN;
      }
      break;

    case STATE_POST_BURST_LISTEN:
      if (mcp2515.readMessage(&rxFrame) == MCP2515::ERROR_OK) {
        // Scenario A: Match Unicast Acknowledgment Frame
        if (rxFrame.can_id == 0 && rxFrame.data[0] == MY_LMP_ID && rxFrame.data[1] == 0) {
          registrationConfirmed = true;
          Serial.println(F(" -> ACK MATCHED: Registration confirmation locked by Master."));
        }

        // Scenario B: Match End-of-Cycle Broadcast notification
        if (rxFrame.can_id == 0 && rxFrame.data[0] == 0 && rxFrame.data[1] == 1) {
          Serial.println(F(" -> EOC received. Verifying local state logic..."));
          if (registrationConfirmed) {
            Serial.println(F(">>> SUCCESS: Entering operational telemetry state. Absolute bus silence regarding ID. <<<"));
            currentLmpState = STATE_OPERATIONAL_TELEMETRY;
          } else {
            Serial.println(F(" -> ERROR: ACK missing/corrupt. Re-entering broadcast loop."));
            windowTimer = millis();
            transmissionTimer = millis();
            currentLmpState = STATE_ACTIVE_BURST;
          }
        }
      }
      break;

    case STATE_OPERATIONAL_TELEMETRY:
      // Standard runtime telemetry state. Stays perfectly silent regarding identity.
      // If the operator manually triggers rediscovery, the master issues a new (0,0) frame:
      if (mcp2515.readMessage(&rxFrame) == MCP2515::ERROR_OK) {
        if (rxFrame.can_id == 0 && rxFrame.data[0] == 0 && rxFrame.data[1] == 0) {
          Serial.println(F("[RESET] Master forced system discovery reboot. Clearing registration."));
          registrationConfirmed = false;
          windowTimer = millis();
          transmissionTimer = millis();
          currentLmpState = STATE_ACTIVE_BURST;
        }
      }
      break;
  }
}