#pragma once

#include "body.hpp"

// A 2D point on the screen/image.
struct ScreenPoint {
    double x = 0.0;
    double y = 0.0;
};

// Projection converts 3D positions into a simple angled 2D view.
class Projection {
public:
    Projection(double scale, double heightScale)
        : scale_(scale), heightScale_(heightScale) {
    }

    ScreenPoint project(const Vec3& p) const {
        ScreenPoint s;

        // Simple angled projection.
        s.x = (p.x - p.y) * scale_;
        s.y = (p.x + p.y) * 0.25 * scale_ - p.z * heightScale_;

        return s;
    }

private:
    double scale_ = 1.0;
    double heightScale_ = 1.0;
};