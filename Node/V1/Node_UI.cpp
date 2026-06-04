#include "Node_UI.h"
#include "Node_Registry.h"

// 📦 Define an internal event structure for the HMI event queue
enum HmiEventType {
    EVENT_BUTTON_CLICK,
    EVENT_PERIODIC_REFRESH
};

struct HmiEvent {
    HmiEventType type;
    uint8_t      buttonId; // Holds button pin or identifier index
    bool         isLong;   // Detects long-press configurations
};

// 🎛️ Static Member Instantiations
// Address 0x3C is standard for onboard Heltec SSD1306 screens over internal I2C (SDA=17, SCL=18)
SSD1306Wire NodeUI::display(0x3c, 17, 18); 
MenuLevel NodeUI::activeMenuState = MENU_AUTO_SCROLL_DASHBOARD;
uint8_t NodeUI::selectedDeviceIndex = 0;

// Internal FreeRTOS Inter-task communication queue handle
static QueueHandle_t xHmiQueue = NULL;

/**
 * @brief Simple hardware ISR (Interrupt Service Routine). 
 * Fires instantly when an operator touches a navigation input button.
 */
static void IRAM_ATTR hmiButtonISR(void* arg) {
    uint32_t pin = (uint32_t)arg;
    HmiEvent evt;
    evt.type = EVENT_BUTTON_CLICK;
    evt.buttonId = pin;
    evt.isLong = false;
    
    // 🚨 RTOS Rule: Use special "FromISR" variants when executing inside interrupts
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    xQueueSendFromISR(xHmiQueue, &evt, &xHigherPriorityTaskWoken);
    if (xHigherPriorityTaskWoken) {
        portYIELD_FROM_ISR();
    }
}

/**
 * @brief Boots screen hardware, configures text geometries, and maps inputs.
 */
void NodeUI::init() {
    // 1. Instantiate the background event processing queue (holds 10 events max)
    xHmiQueue = xQueueCreate(10, sizeof(HmiEvent));
    if (xHmiQueue == NULL) {
        Serial.println(F("[HMI Engine] CRITICAL: Event Queue Allocation Failed!"));
        return;
    }

    // 2. Wake up local Heltec physical display panel
    display.init();
    display.flipScreenVertically();
    display.setFont(ArialMT_Plain_10);
    
    // 3. Configure Input Navigation Buttons (Pins 1, 2, 3, 19, 20 as examples)
    // Adjust these definitions to match your layout or the Heltec onboard user button (PRG = Pin 0)
    uint8_t buttonPins[] = {0, 1, 2, 3}; 
    for(uint8_t pin : buttonPins) {
        pinMode(pin, INPUT_PULLUP);
        // Bind hardware lines directly into our optimized execution ISR router
        attachInterruptArg(digitalPinToInterrupt(pin), hmiButtonISR, (void*)pin, FALLING);
    }
    
    Serial.println(F("[HMI Engine] Display drivers and input maps online."));
}

/**
 * @brief Renders the static top bar area across dashboard spaces.
 */
void NodeUI::renderHeader() {
    display.setFont(ArialMT_Plain_10);
    display.setTextAlignment(TEXT_ALIGN_LEFT);
    display.drawString(0, 0, "HPCL NODE GATEWAY");
    
    display.setTextAlignment(TEXT_ALIGN_RIGHT);
    display.drawString(128, 0, "SS-A1"); // Substation profile mapping
    
    display.drawHorizontalLine(0, 12, 128);
}

/**
 * @brief Default Screen (MENU_AUTO_SCROLL_DASHBOARD):
 * Loops through active nodes showing live object values without operator interaction.
 */
void NodeUI::drawAutoDashboard() {
    renderHeader();
    
    uint8_t activeNodes[MAX_NODE_ID];
    uint8_t totalFound = NodeRegistry::getActiveNodesList(activeNodes);
    
    if (totalFound == 0) {
        display.setTextAlignment(TEXT_ALIGN_CENTER);
        display.setFont(ArialMT_Plain_10);
        display.drawString(64, 32, "SCANNING NETWORK...");
        display.drawString(64, 45, "[Waiting for LMPs]");
        return;
    }
    
    // Let the auto-scroller select a device index based on system runtime loops
    uint32_t activeIndex = (millis() / 3000) % totalFound; 
    uint8_t currentLmpId = activeNodes[activeIndex];
    
    LMPDataRecord snapshot;
    if (NodeRegistry::getNodeSnapshot(currentLmpId, snapshot)) {
        display.setFont(ArialMT_Plain_16);
        display.setTextAlignment(TEXT_ALIGN_LEFT);
        display.drawString(0, 16, "LMP ID: " + String(currentLmpId));
        
        display.setFont(ArialMT_Plain_10);
        display.drawString(0, 36, "Obj T: " + String(snapshot.objectTemp1, 1) + " C");
        display.drawString(0, 48, "Amb T: " + String(snapshot.ambientTemp, 1) + " C");
        
        display.setTextAlignment(TEXT_ALIGN_RIGHT);
        display.drawString(128, 48, "RH: " + String(snapshot.humidity, 0) + "%");
        
        // Show group profile validation marker
        display.drawString(128, 16, "GRP: " + String(snapshot.groupType));
    }
}

/**
 * @brief Deep Dive Screen (MENU_DEVICE_DEEP_DIVE):
 * Dedicated diagnostic view for a single selected device.
 */
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
    
    // Check if error codes exist and display an alert flag status indicator
    if (snapshot.errorCode != 0x00) {
        display.setTextAlignment(TEXT_ALIGN_RIGHT);
        display.drawString(128, 14, "ERR: 0x" + String(snapshot.errorCode, HEX));
    }
}

/**
 * @brief Central State Machine handling navigation inputs.
 */
void NodeUI::handleButtonPush(uint8_t buttonId, bool isLongPress) {
    // Basic navigation template routing logic. 
    // Example rule: Pin 0 (PRG Button) alternates tracking screen view planes
    if (buttonId == 0) {
        if (activeMenuState == MENU_AUTO_SCROLL_DASHBOARD) {
            activeMenuState = MENU_DEVICE_DEEP_DIVE;
            selectedDeviceIndex = 0; // Lock on first item entry
        } else {
            activeMenuState = MENU_AUTO_SCROLL_DASHBOARD;
        }
    }
    
    // Example rules for changing nodes while viewing the Deep Dive screen
    if (activeMenuState == MENU_DEVICE_DEEP_DIVE) {
        uint8_t activeNodes[MAX_NODE_ID];
        uint8_t totalFound = NodeRegistry::getActiveNodesList(activeNodes);
        
        if (totalFound > 0) {
            if (buttonId == 1) { // Up Pin increment mapping
                selectedDeviceIndex = (selectedDeviceIndex + 1) % totalFound;
            }
            if (buttonId == 2) { // Down Pin decrement mapping
                selectedDeviceIndex = (selectedDeviceIndex + totalFound - 1) % totalFound;
            }
        }
    }
}

/**
 * @brief The Continuous Execution Thread run by the FreeRTOS Scheduler on Core 0.
 */
void NodeUI::runHMITask(void* pvParameters) {
    HmiEvent currentEvent;
    
    // Configure a block check countdown value tracking loop intervals
    const TickType_t xRefreshPeriod = pdMS_TO_TICKS(200); // Target 5Hz refresh profile frequency
    TickType_t xLastWakeTime = xTaskGetTickCount();
    
    Serial.print(F("[HMI Task] Running on Core: "));
    Serial.println(xPortGetCoreID());

    // Primary execution worker processing loop
    for (;;) {
        
        // 📥 READ FROM QUEUE (Blocks thread until an event occurs or timeout fires)
        // By passing the remaining time window calculation, the loop remains highly deterministic
        BaseType_t eventReceived = xQueueReceive(xHmiQueue, &currentEvent, pdMS_TO_TICKS(50));
        
        if (eventReceived == pdTRUE) {
            if (currentEvent.type == EVENT_BUTTON_CLICK) {
                // An interrupt button press event was detected. Route directly to menu handler.
                handleButtonPush(currentEvent.buttonId, currentEvent.isLong);
            }
        }

        // 🖼️ REFRESH GRAPHICS ENVIRONMENT (Target baseline window verification)
        if ((xTaskGetTickCount() - xLastWakeTime) >= xRefreshPeriod) {
            xLastWakeTime = xTaskGetTickCount();
            
            // Clean graphics backbuffer
            display.clear();
            
            // Branch interface canvas views matching state assignments
            switch (activeMenuState) {
                case MENU_AUTO_SCROLL_DASHBOARD:
                    drawAutoDashboard();
                    break;
                    
                case MENU_DEVICE_DEEP_DIVE: {
                    uint8_t activeNodes[MAX_NODE_ID];
                    uint8_t totalFound = NodeRegistry::getActiveNodesList(activeNodes);
                    if (totalFound > 0 && selectedDeviceIndex < totalFound) {
                        drawDeviceTelemetryPage(activeNodes[selectedDeviceIndex]);
                    } else {
                        drawAutoDashboard(); // Fallback mode
                    }
                    }
                    break;
                    
                default:
                    break;
            }
            
            // Push final bit allocations to physical panel registers
            display.display();
        }
    }
}