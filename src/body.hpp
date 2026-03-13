#pragma once
#include <<cstdint>
#include <string>


struct Vec3{
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
};


struct Body {
    uint32_t id = 0;
    std::string name;


    Vec3 pos;
    Vec3 vel;

    float mag = 0.0f;
    float colorIndex = 0.0f;

    bool hasMotion = false;
}