#pragma once

#include <cstdint>
#include <string>
#include <vector>

struct Vec3 {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;

    Vec3 operator+(const Vec3& other) const {
        return {x + other.x, y + other.y, z + other.z};
    }

    Vec3 operator-(const Vec3& other) const {
        return {x - other.x, y - other.y, z - other.z};
    }

    Vec3 operator*(double s) const {
        return {x * s, y * s, z * s};
    }

    Vec3& operator+=(const Vec3& other) {
        x += other.x;
        y += other.y;
        z += other.z;
        return *this;
    }

    Vec3& operator-=(const Vec3& other) {
        x -= other.x;
        y -= other.y;
        z -= other.z;
        return *this;
    }

    Vec3& operator*=(double s) {
        x *= s;
        y *= s;
        z *= s;
        return *this;
    }
};

struct Body {
    std::uint32_t id = 0;
    std::string name;

    Vec3 pos;
    Vec3 vel;

    float mag = 10.0f;
    float colorIndex = 0.0f;

    bool isSun = false;
    bool hasMotion = true;
};

class Catalog {
public:
    bool loadCsv(const std::string& path);
    const std::vector<Body>& bodies() const { return bodies_; }

private:
    std::vector<Body> bodies_;
};