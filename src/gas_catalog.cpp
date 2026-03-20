#include "gas_catalog.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <limits>
#include <random>
#include <stdexcept>
#include <string>
#include <unordered_map>

namespace {
constexpr double kPi = 3.14159265358979323846;
constexpr double kDegToRad = kPi / 180.0;
constexpr std::size_t kFitsBlockSize = 2880;

struct FitsAxisInfo {
    long length = 1;
    double crval = 0.0;
    double crpix = 1.0;
    double cdelt = 1.0;
    std::string ctype;
    std::string cunit;
};

struct FitsCubeInfo {
    int bitpix = 0;
    int naxis = 0;
    std::array<FitsAxisInfo, 3> axes{};
    double bscale = 1.0;
    double bzero = 0.0;
    bool hasBlank = false;
    long blankValue = 0;
    std::streamoff dataOffset = 0;
};

std::string trim(const std::string& text) {
    std::size_t begin = 0;
    while (begin < text.size() && std::isspace(static_cast<unsigned char>(text[begin]))) {
        ++begin;
    }

    std::size_t end = text.size();
    while (end > begin && std::isspace(static_cast<unsigned char>(text[end - 1]))) {
        --end;
    }

    return text.substr(begin, end - begin);
}

std::string stripQuotes(const std::string& text) {
    if (text.size() >= 2 && text.front() == '\'' && text.back() == '\'') {
        return text.substr(1, text.size() - 2);
    }
    return text;
}

std::string normalizeUnit(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return trim(text);
}

double linearAxisValue(const FitsAxisInfo& axis, long zeroBasedIndex) {
    return axis.crval + ((static_cast<double>(zeroBasedIndex) + 1.0) - axis.crpix) * axis.cdelt;
}

double angleToRadians(double value, const std::string& unit) {
    const std::string normalized = normalizeUnit(unit);
    if (normalized.find("rad") != std::string::npos) {
        return value;
    }
    return value * kDegToRad;
}

double velocityToKmPerSec(double value, const std::string& unit) {
    const std::string normalized = normalizeUnit(unit);
    if (normalized.find("m/s") != std::string::npos) {
        return value / 1000.0;
    }
    return value;
}

Vec3 galactocentricFromSpherical(double longitudeDeg,
                                 double latitudeDeg,
                                 double distancePc,
                                 const std::string& longitudeUnit,
                                 const std::string& latitudeUnit) {
    const double longitudeRad = angleToRadians(longitudeDeg, longitudeUnit);
    const double latitudeRad = angleToRadians(latitudeDeg, latitudeUnit);
    const double cosLatitude = std::cos(latitudeRad);

    // Treat the velocity-derived distance as a galactocentric radius so
    // clouds are distributed around the galactic center, not around the Sun.
    const Vec3 galactocentric{
        distancePc * cosLatitude * std::cos(longitudeRad),
        distancePc * cosLatitude * std::sin(longitudeRad),
        distancePc * std::sin(latitudeRad)};
    return galactocentric;
}

template <typename UInt>
UInt readUnsignedBigEndian(std::ifstream& file) {
    std::array<unsigned char, sizeof(UInt)> bytes{};
    file.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!file) {
        throw std::runtime_error("Unexpected end of FITS data");
    }

    UInt value = 0;
    for (unsigned char byte : bytes) {
        value = static_cast<UInt>((value << 8) | static_cast<UInt>(byte));
    }
    return value;
}

double readScaledFitsValue(std::ifstream& file, const FitsCubeInfo& info) {
    double rawValue = 0.0;
    long rawInteger = 0;
    bool isIntegerType = true;

    switch (info.bitpix) {
        case 8: {
            rawInteger = static_cast<long>(readUnsignedBigEndian<std::uint8_t>(file));
            rawValue = static_cast<double>(rawInteger);
            break;
        }
        case 16: {
            rawInteger = static_cast<long>(static_cast<std::int16_t>(readUnsignedBigEndian<std::uint16_t>(file)));
            rawValue = static_cast<double>(rawInteger);
            break;
        }
        case 32: {
            rawInteger = static_cast<long>(static_cast<std::int32_t>(readUnsignedBigEndian<std::uint32_t>(file)));
            rawValue = static_cast<double>(rawInteger);
            break;
        }
        case -32: {
            const std::uint32_t bits = readUnsignedBigEndian<std::uint32_t>(file);
            float value = 0.0f;
            std::memcpy(&value, &bits, sizeof(float));
            rawValue = static_cast<double>(value);
            isIntegerType = false;
            break;
        }
        case -64: {
            const std::uint64_t bits = readUnsignedBigEndian<std::uint64_t>(file);
            double value = 0.0;
            std::memcpy(&value, &bits, sizeof(double));
            rawValue = value;
            isIntegerType = false;
            break;
        }
        default:
            throw std::runtime_error("Unsupported FITS BITPIX value");
    }

    if (isIntegerType && info.hasBlank && rawInteger == info.blankValue) {
        return std::numeric_limits<double>::quiet_NaN();
    }

    return rawValue * info.bscale + info.bzero;
}

FitsCubeInfo parseFitsCubeHeader(std::ifstream& file) {
    std::unordered_map<std::string, std::string> cards;
    bool foundEnd = false;

    while (!foundEnd) {
        std::array<char, kFitsBlockSize> block{};
        file.read(block.data(), static_cast<std::streamsize>(block.size()));
        if (!file) {
            throw std::runtime_error("Unexpected end of FITS header");
        }

        for (std::size_t offset = 0; offset < block.size(); offset += 80) {
            const std::string card(block.data() + offset, 80);
            const std::string key = trim(card.substr(0, 8));
            if (key.empty()) {
                continue;
            }
            if (key == "END") {
                foundEnd = true;
                break;
            }

            if (card.size() >= 10 && card[8] == '=' && card[9] == ' ') {
                std::string value = card.substr(10);
                const std::size_t comment = value.find('/');
                if (comment != std::string::npos) {
                    value = value.substr(0, comment);
                }
                cards[key] = trim(value);
            }
        }
    }

    FitsCubeInfo info;
    info.bitpix = std::stoi(cards.at("BITPIX"));
    info.naxis = std::stoi(cards.at("NAXIS"));
    if (info.naxis < 3) {
        throw std::runtime_error("FITS gas cube must have at least 3 axes");
    }

    for (int axis = 0; axis < 3; ++axis) {
        const int axisNumber = axis + 1;
        FitsAxisInfo axisInfo;
        axisInfo.length = std::stol(cards.at("NAXIS" + std::to_string(axisNumber)));

        const auto crvalIt = cards.find("CRVAL" + std::to_string(axisNumber));
        if (crvalIt != cards.end()) {
            axisInfo.crval = std::stod(crvalIt->second);
        }

        const auto crpixIt = cards.find("CRPIX" + std::to_string(axisNumber));
        if (crpixIt != cards.end()) {
            axisInfo.crpix = std::stod(crpixIt->second);
        }

        const auto cdeltIt = cards.find("CDELT" + std::to_string(axisNumber));
        if (cdeltIt != cards.end()) {
            axisInfo.cdelt = std::stod(cdeltIt->second);
        }

        const auto ctypeIt = cards.find("CTYPE" + std::to_string(axisNumber));
        if (ctypeIt != cards.end()) {
            axisInfo.ctype = stripQuotes(ctypeIt->second);
        }

        const auto cunitIt = cards.find("CUNIT" + std::to_string(axisNumber));
        if (cunitIt != cards.end()) {
            axisInfo.cunit = stripQuotes(cunitIt->second);
        }

        info.axes[axis] = axisInfo;
    }

    const auto bscaleIt = cards.find("BSCALE");
    if (bscaleIt != cards.end()) {
        info.bscale = std::stod(bscaleIt->second);
    }

    const auto bzeroIt = cards.find("BZERO");
    if (bzeroIt != cards.end()) {
        info.bzero = std::stod(bzeroIt->second);
    }

    const auto blankIt = cards.find("BLANK");
    if (blankIt != cards.end()) {
        info.hasBlank = true;
        info.blankValue = std::stol(blankIt->second);
    }

    info.dataOffset = file.tellg();
    return info;
}
} // namespace

bool GasCatalog::loadFits(const std::string& path, const GasCatalogConfig& config) {
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        std::cerr << "Failed to open FITS file: " << path << "\n";
        return false;
    }

    points_.clear();
    maxDensity_ = 0.0;

    try {
        const FitsCubeInfo info = parseFitsCubeHeader(file);
        file.seekg(info.dataOffset);

        // Detect which FITS axis index corresponds to velocity, longitude, latitude by CTYPE.
        // FITS axes are stored with axis 0 (NAXIS1) varying fastest in the data stream.
        int velAxisIdx = -1, lonAxisIdx = -1, latAxisIdx = -1;
        for (int i = 0; i < 3; ++i) {
            std::string ctype = info.axes[i].ctype;
            for (auto& c : ctype) {
                c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
            }
            if (ctype.find("VELO") != std::string::npos || ctype.find("VRAD") != std::string::npos || ctype.find("FELO") != std::string::npos) {
                velAxisIdx = i;
            } else if (ctype.find("LON") != std::string::npos) {
                lonAxisIdx = i;
            } else if (ctype.find("LAT") != std::string::npos) {
                latAxisIdx = i;
            }
        }
        // Fall back to old assumptions if CTYPE-based detection fails.
        if (velAxisIdx < 0) velAxisIdx = 2;
        if (lonAxisIdx < 0) lonAxisIdx = 0;
        if (latAxisIdx < 0) latAxisIdx = 1;

        std::cerr << "Gas axes: vel=" << velAxisIdx << " (" << info.axes[velAxisIdx].ctype
                  << "), lon=" << lonAxisIdx << " (" << info.axes[lonAxisIdx].ctype
                  << "), lat=" << latAxisIdx << " (" << info.axes[latAxisIdx].ctype << ")\n";

        // Assign strides by semantic axis -> FITS axis index.
        int strides[3] = {1, 1, 1};
        strides[velAxisIdx] = std::max(1, config.velocityStride);
        strides[lonAxisIdx] = std::max(1, config.longitudeStride);
        strides[latAxisIdx] = std::max(1, config.latitudeStride);

        std::mt19937_64 rng(42);
        std::uint64_t keptCandidates = 0;
        points_.reserve(config.maxPoints);

        // Always iterate axis2 (slowest) -> axis1 -> axis0 (fastest) to match FITS storage order.
        for (long idx2 = 0; idx2 < info.axes[2].length; ++idx2) {
            const bool keep2 = (idx2 % strides[2]) == 0;
            const double axisVal2 = linearAxisValue(info.axes[2], idx2);

            for (long idx1 = 0; idx1 < info.axes[1].length; ++idx1) {
                const bool keep1 = keep2 && ((idx1 % strides[1]) == 0);
                const double axisVal1 = linearAxisValue(info.axes[1], idx1);

                for (long idx0 = 0; idx0 < info.axes[0].length; ++idx0) {
                    const double intensity = readScaledFitsValue(file, info);
                    if (!keep1 || (idx0 % strides[0]) != 0) {
                        continue;
                    }
                    if (!std::isfinite(intensity) || intensity < config.intensityThreshold) {
                        continue;
                    }

                    const double axisValues[3] = {
                        linearAxisValue(info.axes[0], idx0),
                        axisVal1,
                        axisVal2
                    };

                    const double velocityRaw = axisValues[velAxisIdx];
                    const double velocityKmPerSec = velocityToKmPerSec(velocityRaw, info.axes[velAxisIdx].cunit);
                    const double distancePc = std::abs(velocityKmPerSec) * config.velocityDistanceScalePcPerKmS;

                    const Vec3 galPos = galactocentricFromSpherical(
                        axisValues[lonAxisIdx],
                        axisValues[latAxisIdx],
                        distancePc,
                        info.axes[lonAxisIdx].cunit,
                        info.axes[latAxisIdx].cunit);

                    GasPoint point;
                    point.pos = galPos;
                    point.density = static_cast<float>(intensity);
                    maxDensity_ = std::max(maxDensity_, intensity);

                    ++keptCandidates;
                    if (points_.size() < config.maxPoints) {
                        points_.push_back(point);
                        continue;
                    }

                    std::uniform_int_distribution<std::uint64_t> dist(0, keptCandidates - 1);
                    const std::uint64_t chosen = dist(rng);
                    if (chosen < points_.size()) {
                        points_[static_cast<std::size_t>(chosen)] = point;
                    }
                }
            }
        }

        std::cout << "Loaded " << points_.size() << " gas points from " << path
                  << " with threshold " << config.intensityThreshold << "\n";
        return true;
    } catch (const std::exception& ex) {
        std::cerr << "Failed to parse FITS gas cube: " << ex.what() << "\n";
        points_.clear();
        maxDensity_ = 0.0;
        return false;
    }
}