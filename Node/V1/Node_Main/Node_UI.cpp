#include "Node_UI.h"
#include "Node_Registry.h"
#include "Node_Storage.h"
#include "Node_CAN.h"
#include <time.h>
#include <sys/time.h>

// 🎛️ HARDWARE PIN DEFINITIONS (Safe ESP32-S3 Pins)
#define BTN_ENTER 1   
#define BTN_BACK  3   
#define BTN_UP    41 
#define BTN_DOWN  42 
#define BTN_HOME  47 

enum HmiEventType { EVENT_BUTTON_CLICK };
struct HmiEvent { HmiEventType type; uint8_t buttonId; };

// SSD1306Wire NodeUI::display(0x3c, 500000, SDA_OLED, SCL_OLED, GEOMETRY_128_64, RST_OLED);
SSD1306Wire NodeUI::display(0x3d, 500000, EXT_OLED_SDA, EXT_OLED_SCL, GEOMETRY_128_64, EXT_OLED_RST);
MenuLevel NodeUI::activeMenuState = MENU_WELCOME_SPLASH;

static uint8_t selectedLmpId = 0; 
static uint8_t currentLiveId = 0;
uint8_t NodeUI::sysHubCursorIndex = 0; 

// 🕒 RTC Setup Variables (DD, MM, YYYY, HH, MM, SS)
static uint8_t timeSetupCursor = 0; 
static int timeSetupValues[6] = {1, 1, 2024, 0, 0, 0}; 

// 🎯 LIVE POLLING CONFIGURATION TRACKER
static uint16_t editingPollRate = 4000; 

static QueueHandle_t xHmiQueue = NULL;
static uint32_t buttonPressStart[50] = {0}; 

// 🎯 SYNC LIVE LMP DATA WITH UI
void NodeUI::updateLivePollRate(uint8_t targetId, uint16_t rate) {
    if (activeMenuState == MENU_DEVICE_SETTINGS && selectedLmpId == targetId) {
        editingPollRate = rate;
    }
}

// 🧮 TIME MATH HELPERS
static uint8_t getMaxDays(uint8_t month, int year) {
    if (month == 2) { 
        if ((year % 4 == 0 && year % 100 != 0) || (year % 400 == 0)) return 29;
        return 28;
    }
    if (month == 4 || month == 6 || month == 9 || month == 11) return 30;
    return 31;
}

static String padZero(int val) {
    return (val < 10 ? "0" : "") + String(val);
}

// ⚡ HARDWARE INTERRUPT (Debounced for 150ms)
static void IRAM_ATTR hmiButtonISR(void* arg) {
    uint32_t pin = (uint32_t)arg;
    uint32_t currentTime = xTaskGetTickCountFromISR() * portTICK_PERIOD_MS;
    
    if ((currentTime - buttonPressStart[pin]) > 150) {
        buttonPressStart[pin] = currentTime;
        HmiEvent evt;
        evt.type = EVENT_BUTTON_CLICK;
        evt.buttonId = pin;
        
        BaseType_t xHigherPriorityTaskWoken = pdFALSE;
        xQueueSendFromISR(xHmiQueue, &evt, &xHigherPriorityTaskWoken);
        if (xHigherPriorityTaskWoken) portYIELD_FROM_ISR();
    }
}

void NodeUI::init() {
    xHmiQueue = xQueueCreate(15, sizeof(HmiEvent));
    if (xHmiQueue == NULL) return;

    pinMode(Vext, OUTPUT);
    digitalWrite(Vext, HIGH); // Cut power
    delay(200);               // Let capacitors drain completely
    digitalWrite(Vext, LOW);  // Restore power
    
    // ⏱️ Wait 250ms for the 3.3V rail to completely stabilize
    delay(250);           

    display.init();
    // display.flipScreenVertically();
    display.setFont(ArialMT_Plain_10);
    
    uint8_t buttonPins[] = {BTN_ENTER, BTN_BACK, BTN_UP, BTN_DOWN, BTN_HOME}; 
    for(uint8_t pin : buttonPins) {
        pinMode(pin, INPUT_PULLUP);
        attachInterruptArg(digitalPinToInterrupt(pin), hmiButtonISR, (void*)pin, FALLING); 
    }
    
    // 🕒 THE RTC BASELINE KICKSTART
    struct timeval tv;
    gettimeofday(&tv, NULL);
    if (tv.tv_sec < 10000) { 
        tv.tv_sec = 1704067200; // Epoch for 2024-01-01 00:00:00
        tv.tv_usec = 0;
        settimeofday(&tv, NULL);
    }
    
    Serial.println(F("[HMI Engine] LMP Config & System Hub Architecture Deployed."));
}

void NodeUI::renderHeader(const String& modeIndicator){
    uint8_t activeNodes[MAX_NODE_ID];
    uint8_t totalFound = NodeRegistry::getActiveNodesList(activeNodes);

    display.setFont(ArialMT_Plain_10);
    display.setTextAlignment(TEXT_ALIGN_LEFT);
    display.drawString(0, 0, "HPCL: " + String(totalFound) + " Nodes"); 
    
    display.setTextAlignment(TEXT_ALIGN_RIGHT);
    display.drawString(128, 0, "SS-A1 " + modeIndicator); 
    display.drawHorizontalLine(0, 12, 128);
}

void NodeUI::drawWelcomeSplash() {
    display.setFont(ArialMT_Plain_16);
    display.setTextAlignment(TEXT_ALIGN_CENTER);
    display.drawString(64, 6, "HPCL MUMBAI");
    display.setFont(ArialMT_Plain_10);
    display.drawString(64, 26, "Substation Gateway v2.6");
    display.drawString(64, 40, "Dev: Chirag Kotian");
    display.drawString(64, 52, "[CAN Boot Initializing]");
}

void NodeUI::drawScanningPage() {
    renderHeader("[S]");
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

// 📊 LMP TELEMETRY (Pages 1 & 2)
void NodeUI::drawTelemetryPage(uint8_t targetId, bool isAuto) {
    renderHeader(isAuto ? "[A]" : "[M]");
    
    LMPDataRecord snapshot;
    if (!NodeRegistry::getNodeSnapshot(targetId, snapshot)) {
        display.setTextAlignment(TEXT_ALIGN_CENTER);
        display.setFont(ArialMT_Plain_10);
        display.drawString(64, 30, "[ NO DEVICES ONLINE ]");
        return;
    }
    
    display.setFont(ArialMT_Plain_16);
    display.setTextAlignment(TEXT_ALIGN_LEFT);
    display.drawString(0, 15, "LMP ID: " + String(targetId));
    
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

// ⚙️ LMP DIAGNOSTICS & SETTINGS (Pages 3 & 4)
void NodeUI::drawDeviceDiagnosticPage(uint8_t targetId) {
    renderHeader("[D]");
    LMPDataRecord snapshot;
    
    if (!NodeRegistry::getNodeSnapshot(targetId, snapshot)) {
        display.setTextAlignment(TEXT_ALIGN_CENTER);
        display.drawString(64, 32, "NODE OFFLINE");
        return;
    }
    
    display.setFont(ArialMT_Plain_10);
    display.setTextAlignment(TEXT_ALIGN_LEFT);
    display.drawString(0, 14, "DIAGS -> NODE " + String(targetId));
    
    display.setFont(ArialMT_Plain_16);
    display.setTextAlignment(TEXT_ALIGN_CENTER);
    
    String binaryString = "";
    for (int i = 7; i >= 0; i--) { binaryString += (snapshot.errorCode & (1 << i)) ? "1" : "0"; }
    display.drawString(64, 30, binaryString);
    
    display.setFont(ArialMT_Plain_10);
    display.drawString(64, 50, "HEX VALUE: 0x" + String(snapshot.errorCode, HEX));
}

void NodeUI::drawDeviceSettingsPage(uint8_t targetId) {
    renderHeader("[⚙️]");
    display.setFont(ArialMT_Plain_10);
    display.setTextAlignment(TEXT_ALIGN_LEFT);
    display.drawString(0, 14, "SETTINGS -> NODE " + String(targetId));
    display.drawString(0, 30, "LMP Telemetry Stream Rate:");
    
    display.setFont(ArialMT_Plain_16);
    display.setTextAlignment(TEXT_ALIGN_CENTER);
    
    // 🎯 FORMAT TO SECONDS WITH 1 DECIMAL PLACE
    float displaySec = editingPollRate / 1000.0f;
    display.drawString(64, 45, String(displaySec, 1) + " sec");
}

// ==========================================
// 🌐 GATEWAY SYSTEM HUB UI 
// ==========================================
void NodeUI::drawSysHubPage() {
    renderHeader("[SYS]");
    display.setFont(ArialMT_Plain_10);
    display.setTextAlignment(TEXT_ALIGN_LEFT);
    display.drawString(0, 14, "GATEWAY CONTROL PANEL");
    
    String menuItems[] = {"1. System Health", "2. Gateway Diagnostics", "3. Configure RTC Time"};
    for (int i = 0; i < 3; i++) {
        if (i == sysHubCursorIndex) display.drawString(0, 28 + (i * 11), "> " + menuItems[i]); 
        else display.drawString(10, 28 + (i * 11), menuItems[i]);       
    }
}

void NodeUI::drawSysHealthPage() {
    renderHeader("[SYS]");
    display.setFont(ArialMT_Plain_10);
    display.setTextAlignment(TEXT_ALIGN_LEFT);
    display.drawString(0, 14, "NETWORK OVERVIEW");
    
    uint8_t activeNodes[MAX_NODE_ID];
    uint8_t totalFound = NodeRegistry::getActiveNodesList(activeNodes);
    
    // 🕒 FETCH THE LIVE RUNNING TIME FROM THE ESP32
    struct tm timeinfo;
    String liveTime = "--:--:--";
    String liveDate = "--/--/----";
    if(getLocalTime(&timeinfo, 10)) { 
        char timeStringBuff[10];
        strftime(timeStringBuff, sizeof(timeStringBuff), "%H:%M:%S", &timeinfo);
        liveTime = String(timeStringBuff);
        
        char dateStringBuff[12];
        strftime(dateStringBuff, sizeof(dateStringBuff), "%d/%m/%Y", &timeinfo);
        liveDate = String(dateStringBuff);
    }
    
    // Reorganized to make room for the full date and time string
    display.drawString(0, 26, "LMPs: " + String(totalFound));
    display.drawString(64, 26, "SD: " + String(NodeStorage::sdAvailable ? "OK" : "ERR"));
    
    display.drawString(0, 38, "CAN:  OK");
    display.drawString(64, 26, "LoRa: " + String(NodeLoRa::isSystemHealthy ? "OK" : "ERR"));
    
    display.setTextAlignment(TEXT_ALIGN_CENTER);
    display.drawString(64, 52, liveDate + "  " + liveTime); // E.g., "15/08/2026  14:30:00"
}

void NodeUI::drawSysDiagsPage() {
    renderHeader("[SYS]");
    display.setFont(ArialMT_Plain_10);
    display.setTextAlignment(TEXT_ALIGN_LEFT);
    display.drawString(0, 14, "GATEWAY DIAGNOSTICS");
    
    display.setFont(ArialMT_Plain_16);
    display.setTextAlignment(TEXT_ALIGN_CENTER);
    display.drawString(64, 30, "00000000"); 
    
    display.setFont(ArialMT_Plain_10);
    display.drawString(64, 50, "HEX VALUE: 0x00");
}

void NodeUI::drawSysTimeSetupPage() {
    renderHeader("[SYS]");
    display.setFont(ArialMT_Plain_10);
    display.setTextAlignment(TEXT_ALIGN_LEFT);
    display.drawString(0, 14, "SET GATEWAY RTC TIME");
    
    // Utilizing padZero() so it always looks clean: "05 / 09 / 2026"
    String d = (timeSetupCursor==0 ? ">" : "") + padZero(timeSetupValues[0]);
    String m = (timeSetupCursor==1 ? ">" : "") + padZero(timeSetupValues[1]);
    String y = (timeSetupCursor==2 ? ">" : "") + String(timeSetupValues[2]);
    
    String hr = (timeSetupCursor==3 ? ">" : "") + padZero(timeSetupValues[3]);
    String mn = (timeSetupCursor==4 ? ">" : "") + padZero(timeSetupValues[4]);
    String sc = (timeSetupCursor==5 ? ">" : "") + padZero(timeSetupValues[5]);

    display.setTextAlignment(TEXT_ALIGN_CENTER);
    display.drawString(64, 28, d + " / " + m + " / " + y);
    display.drawString(64, 42, hr + " : " + mn + " : " + sc);
    
    display.drawString(64, 54, "[ENTER=Next, BACK=Save]");
}

// 🕹️ THE COMBINED ROUTING MATRIX
void NodeUI::handleButtonPush(uint8_t buttonId) {
    
    if (buttonId == BTN_HOME) { 
        activeMenuState = MENU_AUTO_DASHBOARD;
        NodeCAN::activeDiagnosticNode = 0; 
        return; 
    }

    switch (activeMenuState) {
        
        // --- PAGE 1: AUTO DASHBOARD ---
        case MENU_AUTO_DASHBOARD:
            if (buttonId == BTN_ENTER) { 
                activeMenuState = MENU_MANUAL_TELEMETRY;
                selectedLmpId = currentLiveId; 
            }
            else if (buttonId == BTN_BACK) {
                activeMenuState = MENU_SYS_HUB;
                sysHubCursorIndex = 0;
            }
            break;
            
        // --- PAGE 2: MANUAL TELEMETRY ---
        case MENU_MANUAL_TELEMETRY:
            if (buttonId == BTN_ENTER) { 
                activeMenuState = MENU_DEVICE_DIAGS;
                NodeCAN::activeDiagnosticNode = selectedLmpId;
                NodeCAN::requestFreshDiagnostics(selectedLmpId); 
            } 
            else if (buttonId == BTN_BACK) {
                activeMenuState = MENU_AUTO_DASHBOARD;
            }
            else if (buttonId == BTN_UP || buttonId == BTN_DOWN) {
                uint8_t activeNodes[MAX_NODE_ID];
                uint8_t totalFound = NodeRegistry::getActiveNodesList(activeNodes);
                if (totalFound > 1) {
                    uint8_t currentIndex = 0;
                    for (uint8_t i = 0; i < totalFound; i++) { if (activeNodes[i] == selectedLmpId) currentIndex = i; }
                    if (buttonId == BTN_UP) currentIndex = (currentIndex + 1) % totalFound; 
                    else if (buttonId == BTN_DOWN) currentIndex = (currentIndex == 0) ? (totalFound - 1) : (currentIndex - 1); 
                    selectedLmpId = activeNodes[currentIndex];
                }
            }
            break;

        // --- PAGE 3: LMP DIAGNOSTICS ---
        case MENU_DEVICE_DIAGS:
            if (buttonId == BTN_ENTER) {
                activeMenuState = MENU_DEVICE_SETTINGS;
                // Pre-fill the localized variable with the currently known rate
                editingPollRate = NodeCAN::lmpPollRates[selectedLmpId]; 
                // Dispatch a request to the node to get the *live* truth
                NodeCAN::requestPollRate(selectedLmpId); 
            }
            else if (buttonId == BTN_BACK) { 
                activeMenuState = MENU_MANUAL_TELEMETRY;
                NodeCAN::activeDiagnosticNode = 0; 
            }
            else if (buttonId == BTN_UP || buttonId == BTN_DOWN) {
                uint8_t activeNodes[MAX_NODE_ID];
                uint8_t totalFound = NodeRegistry::getActiveNodesList(activeNodes);
                if (totalFound > 1) {
                    uint8_t currentIndex = 0;
                    for (uint8_t i = 0; i < totalFound; i++) { if (activeNodes[i] == selectedLmpId) currentIndex = i; }
                    if (buttonId == BTN_UP) currentIndex = (currentIndex + 1) % totalFound; 
                    else if (buttonId == BTN_DOWN) currentIndex = (currentIndex == 0) ? (totalFound - 1) : (currentIndex - 1); 
                    selectedLmpId = activeNodes[currentIndex];
                    NodeCAN::activeDiagnosticNode = selectedLmpId;
                    NodeCAN::requestFreshDiagnostics(selectedLmpId); 
                }
            }
            break;
            
        // --- PAGE 4: LMP SETTINGS ---
        case MENU_DEVICE_SETTINGS:
            if (buttonId == BTN_BACK) {
                // 🎯 DISPATCH NEW SETTING TO FIELD NODE AND SAVE
                NodeCAN::setPollRate(selectedLmpId, editingPollRate);
                activeMenuState = MENU_DEVICE_DIAGS;
            }
            else if (buttonId == BTN_UP && editingPollRate <= 59500) { 
                editingPollRate += 500; // Increase by 0.5s
            }
            else if (buttonId == BTN_DOWN && editingPollRate >= 3000) { 
                editingPollRate -= 500; // Decrease by 0.5s
            }
            break;

        // ==========================================
        // 🌐 GATEWAY SYSTEM MENUS
        // ==========================================
        case MENU_SYS_HUB:
            if (buttonId == BTN_BACK) activeMenuState = MENU_AUTO_DASHBOARD;
            else if (buttonId == BTN_UP && sysHubCursorIndex > 0) sysHubCursorIndex--;
            else if (buttonId == BTN_DOWN && sysHubCursorIndex < 2) sysHubCursorIndex++;
            else if (buttonId == BTN_ENTER) {
                if (sysHubCursorIndex == 0) activeMenuState = MENU_SYS_HEALTH;
                else if (sysHubCursorIndex == 1) activeMenuState = MENU_SYS_DIAGS;
                else if (sysHubCursorIndex == 2) {
                    activeMenuState = MENU_SYS_TIME_SETUP;
                    timeSetupCursor = 0; 
                    
                    // 🕒 PRE-FILL WITH LIVE OS CLOCK
                    struct tm timeinfo;
                    if(getLocalTime(&timeinfo, 10)) {
                        timeSetupValues[0] = timeinfo.tm_mday;
                        timeSetupValues[1] = timeinfo.tm_mon + 1;
                        timeSetupValues[2] = timeinfo.tm_year + 1900;
                        timeSetupValues[3] = timeinfo.tm_hour;
                        timeSetupValues[4] = timeinfo.tm_min;
                        timeSetupValues[5] = timeinfo.tm_sec;
                    }
                }
            }
            break;
            
        case MENU_SYS_HEALTH:
        case MENU_SYS_DIAGS:
            if (buttonId == BTN_BACK) activeMenuState = MENU_SYS_HUB;
            break;
            
        case MENU_SYS_TIME_SETUP:
            if (buttonId == BTN_BACK) {
                // 🕒 USER SAVED THE TIME: Inject it into the OS Clock
                struct tm t;
                t.tm_mday = timeSetupValues[0];
                t.tm_mon  = timeSetupValues[1] - 1;
                t.tm_year = timeSetupValues[2] - 1900;
                t.tm_hour = timeSetupValues[3];
                t.tm_min  = timeSetupValues[4];
                t.tm_sec  = timeSetupValues[5];
                
                time_t timeSinceEpoch = mktime(&t);
                struct timeval now;
                now.tv_sec = timeSinceEpoch;
                now.tv_usec = 0;
                settimeofday(&now, NULL);

                activeMenuState = MENU_SYS_HUB;
                Serial.println(F("💾 RTC Time Synced to internal ESP32 Hardware Timer."));
            }
            else if (buttonId == BTN_ENTER) {
                timeSetupCursor++;
                if (timeSetupCursor > 5) timeSetupCursor = 0; 
            }
            else if (buttonId == BTN_UP || buttonId == BTN_DOWN) {
                // 1. Change the Value
                if (buttonId == BTN_UP) timeSetupValues[timeSetupCursor]++;
                else timeSetupValues[timeSetupCursor]--;

                // 2. Apply "Smart Wrap-Around" Bounds 
                if (timeSetupCursor == 0) { // Day
                    uint8_t maxD = getMaxDays(timeSetupValues[1], timeSetupValues[2]);
                    if (timeSetupValues[0] > maxD) timeSetupValues[0] = 1;
                    else if (timeSetupValues[0] < 1) timeSetupValues[0] = maxD;
                }
                else if (timeSetupCursor == 1) { // Month
                    if (timeSetupValues[1] > 12) timeSetupValues[1] = 1;
                    else if (timeSetupValues[1] < 1) timeSetupValues[1] = 12;
                    uint8_t maxD = getMaxDays(timeSetupValues[1], timeSetupValues[2]);
                    if (timeSetupValues[0] > maxD) timeSetupValues[0] = maxD;
                }
                else if (timeSetupCursor == 2) { // Year
                    if (timeSetupValues[2] > 2099) timeSetupValues[2] = 2024;
                    else if (timeSetupValues[2] < 2024) timeSetupValues[2] = 2099;
                    uint8_t maxD = getMaxDays(timeSetupValues[1], timeSetupValues[2]);
                    if (timeSetupValues[0] > maxD) timeSetupValues[0] = maxD;
                }
                else if (timeSetupCursor == 3) { // Hour
                    if (timeSetupValues[3] > 23) timeSetupValues[3] = 0;
                    else if (timeSetupValues[3] < 0) timeSetupValues[3] = 23;
                }
                else if (timeSetupCursor == 4 || timeSetupCursor == 5) { // Min/Sec
                    if (timeSetupValues[timeSetupCursor] > 59) timeSetupValues[timeSetupCursor] = 0;
                    else if (timeSetupValues[timeSetupCursor] < 0) timeSetupValues[timeSetupCursor] = 59;
                }
            }
            break;

        default: break;
    }
}

void NodeUI::runHMITask(void* pvParameters) {
    HmiEvent currentEvent;
    const TickType_t xRefreshPeriod = pdMS_TO_TICKS(200); 
    TickType_t xLastWakeTime = xTaskGetTickCount();

    for (;;) {
        BaseType_t eventReceived = xQueueReceive(xHmiQueue, &currentEvent, pdMS_TO_TICKS(50));
        if (eventReceived == pdTRUE && currentEvent.type == EVENT_BUTTON_CLICK) {
            handleButtonPush(currentEvent.buttonId);
        }

        if ((xTaskGetTickCount() - xLastWakeTime) >= xRefreshPeriod) {
            xLastWakeTime = xTaskGetTickCount();
            display.clear();
            
            NetworkState busState = NodeCAN::getBusState();
            
            if (activeMenuState == MENU_WELCOME_SPLASH || activeMenuState == MENU_SCANNING_BUS || activeMenuState == MENU_AUTO_DASHBOARD) {
                if (busState == STATE_STANDBY || busState == STATE_INIT_DISCOVERY) {
                    activeMenuState = MENU_WELCOME_SPLASH;
                } else if (busState == STATE_COLLECTING_REPLIES || busState == STATE_SEND_ACK_SEQUENTIAL || busState == STATE_RECHECK_WINDOW) {
                    activeMenuState = MENU_SCANNING_BUS;
                } else if (busState == STATE_OPERATIONAL_MODE) {
                    activeMenuState = MENU_AUTO_DASHBOARD;
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
                case MENU_AUTO_DASHBOARD: {
                    uint8_t activeNodes[MAX_NODE_ID];
                    uint8_t totalFound = NodeRegistry::getActiveNodesList(activeNodes);
                    if (totalFound > 0) {
                        uint32_t activeIndex = (millis() / 3000) % totalFound; 
                        currentLiveId = activeNodes[activeIndex];
                        drawTelemetryPage(currentLiveId, true);
                    } else {
                        drawTelemetryPage(0, true);
                    }
                    break;
                }
                case MENU_MANUAL_TELEMETRY: drawTelemetryPage(selectedLmpId, false); break;
                case MENU_DEVICE_DIAGS: drawDeviceDiagnosticPage(selectedLmpId); break;
                case MENU_DEVICE_SETTINGS: drawDeviceSettingsPage(selectedLmpId); break;
                
                // 🌐 GATEWAY SYSTEM PAGES
                case MENU_SYS_HUB: drawSysHubPage(); break;
                case MENU_SYS_HEALTH: drawSysHealthPage(); break;
                case MENU_SYS_DIAGS: drawSysDiagsPage(); break;
                case MENU_SYS_TIME_SETUP: drawSysTimeSetupPage(); break;
            }
            display.display();
        }
    }
}