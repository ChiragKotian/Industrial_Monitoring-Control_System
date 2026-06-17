#include "Node_UI.h"
#include "Node_Registry.h"
#include "Node_Storage.h"

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
SSD1306Wire NodeUI::display(0x3c, 500000, SDA_OLED, SCL_OLED, GEOMETRY_128_64, RST_OLED);
MenuLevel NodeUI::activeMenuState = MENU_AUTO_SCROLL_DASHBOARD;
uint8_t NodeUI::selectedDeviceIndex = 0;

static uint8_t currentLiveIndex = 0;

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

    // ⚡ STEP 1.5: PHYSICAL BOOT SEQUENCE FOR HELTEC V3 ONBOARD SCREEN
    // A. Supply power to the OLED panel regulator via the Vext Rail
    pinMode(Vext, OUTPUT);
    digitalWrite(Vext, LOW); // LOW turns on the MOSFET switch to supply 3.3V
    delay(100);              // Crucial stabilization window for voltage lines

    // 2. Wake up local Heltec physical display panel
    display.init();
    display.flipScreenVertically();
    display.setFont(ArialMT_Plain_10);
    
    // 3. Configure Input Navigation Buttons (Pins 1, 2, 3, 19, 20 as examples)
    // Adjust these definitions to match your layout or the Heltec onboard user button (PRG = Pin 0)
    uint8_t buttonPins[] = {0}; 
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
void NodeUI::renderHeader(){
    uint8_t activeNodes[MAX_NODE_ID];
    uint8_t totalFound = NodeRegistry::getActiveNodesList(activeNodes);

    display.setFont(ArialMT_Plain_10);
    display.setTextAlignment(TEXT_ALIGN_LEFT);
    display.drawString(0, 0, "HPCL REGISTRY: " + String(totalFound)); // 🚀 Shows live registry count!
    
    display.setTextAlignment(TEXT_ALIGN_RIGHT);
    display.drawString(128, 0, "SS-A1"); 
    
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
        display.drawString(64, 25, "SCANNING NETWORK...");
        display.drawString(64, 40, "[Waiting for LMPs]");
        return;
    }
    
    // Smoothly cycle through the list of all nodes that successfully passed discovery parameters
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
        
        // 🎛️ DYNAMIC REFINERY PROFILE GRAPHICS MATRIX
        if (snapshot.groupType == 1) { 
            // Group 1: Single IR Panel view
            display.drawString(0, 32, "IR Probe 1: " + String(snapshot.objectTemp1, 1) + " C");
            display.drawString(0, 46, "Case Amb:   " + String(snapshot.ambientTemp, 1) + " C");
            display.setTextAlignment(TEXT_ALIGN_RIGHT);
            display.drawString(128, 46, "SNG-IR");
        } 
        else if (snapshot.groupType == 2) { 
            // Group 2: IR + High-Precision Environmental Probe
            display.drawString(0, 32, "IR Target:  " + String(snapshot.objectTemp1, 1) + " C");
            display.drawString(0, 46, "Prec Amb:   " + String(snapshot.ambientTemp, 1) + " C");
            display.setTextAlignment(TEXT_ALIGN_RIGHT);
            display.drawString(128, 46, "RH: " + String(snapshot.humidity, 0) + "%");
        }
        else if (snapshot.groupType == 3) {
            // Group 3: Dual-Zone Transformer/Busbar Differential
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
            selectedDeviceIndex = currentLiveIndex; // Lock on first item entry
        } else {
            activeMenuState = MENU_AUTO_SCROLL_DASHBOARD;
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