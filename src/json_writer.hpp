#pragma once

#include <string>
#include <vector>
#include "body.hpp"

class JsonWriter {
public:
    static bool write(const std::string& path, const std::vector<Body>& bodies);
};