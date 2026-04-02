#pragma once
#include "ChartTypes.h"
#include <string>

class ChartLoader {
public:
    // Auto-detect format from file extension / header
    static ChartData load(const std::string& path);

private:
    static ChartData loadBandori(const std::string& path);
    static ChartData loadPhigros(const std::string& path);
    static ChartData loadArcaea(const std::string& path);
    static ChartData loadCytus(const std::string& path);
    static ChartData loadLanota(const std::string& path);
};
