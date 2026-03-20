#include "json_writer.hpp"

#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string>

namespace {
std::string escapeJson(const std::string& input) {
    std::ostringstream out;
    for (char c : input) {
        switch (c) {
            case '\\': out << "\\\\"; break;
            case '"': out << "\\\""; break;
            case '\n': out << "\\n"; break;
            case '\r': out << "\\r"; break;
            case '\t': out << "\\t"; break;
            default: out << c; break;
        }
    }
    return out.str();
}

bool writeLabelDebugFile(const std::string& scenePath, const SimulationResult& result) {
    std::filesystem::path outPath(scenePath);
    const std::filesystem::path debugPath = outPath.parent_path() / "label_debug.json";

    std::ofstream out(debugPath.string());
    if (!out.is_open()) {
        return false;
    }

    std::size_t labeledCount = 0;
    for (const auto& body : result.bodies) {
        if (!body.name.empty()) {
            ++labeledCount;
        }
    }

    out << "{\n";
    out << "  \"totalStars\": " << result.bodies.size() << ",\n";
    out << "  \"labeledStars\": " << labeledCount << ",\n";
    out << "  \"entries\": [\n";

    for (std::size_t i = 0; i < result.bodies.size(); ++i) {
        const auto& body = result.bodies[i];
        out << "    {\"index\": " << i
            << ", \"id\": " << body.id
            << ", \"sourceId\": \"" << escapeJson(body.sourceId) << "\""
            << ", \"label\": \"" << escapeJson(body.name) << "\""
            << ", \"reason\": \"" << escapeJson(body.labelDebugReason) << "\""
            << ", \"hasLabel\": " << (body.name.empty() ? "false" : "true")
            << "}";
        if (i + 1 < result.bodies.size()) {
            out << ",";
        }
        out << "\n";
    }

    out << "  ]\n";
    out << "}\n";
    return true;
}
}

bool JsonWriter::writeScene(const std::string& path,
                            const SimulationResult& result,
                            const std::vector<GasPoint>& gasPoints,
                            double gasDensityMax) {
    std::ofstream out(path);
    if (!out.is_open()) {
        return false;
    }

    out << std::setprecision(8);
    out << "{\n";
    out << "  \"coordinateFrame\": \"galactocentric\",\n";
    out << "  \"frameCount\": " << result.frames.size() << ",\n";
    out << "  \"yearsPerFrame\": " << result.yearsPerFrame << ",\n";
    out << "  \"stars\": [\n";
    for (std::size_t i = 0; i < result.bodies.size(); ++i) {
        const auto& b = result.bodies[i];
        out << "    {\"id\": " << b.id
            << ", \"name\": \"" << escapeJson(b.name) << "\""
            << ", \"mag\": " << b.mag
            << ", \"ci\": " << b.colorIndex
            << ", \"isSun\": " << (b.isSun ? "true" : "false")
            << "}";
        if (i + 1 < result.bodies.size()) out << ",";
        out << "\n";
    }
    out << "  ],\n";
    out << "  \"gasPointCount\": " << gasPoints.size() << ",\n";
    out << "  \"gasDensityMax\": " << gasDensityMax << ",\n";
    out << "  \"gasPoints\": [";
    for (std::size_t i = 0; i < gasPoints.size(); ++i) {
        const auto& gasPoint = gasPoints[i];
        out << gasPoint.pos.x << "," << gasPoint.pos.y << "," << gasPoint.pos.z << ","
            << gasPoint.density;
        if (i + 1 < gasPoints.size()) {
            out << ",";
        }
    }
    out << "],\n";
    out << "  \"frames\": [\n";
    for (std::size_t fi = 0; fi < result.frames.size(); ++fi) {
        out << "    [";
        const auto& frame = result.frames[fi];
        for (std::size_t bi = 0; bi < frame.size(); ++bi) {
            const auto& p = frame[bi];
            out << p.x << "," << p.y << "," << p.z;
            if (bi + 1 < frame.size()) out << ",";
        }
        out << "]";
        if (fi + 1 < result.frames.size()) out << ",";
        out << "\n";
    }
    out << "  ]\n";
    out << "}\n";

    // Extra debug artifact to inspect name-resolution coverage per Gaia source id.
    writeLabelDebugFile(path, result);
    return true;
}