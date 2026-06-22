#ifndef NODE_UI_H
#define NODE_UI_H

#include <Arduino.h>
#include <Wire.h>
#include "HT_SSD1306Wire.h"
#include "Node_Config.h"

enum MenuLevel {
    MENU_WELCOME_SPLASH = 0,    
    MENU_SCANNING_BUS,          
    MENU_AUTO_DASHBOARD,        // Page 1: Auto-scrolling LMP telemetry
    MENU_MANUAL_TELEMETRY,      // Page 2: Manual LMP telemetry view
    MENU_DEVICE_DIAGS,          // Page 3: LMP Diagnostics (Hex error code)
    MENU_DEVICE_SETTINGS,       // Page 4: LMP Polling Settings
    
    // 🌐 NEW: Gateway System Menu States
    MENU_SYS_HUB,               // Gateway Main Menu
    MENU_SYS_HEALTH,            // Gateway Overview (LMPs, SD, CAN, LoRa)
    MENU_SYS_DIAGS,             // Gateway Internal Diagnostics
    MENU_SYS_TIME_SETUP         // Gateway RTC Configuration
};

class NodeUI {
private:
    static SSD1306Wire display;
    
    // LMP Rendering Sub-layers
    static void renderHeader(const String& modeIndicator);
    static void drawWelcomeSplash();   
    static void drawScanningPage();    
    static void drawTelemetryPage(uint8_t targetId, bool isAuto);
    static void drawDeviceDiagnosticPage(uint8_t targetId); 
    static void drawDeviceSettingsPage(uint8_t targetId);

    // 🌐 NEW: Gateway System Rendering Sub-layers
    static void drawSysHubPage();
    static void drawSysHealthPage();
    static void drawSysDiagsPage();
    static void drawSysTimeSetupPage();

public:
    static MenuLevel activeMenuState;
    static uint8_t sysHubCursorIndex;

    static void init();
    static void handleButtonPush(uint8_t buttonId);
    static void runHMITask(void* pvParameters);
};

#endif