#ifndef MLX_driver_H
#define MLX_driver_H

#include <Arduino.h>

class MLX_sens {
public:
    static bool initMLX(uint8_t addr = 0x5A);
    static void readMLX(uint8_t addr, float& objOut, float& ambOut, bool& faultOut);
};

#endif