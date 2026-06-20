#ifndef NODE_UI_H
#define NODE_UI_H

#include <Arduino.h>
#include <Wire.h>
#include "HT_SSD1306Wire.h"
#include "Node_Config.h"

enum MenuLevel {
    MENU_WELCOME_SPLASH = 0,    
    MENU_SCANNING_BUS,          
    MENU_AUTO_DASHBOARD,        // Page 1
    MENU_MANUAL_TELEMETRY,      // Page 2
    MENU_DEVICE_DIAGS,          // Page 3
    MENU_DEVICE_SETTINGS        // Page 4
};

class NodeUI {
private:
    static SSD1306Wire display;
    
    // Rendering Sub-layers
    static void renderHeader(const String& modeIndicator);
    static void drawWelcomeSplash();   
    static void drawScanningPage();    
    static void drawTelemetryPage(uint8_t targetId, bool isAuto);
    static void drawDeviceDiagnosticPage(uint8_t targetId); 
    static void drawDeviceSettingsPage(uint8_t targetId);

public:
    static MenuLevel activeMenuState;
    static void init();
    static void handleButtonPush(uint8_t buttonId);
    static void runHMITask(void* pvParameters);
};

#endif