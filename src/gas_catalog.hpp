#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "catalog.hpp"

struct GasPoint {
    Vec3 pos;
    float density = 0.0f;
};

struct GasCatalogConfig {
    double intensityThreshold = 2.5;
    double velocityDistanceScalePcPerKmS = 65.0;
    std::size_t maxPoints = 75000;
    int longitudeStride = 2;
    int latitudeStride = 2;
    int velocityStride = 2;
};

class GasCatalog {
public:
    bool loadFits(const std::string& path, const GasCatalogConfig& config = {});
    const std::vector<GasPoint>& points() const { return points_; }
    double maxDensity() const { return maxDensity_; }

private:
    std::vector<GasPoint> points_;
    double maxDensity_ = 0.0;
};