#ifndef AHT20_driver_H
#define AHT20_driver_H

#include <Arduino.h>

class AHT20_sens
 {
public:
    static bool initAHT();
    static void readAHT(float& tempOut, float& humOut, bool& faultOut);
};

#endif