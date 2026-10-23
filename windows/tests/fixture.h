#pragma once
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>

// Reads a file from tests/fixtures (path injected by CMake as IXPAT_TEST_FIXTURES).
inline std::string readFixture(const std::string& name) {
    std::ifstream in(std::string(IXPAT_TEST_FIXTURES) + "/" + name, std::ios::binary);
    if (!in) throw std::runtime_error("fixture not found: " + name);
    std::ostringstream out;
    out << in.rdbuf();
    return out.str();
}
