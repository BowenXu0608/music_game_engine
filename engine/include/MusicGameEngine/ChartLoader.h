#pragma once
#include "ChartTypes.h"
#include <string>

class ChartLoader {
public:
    // Auto-detect format from file extension / header
    static ChartData load(const std::string& path);

private:
    static ChartData loadDrop2D(const std::string& path);
    static ChartData loadPhigros(const std::string& path);
    static ChartData loadDrop3D(const std::string& path);
    static ChartData loadScanLine(const std::string& path);
    static ChartData loadCircle(const std::string& path);
};
