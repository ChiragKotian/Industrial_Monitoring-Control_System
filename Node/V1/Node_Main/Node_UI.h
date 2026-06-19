#ifndef NODE_UI_H
#define NODE_UI_H

#include <Arduino.h>
#include <Wire.h>
#include "HT_SSD1306Wire.h"
#include "Node_Config.h"

enum MenuLevel {
    MENU_WELCOME_SPLASH = 0,    // 🚀 STEP 1
    MENU_SCANNING_BUS,          // 🚀 STEP 2
    MENU_AUTO_SCROLL_DASHBOARD, // 🚀 STEP 3
    MENU_DEVICE_DEEP_DIVE,
    MENU_DEVICE_DIAGS,
    MENU_SYSTEM_SETTINGS
};

class NodeUI {
private:
    static SSD1306Wire display;
    static MenuLevel activeMenuState;
    static uint8_t selectedDeviceIndex;
    
    static void renderHeader();
    static void drawWelcomeSplash();   // 🚀 Added
    static void drawScanningPage();    // 🚀 Added
    static void drawAutoDashboard();
    static void drawDeviceTelemetryPage(uint8_t targetId);
    static void drawDeviceDiagnosticPage(uint8_t targetId); 

public:
    static void init();
    static void handleButtonPush(uint8_t buttonId, bool isLongPress);
    static void runHMITask(void* pvParameters);
};

#endif