#include "Node_UI.h"
#include "Node_Registry.h"
#include "Node_Storage.h"
#include "Node_CAN.h"

enum HmiEventType {
    EVENT_BUTTON_CLICK,
    EVENT_PERIODIC_REFRESH
};

struct HmiEvent {
    HmiEventType type;
    uint8_t      buttonId; 
    bool         isLong;   
};

SSD1306Wire NodeUI::display(0x3c, 500000, SDA_OLED, SCL_OLED, GEOMETRY_128_64, RST_OLED);
MenuLevel NodeUI::activeMenuState = MENU_WELCOME_SPLASH;
uint8_t NodeUI::selectedDeviceIndex = 0;
static uint8_t currentLiveIndex = 0;
static QueueHandle_t xHmiQueue = NULL;

static void IRAM_ATTR hmiButtonISR(void* arg) {
    uint32_t pin = (uint32_t)arg;
    HmiEvent evt;
    evt.type = EVENT_BUTTON_CLICK;
    evt.buttonId = pin;
    evt.isLong = false;
    
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    xQueueSendFromISR(xHmiQueue, &evt, &xHigherPriorityTaskWoken);
    if (xHigherPriorityTaskWoken) {
        portYIELD_FROM_ISR();
    }
}

void NodeUI::init() {
    xHmiQueue = xQueueCreate(10, sizeof(HmiEvent));
    if (xHmiQueue == NULL) return;

    pinMode(Vext, OUTPUT);
    digitalWrite(Vext, LOW); 
    delay(100);            

    display.init();
    display.flipScreenVertically();
    display.setFont(ArialMT_Plain_10);
    
    uint8_t buttonPins[] = {0}; 
    for(uint8_t pin : buttonPins) {
        pinMode(pin, INPUT_PULLUP);
        attachInterruptArg(digitalPinToInterrupt(pin), hmiButtonISR, (void*)pin, FALLING);
    }
    Serial.println(F("[HMI Engine] Core Graphics Infrastructure Deployed."));
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

/**
 * @brief 🚀 NEW: Renders Authorship & Splash info while background boots occur
 */
void NodeUI::drawWelcomeSplash() {
    display.setFont(ArialMT_Plain_16);
    display.setTextAlignment(TEXT_ALIGN_CENTER);
    display.drawString(64, 6, "HPCL MUMBAI");
    display.setFont(ArialMT_Plain_10);
    display.drawString(64, 26, "Substation Gateway v1.2");
    display.drawString(64, 40, "Dev: Chirag Kotian");
    display.drawString(64, 52, "[CAN Boot Initializing]");
}

/**
 * @brief 🚀 NEW: Renders precise percentage status of multi-stage network discovery
 */
void NodeUI::drawScanningPage() {
    renderHeader();
    display.setTextAlignment(TEXT_ALIGN_CENTER);
    display.setFont(ArialMT_Plain_10);
    display.drawString(64, 16, "SCANNING FIELD BUS...");
    
    // Calculate accurate completion bar math using exposed timestamp
    uint32_t elapsed = millis() - NodeCAN::discoveryStartTime;
    uint32_t totalExpectedWindow = DISCOVERY_WINDOW + 3500; // Combined window pacing
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
    currentLiveIndex = activeIndex;
    uint8_t currentLmpId = activeNodes[activeIndex];
    
    LMPDataRecord snapshot;
    if (NodeRegistry::getNodeSnapshot(currentLmpId, snapshot)) {
        display.setFont(ArialMT_Plain_16);
        display.setTextAlignment(TEXT_ALIGN_LEFT);
        display.drawString(0, 15, "LMP ID: " + String(currentLmpId));
        
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
    display.drawString(0, 28, "Pacing: " + String(NodeCAN::currentPollingInterval) + " ms");
    
    // Direct Hex Code layout output maps to maximize readability bounds
    display.setTextAlignment(TEXT_ALIGN_RIGHT);
    display.drawString(128, 14, "FLG: 0x" + String(snapshot.errorCode, HEX));
    
    display.setTextAlignment(TEXT_ALIGN_LEFT);
    display.drawHorizontalLine(0, 42, 128);
    
    // Concise bit tracking output strings layout row matrix
    String bitStatus = "B0(IR): " + String((snapshot.errorCode & (1 << 0)) ? "ERR" : "OK") + 
                       " | B2(ENV): " + String((snapshot.errorCode & (1 << 2)) ? "ERR" : "OK");
    display.drawString(0, 48, bitStatus);
}

void NodeUI::handleButtonPush(uint8_t buttonId, bool isLongPress) {
    if (buttonId == 0) { 
        if (activeMenuState == MENU_AUTO_SCROLL_DASHBOARD) {
            activeMenuState = MENU_DEVICE_DEEP_DIVE;
            selectedDeviceIndex = currentLiveIndex; 
        } 
        else if (activeMenuState == MENU_DEVICE_DEEP_DIVE) {
            activeMenuState = MENU_DEVICE_DIAGS;
            uint8_t activeNodes[MAX_NODE_ID];
            uint8_t totalFound = NodeRegistry::getActiveNodesList(activeNodes);
            if (totalFound > 0 && selectedDeviceIndex < totalFound) {
                NodeCAN::requestFreshDiagnostics(activeNodes[selectedDeviceIndex]);
            }
        } 
        else if (activeMenuState == MENU_DEVICE_DIAGS) {
            activeMenuState = MENU_AUTO_SCROLL_DASHBOARD; 
        }
    }
}

void NodeUI::runHMITask(void* pvParameters) {
    HmiEvent currentEvent;
    const TickType_t xRefreshPeriod = pdMS_TO_TICKS(200); 
    TickType_t xLastWakeTime = xTaskGetTickCount();

    for (;;) {
        BaseType_t eventReceived = xQueueReceive(xHmiQueue, &currentEvent, pdMS_TO_TICKS(50));
        if (eventReceived == pdTRUE && currentEvent.type == EVENT_BUTTON_CLICK) {
            handleButtonPush(currentEvent.buttonId, currentEvent.isLong);
        }

        if ((xTaskGetTickCount() - xLastWakeTime) >= xRefreshPeriod) {
            xLastWakeTime = xTaskGetTickCount();
            display.clear();
            
            // 📥 HARNESS HUD DRIVER STATES DIRECTLY TO NETWORK FIELD STATES
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
            }
            
            switch (activeMenuState) {
                case MENU_WELCOME_SPLASH:
                    drawWelcomeSplash();
                    break;
                case MENU_SCANNING_BUS:
                    drawScanningPage();
                    break;
                case MENU_AUTO_SCROLL_DASHBOARD:
                    drawAutoDashboard();
                    break;
                case MENU_DEVICE_DEEP_DIVE: {
                    uint8_t activeNodes[MAX_NODE_ID];
                    uint8_t totalFound = NodeRegistry::getActiveNodesList(activeNodes);
                    if (totalFound > 0 && selectedDeviceIndex < totalFound) {
                        drawDeviceTelemetryPage(activeNodes[selectedDeviceIndex]);
                    } else {
                        activeMenuState = MENU_AUTO_SCROLL_DASHBOARD;
                    }
                    break;
                }
                case MENU_DEVICE_DIAGS: { 
                    uint8_t activeNodes[MAX_NODE_ID];
                    uint8_t totalFound = NodeRegistry::getActiveNodesList(activeNodes);
                    if (totalFound > 0 && selectedDeviceIndex < totalFound) {
                        drawDeviceDiagnosticPage(activeNodes[selectedDeviceIndex]);
                    } else {
                        activeMenuState = MENU_AUTO_SCROLL_DASHBOARD;
                    }
                    break;
                }
                default:
                    break;
            }
            display.display();
        }
    }
}