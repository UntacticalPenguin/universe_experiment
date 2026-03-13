#include "json_writer.hpp"

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
}

bool JsonWriter::writeScene(const std::string& path, const SimulationResult& result) {
    std::ofstream out(path);
    if (!out.is_open()) {
        return false;
    }

    out << std::setprecision(8);
    out << "{\n";
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
    return true;
}