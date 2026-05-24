#include <Wire.h>
#include <Arduino.h>
#include "HT_SSD1306Wire.h"

/* ==========================================================================
   STEP 1: BUTTON ASSIGNMENTS (Top for easy editing)
   Connect buttons between the GPIO and GND (using INPUT_PULLUP)
   ========================================================================== */
#define BTN_CENTER 46  // Button 1: OK / Save (Long Press)
#define BTN_UP     45  // Button 2: Up / Increase
#define BTN_DOWN   42  // Button 3: Down / Decrease
#define BTN_LEFT   41  // Button 4: Back / Exit (Long Press)
#define BTN_RIGHT  40  // Button 5: Select / Main Menu (Long Press)

// OLED Pins for Heltec V3
static SSD1306Wire display(0x3c, 500000, SDA_OLED, SCL_OLED, GEOMETRY_128_64, RST_OLED);

/* ==========================================================================
   STATE MACHINE DEFINITIONS
   ========================================================================== */
enum MenuState {
  EMERGENCY,      // Level 0: Critical Alerts only [cite: 50, 95]
  DASHBOARD,      // Level 1: Auto-scrolling stats [cite: 54, 98]
  MAIN_MENU,      // Level 2: The Hub [cite: 102]
  DEVICE_LIST,    // Level 3: Selection of LMPs [cite: 58, 105]
  DEVICE_VIEW     // Level 3/4: Real-time telemetry [cite: 60, 106]
};

MenuState currentMode = DASHBOARD;
bool isEmergencyActive = true; // Set this true via logic to trigger Level 0 [cite: 51, 95]
int menuIndex = 0;
unsigned long lastActivityTime = 0;
unsigned long lastScrollTime = 0;
int scrollPage = 0;

void setup() {
  Serial.begin(115200);
  VextON(); // Power OLED [cite: 192, 209]
  delay(100);
  display.init();
  
  // Pin Setup [cite: 136]
  pinMode(BTN_CENTER, INPUT);
  pinMode(BTN_UP,     INPUT);
  pinMode(BTN_DOWN,   INPUT);
  pinMode(BTN_LEFT,   INPUT);
  pinMode(BTN_RIGHT,  INPUT);

  showSplashScreen(); // Credential check [cite: 63, 208]
  delay(3000); 
  lastActivityTime = millis();
}

void loop() {
  handleButtons(); // Update state machine based on user input
  
  display.clear(); // Buffer Logic: Clear -> Draw -> Display [cite: 127, 202, 212]

  // EMERGENCY OVERRIDE (Level 0) 
  if (isEmergencyActive) {
    drawEmergencyPage();
  } 
  else {
    // AUTO-SCROLL TIMEOUT (Return to Dashboard after 60s) [cite: 54, 98]
    if (millis() - lastActivityTime > 60000 && currentMode != DASHBOARD) {
      currentMode = DASHBOARD;
    }

    switch (currentMode) {
      case DASHBOARD:   drawDashboard(); break;
      case MAIN_MENU:   drawMainMenu();  break;
      // case DEVICE_LIST: drawDeviceList(); break;
      // case DEVICE_VIEW: drawDeviceView(); break;
    }
  }

  display.display();
  delay(10); // Small delay for stability
}

/* ==========================================================================
   HMI DRAWING FUNCTIONS
   ========================================================================== */

void showSplashScreen() {
  display.clear();
  display.setTextAlignment(TEXT_ALIGN_CENTER);
  display.setFont(ArialMT_Plain_16);
  display.drawString(64, 10, "HPCL MUMBAI"); // Author info [cite: 65, 119, 208]
  display.setFont(ArialMT_Plain_10);
  display.drawString(64, 30, "Substation Node V1.0");
  display.drawHorizontalLine(20, 45, 88);
  display.drawString(64, 50, "Dev: Chirag Kotian");
  display.display();
}

void drawDashboard() {
  // Auto-scroll logic for Dashboard [cite: 98, 100]
  if (millis() - lastScrollTime > 5000) {
    scrollPage = (scrollPage + 1) % 3;
    lastScrollTime = millis();
  }

  display.setTextAlignment(TEXT_ALIGN_LEFT);
  display.setFont(ArialMT_Plain_10);
  display.drawString(0, 0, "DASHBOARD");
  display.drawHorizontalLine(0, 12, 128);

  display.setTextAlignment(TEXT_ALIGN_CENTER);
  if (scrollPage == 0) {
    display.drawString(64, 25, "ACTIVE ALARMS");
    display.drawString(64, 40, "No Critical Faults");
  } else if (scrollPage == 1) {
    display.drawString(64, 25, "SYSTEM HEALTH");
    display.drawString(64, 40, "LoRa: OK | SD: READY"); // Icons go here later [cite: 71, 198]
  } else {
    display.drawString(64, 25, "DISCOVERY");
    display.drawString(64, 40, "LMPs Detected: 03"); // Discovery logic results [cite: 56, 100]
  }
}

void drawMainMenu() {
  const char* options[] = {"1. View Devices", "2. Manual Control", "3. System Settings", "4. Re-Initialize"};
  display.setTextAlignment(TEXT_ALIGN_LEFT);
  display.setFont(ArialMT_Plain_10);
  display.drawString(0, 0, "MAIN MENU");
  display.drawHorizontalLine(0, 12, 128);

  for (int i = 0; i < 4; i++) {
    if (i == menuIndex) {
      display.fillRect(0, 15 + (i * 12), 128, 12); // Inverted selection [cite: 29, 214]
      display.setColor(BLACK);
      display.drawString(5, 16 + (i * 12), options[i]);
      display.setColor(WHITE);
    } else {
      display.drawString(5, 16 + (i * 12), options[i]);
    }
  }
}

void drawEmergencyPage() {
  display.invertDisplay(); // Inverted visual for alerts [cite: 52, 131]
  display.setTextAlignment(TEXT_ALIGN_CENTER);
  display.setFont(ArialMT_Plain_16);
  display.drawString(64, 15, "!! CRITICAL !!");
  display.setFont(ArialMT_Plain_10);
  display.drawString(64, 40, "Transformer Overheat");
  display.drawString(64, 52, "Long Press [OK] to Ack"); // Safety action [cite: 53, 97, 116]
}

/* ==========================================================================
   NAVIGATION LOGIC (Placeholder for simplicity)
   ========================================================================== */
void handleButtons() {
  // Using digitalRead for demo; will move to interrupts/Queue for RTOS [cite: 86, 205]
  if (digitalRead(BTN_DOWN) == LOW) {
    menuIndex = (menuIndex + 1) % 4;
    lastActivityTime = millis();
    delay(200); // Simple debounce
  }
  if (digitalRead(BTN_CENTER) == LOW) {
    if (currentMode == DASHBOARD) currentMode = MAIN_MENU;
    lastActivityTime = millis();
    delay(200);
  }
  // Logic for BTN_LEFT (Back) and BTN_RIGHT (Select) goes here [cite: 113, 114]
}

void VextON(void) {
  pinMode(Vext, OUTPUT);
  digitalWrite(Vext, LOW); // Pull LOW to power the OLED [cite: 192, 209]
}