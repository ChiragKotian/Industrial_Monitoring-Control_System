#ifndef NODE_UI_H
#define NODE_UI_H

#include <Arduino.h>
#include <Wire.h>
#include "HT_SSD1306Wire.h"
#include "Node_Config.h"

enum MenuLevel {
    MENU_EMERGENCY_OVERLAY = 0,
    MENU_AUTO_SCROLL_DASHBOARD,
    MENU_DEVICE_HUB,
    MENU_DEVICE_DEEP_DIVE,
    MENU_SYSTEM_SETTINGS
};

class NodeUI {
private:
    static SSD1306Wire display;
    static MenuLevel activeMenuState;
    static uint8_t selectedDeviceIndex;
    
    // Rendering Sub-layers
    static void renderHeader();
    static void drawEmergencyOverlay();
    static void drawAutoDashboard();
    static void drawDeviceHubList();
    static void drawDeviceTelemetryPage(uint8_t targetId);

public:
    static void init();
    static void handleButtonPush(uint8_t buttonId, bool isLongPress);
    
    static void runHMITask(void* pvParameters);
};

#endif