#include "simulation.hpp"

#include <cmath>
#include <stdexcept>

namespace {
constexpr double kG = 4.498502151469554e-15; // pc^3 / (Msun * yr^2)
constexpr Vec3 kSunGalacticPosPc{-8200.0, 0.0, 20.8};

struct GalaxyParams {
    double bulgeMassMsun = 5.0e9;
    double bulgeScalePc = 350.0;

    double diskMassMsun = 6.8e10;
    double diskApc = 3000.0;
    double diskBpc = 280.0;

    double haloV0PcPerYear = 220.0 * 1.022712165045695e-6;
    double haloScalePc = 12000.0;

    double softeningPc = 5.0;
};

double lengthSquared(const Vec3& v) {
    return v.x * v.x + v.y * v.y + v.z * v.z;
}

Vec3 bulgeAcceleration(const Vec3& p, const GalaxyParams& g) {
    const double r2 = lengthSquared(p) + g.softeningPc * g.softeningPc;
    const double c2 = g.bulgeScalePc * g.bulgeScalePc;
    const double denom = std::pow(r2 + c2, 1.5);
    return {-kG * g.bulgeMassMsun * p.x / denom,
            -kG * g.bulgeMassMsun * p.y / denom,
            -kG * g.bulgeMassMsun * p.z / denom};
}

Vec3 diskAcceleration(const Vec3& p, const GalaxyParams& g) {
    const double R2 = p.x * p.x + p.y * p.y;
    const double zz = std::sqrt(p.z * p.z + g.diskBpc * g.diskBpc);
    const double B = g.diskApc + zz;
    const double denom = std::pow(R2 + B * B, 1.5);

    const double factor = -kG * g.diskMassMsun / denom;
    const double ax = factor * p.x;
    const double ay = factor * p.y;
    double az = 0.0;
    if (zz > 1e-12) {
        az = factor * B * p.z / zz;
    }
    return {ax, ay, az};
}

Vec3 haloAcceleration(const Vec3& p, const GalaxyParams& g) {
    const double r2 = lengthSquared(p);
    const double denom = r2 + g.haloScalePc * g.haloScalePc;
    const double factor = -(g.haloV0PcPerYear * g.haloV0PcPerYear) / denom;
    return {factor * p.x, factor * p.y, factor * p.z};
}

Vec3 galaxyAcceleration(const Vec3& galPosPc, const GalaxyParams& g) {
    const Vec3 a1 = bulgeAcceleration(galPosPc, g);
    const Vec3 a2 = diskAcceleration(galPosPc, g);
    const Vec3 a3 = haloAcceleration(galPosPc, g);
    return {a1.x + a2.x + a3.x, a1.y + a2.y + a3.y, a1.z + a2.z + a3.z};
}

Vec3 relativeAcceleration(const Vec3& heliocentricPosPc, const GalaxyParams& g) {
    const Vec3 sunAcc = galaxyAcceleration(kSunGalacticPosPc, g);
    const Vec3 starAcc = galaxyAcceleration(kSunGalacticPosPc + heliocentricPosPc, g);
    return starAcc - sunAcc;
}

} // namespace

void Simulation::setBodies(std::vector<Body> bodies) {
    bodies_ = std::move(bodies);
}

SimulationResult Simulation::precompute(
    const SimulationConfig& config,
    const std::function<void(int completedFrames, int totalFrames)>& onProgress) const {
    if (config.frameCount < 1) {
        throw std::runtime_error("frameCount must be at least 1");
    }
    if (config.substepsPerFrame < 1) {
        throw std::runtime_error("substepsPerFrame must be at least 1");
    }

    GalaxyParams galaxy;
    SimulationResult result;
    result.bodies = bodies_;
    result.yearsPerFrame = config.yearsPerFrame;
    result.frames.reserve(static_cast<std::size_t>(config.frameCount));

    const double h = config.yearsPerFrame / static_cast<double>(config.substepsPerFrame);

    for (int frame = 0; frame < config.frameCount; ++frame) {
        std::vector<Vec3> positions;
        positions.reserve(result.bodies.size());
        for (const auto& body : result.bodies) {
            positions.push_back(body.pos);
        }
        result.frames.push_back(std::move(positions));

        if (onProgress) {
            onProgress(frame + 1, config.frameCount);
        }

        if (frame + 1 == config.frameCount) {
            break;
        }

        for (int sub = 0; sub < config.substepsPerFrame; ++sub) {
            for (auto& body : result.bodies) {
                if (body.isSun || !body.hasMotion) {
                    continue;
                }
                const Vec3 acc = relativeAcceleration(body.pos, galaxy);
                body.vel += acc * h;
                body.pos += body.vel * h;
            }
        }
    }

    return result;
}