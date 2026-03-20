#include <filesystem>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <utility>
#include <vector>
#include <chrono>
#include <algorithm>

#ifdef __unix__
#include <sys/ioctl.h>
#include <unistd.h>
#endif

#include "catalog.hpp"
#include "gas_catalog.hpp"
#include "json_writer.hpp"
#include "simulation.hpp"

namespace {
constexpr Vec3 kSunGalactocentricPosPc{-8200.0, 0.0, 20.8};

std::string argumentOrDefault(int argc, char** argv, int index, const std::string& fallback) {
    return (argc > index && argv[index] && std::string(argv[index]).size() > 0) ? argv[index] : fallback;
}

int intArgOrDefault(int argc, char** argv, int index, int fallback) {
    try {
        return (argc > index) ? std::stoi(argv[index]) : fallback;
    } catch (...) {
        return fallback;
    }
}

double doubleArgOrDefault(int argc, char** argv, int index, double fallback) {
    try {
        return (argc > index) ? std::stod(argv[index]) : fallback;
    } catch (...) {
        return fallback;
    }
}

std::string formatDurationSeconds(double seconds) {
    if (seconds < 0.0) {
        seconds = 0.0;
    }

    const int total = static_cast<int>(seconds + 0.5);
    const int hrs = total / 3600;
    const int mins = (total % 3600) / 60;
    const int secs = total % 60;

    std::ostringstream out;
    out << std::setfill('0');
    if (hrs > 0) {
        out << hrs << ':' << std::setw(2) << mins << ':' << std::setw(2) << secs;
    } else {
        out << mins << ':' << std::setw(2) << secs;
    }
    return out.str();
}

int terminalWidth() {
#ifdef __unix__
    winsize size{};
    if (ioctl(STDERR_FILENO, TIOCGWINSZ, &size) == 0 && size.ws_col > 0) {
        return static_cast<int>(size.ws_col);
    }
#endif
    return 80;
}

bool stderrIsInteractive() {
#ifdef __unix__
    return isatty(STDERR_FILENO) == 1;
#else
    return false;
#endif
}

std::string makeProgressLine(int completedFrames,
                             int totalFrames,
                             double elapsedSec,
                             double etaSec,
                             int width) {
    const double ratio = (totalFrames > 0)
                             ? static_cast<double>(completedFrames) / static_cast<double>(totalFrames)
                             : 1.0;
    const int percent = static_cast<int>(ratio * 100.0 + 0.5);

    std::ostringstream compact;
    compact << percent << "% " << completedFrames << '/' << totalFrames
            << " el " << formatDurationSeconds(elapsedSec)
            << " eta " << formatDurationSeconds(etaSec);
    const std::string compactText = compact.str();
    if (width <= static_cast<int>(compactText.size()) + 1) {
        return compactText;
    }

    const std::string prefix = "Frames ";
    const std::string suffix = " " + compactText;
    int barWidth = width - static_cast<int>(prefix.size() + suffix.size()) - 2;
    barWidth = std::max(8, std::min(36, barWidth));

    std::ostringstream full;
    full << prefix << '[';
    const int filled = static_cast<int>(ratio * barWidth);
    for (int i = 0; i < barWidth; ++i) {
        full << (i < filled ? '#' : '-');
    }
    full << ']' << suffix;

    std::string line = full.str();
    if (static_cast<int>(line.size()) > width) {
        line.resize(static_cast<std::size_t>(width));
    }
    return line;
}

SimulationResult transformToGalactocentric(const SimulationResult& input) {
    SimulationResult transformed = input;
    for (auto& frame : transformed.frames) {
        for (auto& point : frame) {
            point += kSunGalactocentricPosPc;
        }
    }
    return transformed;
}
} // namespace

int main(int argc, char** argv) {
    // Set this to true to render a single static frame without simulation animation.
    const bool renderSingleStationaryFrame = true;
    constexpr double kGaiaNewSampleFraction = 0.05;

    const std::string inputCsv = argumentOrDefault(argc, argv, 1, "gaia.csv");
    const std::string outputJson = argumentOrDefault(argc, argv, 2, "docs/scene.json");
    const std::string gasFitsPath = argumentOrDefault(argc, argv, 6, "COGAL_all_raw.fits");
    const int frameCount = intArgOrDefault(argc, argv, 3, 500);
    const int substepsPerFrame = intArgOrDefault(argc, argv, 4, 32);
    const double yearsPerFrame = doubleArgOrDefault(argc, argv, 5, 1000.0);
    const std::filesystem::path primaryCsvPath(inputCsv);
    const std::filesystem::path gaiaNewPath = primaryCsvPath.parent_path() / "gaia_new.csv";

    Catalog catalog;
    if (!catalog.loadCsv(inputCsv, 1.0, false)) {
        std::cerr << "Failed to load CSV: " << inputCsv << "\n";
        return 1;
    }

    if (std::filesystem::exists(gaiaNewPath)) {
        if (!catalog.loadCsv(gaiaNewPath.string(), kGaiaNewSampleFraction, true)) {
            std::cerr << "Failed to append sampled CSV: " << gaiaNewPath.string() << "\n";
            return 1;
        }
    } else {
        std::cerr << "Sample source not found, skipping append: " << gaiaNewPath.string() << "\n";
    }

    std::vector<Body> bodies = catalog.bodies();

    Body sun;
    sun.id = 0;
    sun.name = "Sun";
    sun.pos = {0.0, 0.0, 0.0};
    sun.vel = {0.0, 0.0, 0.0};
    sun.mag = -26.74f;
    sun.colorIndex = 0.656f;
    sun.isSun = true;
    sun.hasMotion = false;
    bodies.push_back(sun);

    Simulation simulation;
    simulation.setBodies(std::move(bodies));

    SimulationConfig config;
    config.frameCount = renderSingleStationaryFrame ? 1 : std::max(2, frameCount);
    config.substepsPerFrame = substepsPerFrame;
    config.yearsPerFrame = yearsPerFrame;

    const auto startedAt = std::chrono::steady_clock::now();
    const bool interactiveStderr = stderrIsInteractive();
    auto printProgress = [&](int completedFrames, int totalFrames) {
        const auto now = std::chrono::steady_clock::now();
        const double elapsedSec =
            std::chrono::duration_cast<std::chrono::duration<double>>(now - startedAt).count();
        const double etaSec = (completedFrames > 0)
                                  ? elapsedSec * static_cast<double>(totalFrames - completedFrames) /
                                        static_cast<double>(completedFrames)
                                  : 0.0;

        const std::string line = makeProgressLine(
            completedFrames,
            totalFrames,
            elapsedSec,
            etaSec,
            terminalWidth());

        if (interactiveStderr) {
            std::cerr << '\r' << line;
            const int padding = std::max(0, terminalWidth() - static_cast<int>(line.size()));
            if (padding > 0) {
                std::cerr << std::string(static_cast<std::size_t>(padding), ' ');
            }
            std::cerr << std::flush;
            return;
        }

        if (completedFrames == totalFrames || completedFrames == 1 || completedFrames % 100 == 0) {
            std::cerr << line << '\n';
        }
    };

    SimulationResult result = simulation.precompute(config, printProgress);
    std::cerr << "\n";

    GasCatalog gasCatalog;
    GasCatalogConfig gasConfig;
    gasConfig.intensityThreshold = 0.5;
    // The COGAL CO survey covers ±320 km/s; the Milky Way CO disk extends ~13 kpc radius.
    // Scale: 13000 pc / 320 km/s ≈ 40 pc/(km/s).
    gasConfig.velocityDistanceScalePcPerKmS = 40.0;
    gasConfig.maxPoints = 200000;
    gasConfig.longitudeStride = 2;
    gasConfig.latitudeStride = 2;
    gasConfig.velocityStride = 2;
    gasCatalog.loadFits(gasFitsPath, gasConfig);

    const SimulationResult galactocentricResult = transformToGalactocentric(result);

    std::filesystem::path outputPath(outputJson);
    if (outputPath.has_parent_path()) {
        std::filesystem::create_directories(outputPath.parent_path());
    }

    if (!JsonWriter::writeScene(
            outputJson,
            galactocentricResult,
            gasCatalog.points(),
            gasCatalog.maxDensity())) {
        std::cerr << "Failed to write " << outputJson << "\n";
        return 1;
    }

    std::cout << "Wrote " << outputJson << "\n";
    std::cout << "Frames: " << config.frameCount << ", substeps/frame: " << substepsPerFrame
              << ", years/frame: " << yearsPerFrame
              << (renderSingleStationaryFrame ? " (static frame mode)" : " (simulation mode)")
              << "\n";
    return 0;
}
