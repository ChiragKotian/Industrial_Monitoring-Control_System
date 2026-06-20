#include "Node_UI.h"
#include "Node_Registry.h"
#include "Node_Storage.h"
#include "Node_CAN.h"

// 🎛️ HARDWARE PIN DEFINITIONS (Safe ESP32-S3 Pins)
#define BTN_ENTER 1   
#define BTN_BACK  3  
#define BTN_UP    41 
#define BTN_DOWN  42 
#define BTN_HOME  47

enum HmiEventType {
    EVENT_BUTTON_CLICK
};

struct HmiEvent {
    HmiEventType type;
    uint8_t      buttonId; 
};

SSD1306Wire NodeUI::display(0x3c, 500000, SDA_OLED, SCL_OLED, GEOMETRY_128_64, RST_OLED);
MenuLevel NodeUI::activeMenuState = MENU_WELCOME_SPLASH;

static uint8_t selectedLmpId = 0; 
static uint8_t currentLiveId = 0;
static QueueHandle_t xHmiQueue = NULL;
static uint32_t buttonPressStart[50] = {0}; 

// ⚡ HARDWARE INTERRUPT (Debounced for External Pull-ups)
static void IRAM_ATTR hmiButtonISR(void* arg) {
    uint32_t pin = (uint32_t)arg;
    
    if (digitalRead(pin) == LOW) { // Button Pressed (Shorting to GND)
        buttonPressStart[pin] = xTaskGetTickCountFromISR();
    } else { // Button Released
        uint32_t pressDuration = (xTaskGetTickCountFromISR() - buttonPressStart[pin]) * portTICK_PERIOD_MS;
        
        if (pressDuration > 20) { // 20ms hardware debounce threshold
            HmiEvent evt;
            evt.type = EVENT_BUTTON_CLICK;
            evt.buttonId = pin;
            
            BaseType_t xHigherPriorityTaskWoken = pdFALSE;
            xQueueSendFromISR(xHmiQueue, &evt, &xHigherPriorityTaskWoken);
            if (xHigherPriorityTaskWoken) {
                portYIELD_FROM_ISR();
            }
        }
    }
}

void NodeUI::init() {
    xHmiQueue = xQueueCreate(15, sizeof(HmiEvent));
    if (xHmiQueue == NULL) return;

    pinMode(Vext, OUTPUT);
    digitalWrite(Vext, LOW); 
    delay(100);            

    display.init();
    display.flipScreenVertically();
    display.setFont(ArialMT_Plain_10);
    
    // 🎛️ INITIALIZE 5-BUTTON MATRIX
    // Because you have external pull-ups, we just use standard INPUT
    uint8_t buttonPins[] = {BTN_ENTER, BTN_BACK, BTN_UP, BTN_DOWN, BTN_HOME}; 
    for(uint8_t pin : buttonPins) {
        pinMode(pin, INPUT);
        attachInterruptArg(digitalPinToInterrupt(pin), hmiButtonISR, (void*)pin, CHANGE); 
    }
    Serial.println(F("[HMI Engine] 5-Button Matrix Deployed."));
}

void NodeUI::renderHeader(){
    uint8_t activeNodes[MAX_NODE_ID];
    uint8_t totalFound = NodeRegistry::getActiveNodesList(activeNodes);

    display.setFont(ArialMT_Plain_10);
    display.setTextAlignment(TEXT_ALIGN_LEFT);
    display.drawString(0, 0, "HPCL REGISTRY: " + String(totalFound)); 
    display.setTextAlignment(TEXT_ALIGN_RIGHT);
    display.drawString(128, 0, "SS-A1"); 
    display.drawHorizontalLine(0, 12, 128);
}

void NodeUI::drawWelcomeSplash() {
    display.setFont(ArialMT_Plain_16);
    display.setTextAlignment(TEXT_ALIGN_CENTER);
    display.drawString(64, 6, "HPCL MUMBAI");
    display.setFont(ArialMT_Plain_10);
    display.drawString(64, 26, "Substation Gateway v1.2");
    display.drawString(64, 40, "Dev: Chirag Kotian");
    display.drawString(64, 52, "[CAN Boot Initializing]");
}

void NodeUI::drawScanningPage() {
    renderHeader();
    display.setTextAlignment(TEXT_ALIGN_CENTER);
    display.setFont(ArialMT_Plain_10);
    display.drawString(64, 16, "SCANNING FIELD BUS...");
    
    uint32_t elapsed = millis() - NodeCAN::discoveryStartTime;
    uint32_t totalExpectedWindow = DISCOVERY_WINDOW + 3500; 
    uint8_t percentage = (elapsed * 100) / totalExpectedWindow;
    if (percentage > 100) percentage = 100;
    
    uint8_t activeNodes[MAX_NODE_ID];
    uint8_t totalFound = NodeRegistry::getActiveNodesList(activeNodes);
    
    display.setFont(ArialMT_Plain_16);
    display.drawString(64, 28, "Staged LMPs: " + String(totalFound));
    display.drawProgressBar(4, 50, 120, 8, percentage);
}

void NodeUI::drawAutoDashboard() {
    renderHeader();
    uint8_t activeNodes[MAX_NODE_ID];
    uint8_t totalFound = NodeRegistry::getActiveNodesList(activeNodes);
    
    if (totalFound == 0) {
        display.setTextAlignment(TEXT_ALIGN_CENTER);
        display.setFont(ArialMT_Plain_10);
        display.drawString(64, 30, "[ NO DEVICES ONLINE ]");
        return;
    }
    
    uint32_t activeIndex = (millis() / 3000) % totalFound; 
    currentLiveId = activeNodes[activeIndex]; 
    
    LMPDataRecord snapshot;
    if (NodeRegistry::getNodeSnapshot(currentLiveId, snapshot)) {
        display.setFont(ArialMT_Plain_16);
        display.setTextAlignment(TEXT_ALIGN_LEFT);
        display.drawString(0, 15, "LMP ID: " + String(currentLiveId));
        
        display.setFont(ArialMT_Plain_10);
        display.setTextAlignment(TEXT_ALIGN_RIGHT);
        display.drawString(128, 15, "GRP: " + String(snapshot.groupType));
        display.setTextAlignment(TEXT_ALIGN_LEFT);
        
        if (snapshot.groupType == 1) { 
            display.drawString(0, 32, "IR Probe 1: " + String(snapshot.objectTemp1, 1) + " C");
            display.drawString(0, 46, "Case Amb:   " + String(snapshot.ambientTemp, 1) + " C");
            display.setTextAlignment(TEXT_ALIGN_RIGHT);
            display.drawString(128, 46, "SNG-IR");
        } 
        else if (snapshot.groupType == 2) { 
            display.drawString(0, 32, "IR Target:  " + String(snapshot.objectTemp1, 1) + " C");
            display.drawString(0, 46, "Prec Amb:   " + String(snapshot.ambientTemp, 1) + " C");
            display.setTextAlignment(TEXT_ALIGN_RIGHT);
            display.drawString(128, 46, "RH: " + String(snapshot.humidity, 0) + "%");
        }
        else if (snapshot.groupType == 3) {
            display.drawString(0, 24, "Phase A T:  " + String(snapshot.objectTemp1, 1) + " C");
            display.drawString(0, 36, "Phase B T:  " + String(snapshot.objectTemp2, 1) + " C");
            display.drawString(0, 48, "Shared Amb: " + String(snapshot.ambientTemp, 1) + " C");
            display.setTextAlignment(TEXT_ALIGN_RIGHT);
            display.drawString(128, 48, "DUAL-IR");
        }
        else if (snapshot.groupType == 4) { 
            display.drawString(0, 34, "VCB Breaker: CLOSED");
            display.drawString(0, 46, "Aux Fan 1:   RUNNING");
            display.setTextAlignment(TEXT_ALIGN_RIGHT);
            display.drawString(128, 46, "SYS: OK");
        }
    }
}

void NodeUI::drawDeviceTelemetryPage(uint8_t targetId) {
    renderHeader();
    LMPDataRecord snapshot;
    if (!NodeRegistry::getNodeSnapshot(targetId, snapshot)) {
        display.setTextAlignment(TEXT_ALIGN_CENTER);
        display.drawString(64, 32, "NODE UNRESPONSIVE");
        return;
    }
    
    display.setFont(ArialMT_Plain_10);
    display.setTextAlignment(TEXT_ALIGN_LEFT);
    display.drawString(0, 14, "DIAGNOSTICS -> ID " + String(targetId));
    display.drawString(0, 26, "IR Probe 1:  " + String(snapshot.objectTemp1, 2) + " C");
    display.drawString(0, 38, "Encl Amb:    " + String(snapshot.ambientTemp, 2) + " C");
    display.drawString(0, 50, "Encl Hum:    " + String(snapshot.humidity, 1) + " %RH");
    
    if (snapshot.errorCode != 0x00) {
        display.setTextAlignment(TEXT_ALIGN_RIGHT);
        display.drawString(128, 14, "ERR: 0x" + String(snapshot.errorCode, HEX));
    }
}

void NodeUI::drawDeviceDiagnosticPage(uint8_t targetId) {
    renderHeader();
    LMPDataRecord snapshot;
    
    if (!NodeRegistry::getNodeSnapshot(targetId, snapshot)) {
        display.setTextAlignment(TEXT_ALIGN_CENTER);
        display.drawString(64, 32, "NODE OFFLINE");
        return;
    }
    
    display.setFont(ArialMT_Plain_10);
    display.setTextAlignment(TEXT_ALIGN_LEFT);
    display.drawString(0, 14, "DIAGS -> NODE " + String(targetId));
    
    display.setTextAlignment(TEXT_ALIGN_RIGHT);
    display.drawString(128, 14, "Poll: " + String(NodeCAN::currentPollingInterval) + "ms");
    
    display.setFont(ArialMT_Plain_16);
    display.setTextAlignment(TEXT_ALIGN_CENTER);
    
    String binaryString = "";
    for (int i = 7; i >= 0; i--) {
        binaryString += (snapshot.errorCode & (1 << i)) ? "1" : "0";
    }
    display.drawString(64, 30, binaryString);
    
    display.setFont(ArialMT_Plain_10);
    display.drawString(64, 50, "HEX VALUE: 0x" + String(snapshot.errorCode, HEX));
}

// 🕹️ DISCRETE 5-BUTTON LOGIC MATRIX
void NodeUI::handleButtonPush(uint8_t buttonId, bool isLongPress) {
    
    // 🏠 GLOBAL OVERRIDE: Home Button ALWAYS returns to main dashboard
    if (buttonId == BTN_HOME) { 
        activeMenuState = MENU_AUTO_SCROLL_DASHBOARD;
        NodeCAN::activeDiagnosticNode = 0; 
        return; 
    }

    switch (activeMenuState) {
        
        // --- LEVEL 1: DASHBOARD ---
        case MENU_AUTO_SCROLL_DASHBOARD:
            if (buttonId == BTN_ENTER) { 
                activeMenuState = MENU_DEVICE_DEEP_DIVE;
                selectedLmpId = currentLiveId; 
            }
            break;
            
        // --- LEVEL 2: DEVICE TELEMETRY ---
        case MENU_DEVICE_DEEP_DIVE:
            if (buttonId == BTN_ENTER) { 
                activeMenuState = MENU_DEVICE_DIAGS;
                NodeCAN::activeDiagnosticNode = selectedLmpId;
                NodeCAN::requestFreshDiagnostics(selectedLmpId); 
            } 
            else if (buttonId == BTN_BACK) {
                activeMenuState = MENU_AUTO_SCROLL_DASHBOARD;
            }
            else if (buttonId == BTN_UP || buttonId == BTN_DOWN) {
                uint8_t activeNodes[MAX_NODE_ID];
                uint8_t totalFound = NodeRegistry::getActiveNodesList(activeNodes);
                if (totalFound > 1) {
                    uint8_t currentIndex = 0;
                    for (uint8_t i = 0; i < totalFound; i++) {
                        if (activeNodes[i] == selectedLmpId) currentIndex = i;
                    }
                    
                    if (buttonId == BTN_UP) { 
                        currentIndex = (currentIndex + 1) % totalFound;
                    } else if (buttonId == BTN_DOWN) { 
                        currentIndex = (currentIndex == 0) ? (totalFound - 1) : (currentIndex - 1);
                    }
                    selectedLmpId = activeNodes[currentIndex];
                }
            }
            break;
            
        // --- LEVEL 3: DIAGNOSTICS ---
        case MENU_DEVICE_DIAGS:
            if (buttonId == BTN_BACK) { 
                activeMenuState = MENU_DEVICE_DEEP_DIVE;
                NodeCAN::activeDiagnosticNode = 0; // Stop Targeted Polling
            }
            else if (buttonId == BTN_UP) { 
                if (NodeCAN::currentPollingInterval <= 4500) {
                    NodeCAN::currentPollingInterval += 500;
                }
            }
            else if (buttonId == BTN_DOWN) { 
                if (NodeCAN::currentPollingInterval >= 1000) {
                    NodeCAN::currentPollingInterval -= 500;
                }
            }
            break;
            
        default:
            break;
    }
}

void NodeUI::runHMITask(void* pvParameters) {
    HmiEvent currentEvent;
    const TickType_t xRefreshPeriod = pdMS_TO_TICKS(200); 
    TickType_t xLastWakeTime = xTaskGetTickCount();

    for (;;) {
        BaseType_t eventReceived = xQueueReceive(xHmiQueue, &currentEvent, pdMS_TO_TICKS(50));
        if (eventReceived == pdTRUE && currentEvent.type == EVENT_BUTTON_CLICK) {
            handleButtonPush(currentEvent.buttonId, false);
        }

        if ((xTaskGetTickCount() - xLastWakeTime) >= xRefreshPeriod) {
            xLastWakeTime = xTaskGetTickCount();
            display.clear();
            
            NetworkState busState = NodeCAN::getBusState();
            
            if (activeMenuState != MENU_DEVICE_DEEP_DIVE && activeMenuState != MENU_DEVICE_DIAGS) {
                if (busState == STATE_STANDBY || busState == STATE_INIT_DISCOVERY) {
                    activeMenuState = MENU_WELCOME_SPLASH;
                } else if (busState == STATE_COLLECTING_REPLIES || busState == STATE_SEND_ACK_SEQUENTIAL || busState == STATE_RECHECK_WINDOW) {
                    activeMenuState = MENU_SCANNING_BUS;
                } else if (busState == STATE_OPERATIONAL_MODE) {
                    if (activeMenuState == MENU_WELCOME_SPLASH || activeMenuState == MENU_SCANNING_BUS) {
                        activeMenuState = MENU_AUTO_SCROLL_DASHBOARD;
                    }
                }
            } else {
                if (busState != STATE_OPERATIONAL_MODE) {
                     activeMenuState = MENU_SCANNING_BUS;
                     NodeCAN::activeDiagnosticNode = 0; 
                }
            }
            
            switch (activeMenuState) {
                case MENU_WELCOME_SPLASH: drawWelcomeSplash(); break;
                case MENU_SCANNING_BUS: drawScanningPage(); break;
                case MENU_AUTO_SCROLL_DASHBOARD: drawAutoDashboard(); break;
                case MENU_DEVICE_DEEP_DIVE: drawDeviceTelemetryPage(selectedLmpId); break;
                case MENU_DEVICE_DIAGS: drawDeviceDiagnosticPage(selectedLmpId); break;
                default: break;
            }
            display.display();
        }
    }
}