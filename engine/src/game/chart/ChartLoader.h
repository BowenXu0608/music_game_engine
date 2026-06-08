#pragma once
#include "game/chart/ChartTypes.h"
#include <string>

class ChartLoader {
public:
    // Auto-detect format from file extension / header
    static ChartData load(const std::string& path);

private:
    static ChartData loadUnified(const std::string& path);
    static ChartData loadDrop2D(const std::string& path);
    static ChartData loadPhigros(const std::string& path);
    static ChartData loadDrop3D(const std::string& path);
    static ChartData loadScanLine(const std::string& path);
    static ChartData loadCircle(const std::string& path);

    // Fills NoteEvent::beatPosition for every note using chart.timingPoints.
    // Must be called after both timingPoints and notes are populated.
    static void computeBeatPositions(ChartData& chart);
};
