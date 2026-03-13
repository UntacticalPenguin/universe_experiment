#include "json_writer.hpp"
#include <fstream>

bool JsonWriter::write(const std::string& path, const std::vector<Body>& bodies) {
    std::ofstream out(path);
    if (!out.is_open()) {
        return false;
    }

    out << "[\n";

    for (std::size_t i = 0; i < bodies.size(); ++i) {
        const Body& b = bodies[i];

        out << "  {\n";
        out << "    \"id\": " << b.id << ",\n";
        out << "    \"name\": \"" << b.name << "\",\n";
        out << "    \"px\": " << b.pos.x << ",\n";
        out << "    \"py\": " << b.pos.y << ",\n";
        out << "    \"pz\": " << b.pos.z << ",\n";
        out << "    \"vx\": " << b.vel.x << ",\n";
        out << "    \"vy\": " << b.vel.y << ",\n";
        out << "    \"vz\": " << b.vel.z << ",\n";
        out << "    \"mag\": " << b.mag << "\n";
        out << "  }";

        if (i + 1 < bodies.size()) {
            out << ",";
        }
        out << "\n";
    }

    out << "]\n";
    return true;
}