#ifndef NODE_LORA_H
#define NODE_LORA_H

#include <Arduino.h>
#include <RadioLib.h>

class NodeLoRa {
public:
    static void init();
    static void transmitTelemetry(String payload);
    static void runLoRaWorker(void* pvParameters);
    static bool isSystemHealthy;
};

#endif