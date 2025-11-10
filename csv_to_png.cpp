#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "stb_image_write.h"

struct RadarEchoRow {
    int status = 0;
    int scale = 0;
    int range = 0;
    int gain = 0;
    int angle = 0; // 0..8196
    std::vector<uint8_t> echo; // radial samples
};

namespace fs = std::filesystem;

static std::vector<std::string> SplitCsvLine(const std::string &line) {
    std::vector<std::string> cells;
    std::stringstream ss(line);
    std::string cell;
    while (std::getline(ss, cell, ',')) {
        // Trim whitespace
        size_t start = cell.find_first_not_of(" \t\r");
        size_t end = cell.find_last_not_of(" \t\r");
        if (start == std::string::npos) {
            cells.emplace_back();
        } else {
            cells.emplace_back(cell.substr(start, end - start + 1));
        }
    }
    return cells;
}

static std::vector<RadarEchoRow> LoadCsv(const std::string &path) {
    std::ifstream file(path);
    if (!file) {
        throw std::runtime_error("Failed to open CSV: " + path);
    }

    std::vector<RadarEchoRow> rows;
    std::string line;
    // Skip header if present
    if (std::getline(file, line)) {
        auto header = SplitCsvLine(line);
        if (header.empty() || header[0] != "Status") {
            // first line is data, keep it
            file.seekg(0);
        }
    }

    while (std::getline(file, line)) {
        if (line.empty()) continue;
        auto cols = SplitCsvLine(line);
        if (cols.size() < 6) continue; // need metadata + at least one echo

        RadarEchoRow row;
        row.status = std::stoi(cols[0]);
        row.scale = std::stoi(cols[1]);
        row.range = std::stoi(cols[2]);
        row.gain = std::stoi(cols[3]);
        row.angle = std::stoi(cols[4]);
        row.echo.reserve(cols.size() - 5);
        for (size_t i = 5; i < cols.size(); ++i) {
            if (cols[i].empty()) continue;
            int v = std::stoi(cols[i]);
            row.echo.push_back(static_cast<uint8_t>(std::clamp(v, 0, 255)));
        }
        if (!row.echo.empty()) {
            rows.push_back(std::move(row));
        }
    }
    return rows;
}

static void WriteEchoImage(const std::vector<RadarEchoRow> &rows, const std::string &filename) {
    if (rows.empty()) throw std::runtime_error("No radar sweeps in CSV");

    int maxRadius = 0;
    for (const auto &r : rows) {
        if (static_cast<int>(r.echo.size()) > maxRadius) maxRadius = static_cast<int>(r.echo.size());
    }
    if (maxRadius <= 0) throw std::runtime_error("Radar sweeps have zero length");

    const int w = maxRadius * 2;
    const int h = maxRadius * 2;
    const int cx = maxRadius;
    const int cy = maxRadius;

    std::vector<uint8_t> pixels(w * h * 4, 0);
    for (size_t i = 0; i < pixels.size(); i += 4) pixels[i + 3] = 255; // opaque

    constexpr double TWO_PI = 2.0 * M_PI;
    for (const auto &entry : rows) {
        const double angleRad = (entry.angle / 8196.0) * TWO_PI;
        const double cosA = std::cos(angleRad);
        const double sinA = std::sin(angleRad);
        for (size_t r = 0; r < entry.echo.size(); ++r) {
            int x = static_cast<int>(std::lround(cx + static_cast<double>(r) * cosA));
            int y = static_cast<int>(std::lround(cy + static_cast<double>(r) * sinA));
            if (x < 0 || x >= w || y < 0 || y >= h) continue;
            size_t idx = static_cast<size_t>(y) * w * 4 + static_cast<size_t>(x) * 4;
            uint8_t val = entry.echo[r];
            pixels[idx + 0] = val;
            pixels[idx + 1] = val;
            pixels[idx + 2] = val;
        }
    }

    fs::path outPath(filename);
    if (!outPath.parent_path().empty()) {
        fs::create_directories(outPath.parent_path());
    }

    if (!stbi_write_png(filename.c_str(), w, h, 4, pixels.data(), w * 4)) {
        throw std::runtime_error("Failed to write PNG: " + filename);
    }
}

int main(int argc, char **argv) {
    if (argc < 3) {
        std::cerr << "Usage: " << argv[0] << " <input.csv> <output.png>" << std::endl;
        return 1;
    }

    try {
        auto rows = LoadCsv(argv[1]);
        WriteEchoImage(rows, argv[2]);
    } catch (const std::exception &ex) {
        std::cerr << "Error: " << ex.what() << std::endl;
        return 1;
    }
    return 0;
}
