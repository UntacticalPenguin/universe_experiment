#include "svg_writer.hpp"

#include <fstream>

// Smaller magnitude = brighter star.
// We convert that into a circle radius.
static double radiusFromMagnitude(float mag) {
    double radius = 4.5 - 0.45 * static_cast<double>(mag);

    if (radius < 0.5) {
        radius = 0.5;
    }

    if (radius > 6.0) {
        radius = 6.0;
    }

    return radius;
}

bool SvgWriter::write(
    const std::string& path,
    const std::vector<Body>& bodies,
    const Projection& projection,
    int width,
    int height
) {
    std::ofstream out(path);

    if (!out.is_open()) {
        return false;
    }

    out << "<svg xmlns=\"http://www.w3.org/2000/svg\" "
        << "width=\"" << width << "\" "
        << "height=\"" << height << "\" "
        << "viewBox=\"0 0 " << width << " " << height << "\">\n";

    out << "<rect width=\"100%\" height=\"100%\" fill=\"black\" />\n";

    const double centerX = width * 0.5;
    const double centerY = height * 0.5;

    for (const auto& b : bodies) {
        ScreenPoint p = projection.project(b.pos);

        const double screenX = centerX + p.x;
        const double screenY = centerY + p.y;

        if (screenX < 0 || screenX > width || screenY < 0 || screenY > height) {
            continue;
        }

        const double radius = radiusFromMagnitude(b.mag);

        out << "<circle "
            << "cx=\"" << screenX << "\" "
            << "cy=\"" << screenY << "\" "
            << "r=\"" << radius << "\" "
            << "fill=\"white\" "
            << "fill-opacity=\"0.85\" "
            << "/>\n";
    }

    out << "</svg>\n";

    return true;
}