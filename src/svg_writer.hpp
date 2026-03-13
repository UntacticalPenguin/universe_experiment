#pragma once

#include <string>
#include <vector>
#include "body.hpp"
#include "projection.hpp"

// Writes a simple SVG image of the stars.
class SvgWriter {
public:
    static bool write(
        const std::string& path,
        const std::vector<Body>& bodies,
        const Projection& projection,
        int width,
        int height
    );
};