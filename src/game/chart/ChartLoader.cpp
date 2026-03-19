#include "ChartLoader.h"
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <algorithm>

ChartData ChartLoader::load(const std::string& path) {
    // Dispatch by extension
    auto ext = path.substr(path.rfind('.') + 1);
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

    if (ext == "json")  return loadBandori(path);
    if (ext == "pec" || ext == "pgr") return loadPhigros(path);
    if (ext == "aff")   return loadArcaea(path);
    if (ext == "xml")   return loadCytus(path);
    if (ext == "lan")   return loadLanota(path);

    throw std::runtime_error("Unknown chart format: " + ext);
}

// Stub implementations — replace with real parsers per format
ChartData ChartLoader::loadBandori(const std::string& path) {
    ChartData chart;
    chart.title = "Bandori Chart";
    // TODO: parse BanG Dream JSON format
    return chart;
}

ChartData ChartLoader::loadPhigros(const std::string& path) {
    ChartData chart;
    chart.title = "Phigros Chart";
    // TODO: parse Phigros PEC/PGR format
    // Each judgment line has keyframe events and attached notes
    return chart;
}

ChartData ChartLoader::loadArcaea(const std::string& path) {
    ChartData chart;
    chart.title = "Arcaea Chart";

    std::ifstream f(path);
    if (!f.is_open()) throw std::runtime_error("Cannot open: " + path);

    // Minimal AFF parser
    std::string line;
    uint32_t noteID = 0;
    while (std::getline(f, line)) {
        if (line.rfind("arc(", 0) == 0) {
            // arc(startTime,endTime,startX,endX,easeType,startY,endY,color,hitSound,isVoid)
            NoteEvent ev{};
            ev.type = NoteType::Arc;
            ev.id   = noteID++;
            ArcData arc{};
            // Simplified parse — real parser would be more robust
            sscanf(line.c_str(), "arc(%lf,%*f,%f,%f,%*[^,],%f,%f,%d,%*[^,],%*[^)])",
                   &ev.time, &arc.startPos.x, &arc.endPos.x,
                   &arc.startPos.y, &arc.endPos.y, &arc.color);
            ev.data = arc;
            chart.notes.push_back(ev);
        } else if (line.rfind("(", 0) == 0) {
            // Tap note: (time,lane)
            NoteEvent ev{};
            ev.type = NoteType::Tap;
            ev.id   = noteID++;
            TapData tap{};
            int lane = 0;
            sscanf(line.c_str(), "(%lf,%d)", &ev.time, &lane);
            ev.time /= 1000.0;  // AFF uses milliseconds
            tap.laneX = (lane - 1) / 3.f - 0.5f;  // normalize to [-0.5, 0.5]
            ev.data = tap;
            chart.notes.push_back(ev);
        }
    }
    return chart;
}

ChartData ChartLoader::loadCytus(const std::string& path) {
    ChartData chart;
    chart.title = "Cytus Chart";
    // TODO: parse Cytus XML format
    return chart;
}

ChartData ChartLoader::loadLanota(const std::string& path) {
    ChartData chart;
    chart.title = "Lanota Chart";
    // TODO: parse Lanota format
    return chart;
}
