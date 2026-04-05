#pragma once
#include "game/chart/ChartTypes.h"
#include <string>

class ChartLoader {
public:
    // Auto-detect format from file extension / header
    static ChartData load(const std::string& path);

private:
    static ChartData loadUnified(const std::string& path);
    static ChartData loadBandori(const std::string& path);
    static ChartData loadPhigros(const std::string& path);
    static ChartData loadArcaea(const std::string& path);
    static ChartData loadCytus(const std::string& path);
    static ChartData loadLanota(const std::string& path);

    // Fills NoteEvent::beatPosition for every note using chart.timingPoints.
    // Must be called after both timingPoints and notes are populated.
    static void computeBeatPositions(ChartData& chart);
};
