#include <SPI.h>
#include <SD.h>
#include <mcp2515.h>

// SHARED PINS
#define SHARED_SCK    4
#define SHARED_MOSI   6

// SEPARATE MISO PINS
#define CAN_MISO      5
#define SD_MISO       48

// CHIP SELECTS
#define CAN_CS        7
#define SD_CS         26

SPIClass hspiShared(HSPI);
MCP2515 mcp2515(CAN_CS, 4000000, &hspiShared);

// ---------------------------------------------------------
// Switch bus to SD card: full re-init, not just pin remap.
// ---------------------------------------------------------
bool switchToSD() {
    digitalWrite(CAN_CS, HIGH); // deselect CAN first
    hspiShared.end();
    delayMicroseconds(50);
    hspiShared.begin(SHARED_SCK, SD_MISO, SHARED_MOSI, SD_CS);

    // CRITICAL: SD.begin() has a "return true if already mounted"
    // guard that skips re-init on repeat calls. Force a real
    // remount every time by ending it first.
    SD.end();
    bool ok = SD.begin(SD_CS, hspiShared, 4000000);
    return ok;
}

// ---------------------------------------------------------
// Switch bus to CAN: full re-init, not just pin remap.
// ---------------------------------------------------------
bool switchToCAN() {
    digitalWrite(SD_CS, HIGH); // deselect SD first
    hspiShared.end();
    delayMicroseconds(50);
    hspiShared.begin(SHARED_SCK, CAN_MISO, SHARED_MOSI, CAN_CS);

    mcp2515.reset();
    bool ok = (mcp2515.setBitrate(CAN_250KBPS, MCP_8MHZ) == MCP2515::ERROR_OK);
    if (ok) mcp2515.setNormalMode();
    return ok;
}

void drainCanRx() {
    struct can_frame rxFrame;
    while (mcp2515.readMessage(&rxFrame) == MCP2515::ERROR_OK) {
        // handle/log received frames here if needed
    }
    uint8_t err = mcp2515.getErrorFlags();
    if (err & 0xC0) { // RX0OVR (bit6) | RX1OVR (bit7)
        Serial.print(F("[CAN OVERFLOW! clearing] "));
        mcp2515.clearRXnOVRFlags();
        mcp2515.clearMERR();
    }
}

void setup() {
    Serial.begin(115200);
    delay(2000);

    Serial.println(F("\n======================================="));
    Serial.println(F(" RESYNC-ON-SWITCH PIN HOPPING (v2) "));
    Serial.println(F("======================================="));

    pinMode(CAN_CS, OUTPUT); digitalWrite(CAN_CS, HIGH);
    pinMode(SD_CS, OUTPUT);  digitalWrite(SD_CS, HIGH);

    // Give the card time to finish its power-up sequence.
    // Cheap cards especially can need 10-50ms+ after Vcc is stable
    // before they'll respond correctly to CMD0.
    delay(100);

    Serial.print(F("-> Initial SD mount... "));
    bool sdOk = switchToSD();
    // Retry a couple times if the very first attempt fails --
    // this is normal on some cards/boards at cold power-up.
    for (int i = 0; i < 3 && !sdOk; i++) {
        delay(50);
        sdOk = switchToSD();
    }
    Serial.println(sdOk ? F("SUCCESS") : F("FAILED"));

    Serial.print(F("-> Initial CAN init... "));
    Serial.println(switchToCAN() ? F("SUCCESS") : F("FAILED"));

    Serial.println(F("\n--- SYSTEM ARBITRATION ACTIVE ---"));
}

void loop() {
    static uint32_t loopCounter = 0;
    loopCounter++;
    Serial.print(F("Cycle [")); Serial.print(loopCounter); Serial.print(F("]: "));

    // ---------- SD CARD ----------
    if (switchToSD()) {
        digitalWrite(SD_CS, LOW);
        File testFile = SD.open("/spi_test.txt", FILE_APPEND);
        if (testFile) {
            testFile.println("Seamless Pin Hopping Successful");
            testFile.flush();
            testFile.close();
            Serial.print(F("SD Write: OK | "));
        } else {
            Serial.print(F("SD Write: ERR (open failed) | "));
        }
        digitalWrite(SD_CS, HIGH);
    } else {
        Serial.print(F("SD Write: ERR (mount failed) | "));
    }

    // ---------- CAN BUS ----------
    if (switchToCAN()) {
        digitalWrite(CAN_CS, LOW);
        drainCanRx();

        struct can_frame testFrame;
        testFrame.can_id = 0x88;
        testFrame.can_dlc = 1;
        testFrame.data[0] = 0xFF;

        if (mcp2515.sendMessage(&testFrame) == MCP2515::ERROR_OK) {
            Serial.print(F("CAN TX: OK | "));
        } else {
            Serial.print(F("CAN TX: NO ACK | "));
        }
        Serial.print(F("TEC=")); Serial.print(mcp2515.errorCountTX());
        Serial.print(F(" REC=")); Serial.println(mcp2515.errorCountRX());
        digitalWrite(CAN_CS, HIGH);
    } else {
        Serial.println(F("CAN TX: ERR (init failed)"));
    }

    delay(1000);
}