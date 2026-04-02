#include "ChartLoader.h"
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <algorithm>

ChartData ChartLoader::load(const std::string& path) {
    std::ifstream f(path);
    if (!f.is_open()) throw std::runtime_error("Cannot open: " + path);

    // Check if it's unified format by looking for "version" field
    std::string firstLine;
    std::getline(f, firstLine);
    f.seekg(0);
    f.close();

    if (firstLine.find("\"version\"") != std::string::npos) {
        return loadUnified(path);
    }

    // Dispatch by extension for legacy formats
    auto ext = path.substr(path.rfind('.') + 1);
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

    if (ext == "json")  return loadBandori(path);
    if (ext == "pec" || ext == "pgr") return loadPhigros(path);
    if (ext == "aff")   return loadArcaea(path);
    if (ext == "xml")   return loadCytus(path);
    if (ext == "lan")   return loadLanota(path);

    throw std::runtime_error("Unknown chart format: " + ext);
}

ChartData ChartLoader::loadUnified(const std::string& path) {
    ChartData chart;
    std::ifstream f(path);
    if (!f.is_open()) throw std::runtime_error("Cannot open: " + path);

    std::string content((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());

    auto findValue = [&](const std::string& key) -> std::string {
        auto pos = content.find("\"" + key + "\"");
        if (pos == std::string::npos) return "";
        pos = content.find(":", pos) + 1;
        while (pos < content.size() && (content[pos] == ' ' || content[pos] == '\t')) pos++;
        if (content[pos] == '"') {
            pos++;
            auto end = content.find('"', pos);
            return content.substr(pos, end - pos);
        }
        auto end = pos;
        while (end < content.size() && content[end] != ',' && content[end] != '}' && content[end] != '\n') end++;
        return content.substr(pos, end - pos);
    };

    chart.title = findValue("title");
    chart.artist = findValue("artist");
    chart.offset = std::stof(findValue("offset").empty() ? "0" : findValue("offset"));

    // Parse notes array
    auto notesPos = content.find("\"notes\"");
    if (notesPos != std::string::npos) {
        auto arrayStart = content.find('[', notesPos);
        auto arrayEnd = content.find(']', arrayStart);
        std::string notesStr = content.substr(arrayStart + 1, arrayEnd - arrayStart - 1);

        uint32_t noteID = 0;
        size_t pos = 0;
        while ((pos = notesStr.find('{', pos)) != std::string::npos) {
            auto objEnd = notesStr.find('}', pos);
            std::string noteObj = notesStr.substr(pos, objEnd - pos + 1);

            auto getVal = [&](const std::string& k) {
                auto p = noteObj.find("\"" + k + "\"");
                if (p == std::string::npos) return std::string("");
                p = noteObj.find(":", p) + 1;
                while (p < noteObj.size() && (noteObj[p] == ' ' || noteObj[p] == '\t')) p++;
                if (noteObj[p] == '"') {
                    p++;
                    auto e = noteObj.find('"', p);
                    return noteObj.substr(p, e - p);
                }
                auto e = p;
                while (e < noteObj.size() && noteObj[e] != ',' && noteObj[e] != '}') e++;
                return noteObj.substr(p, e - p);
            };

            NoteEvent ev{};
            ev.id = noteID++;
            ev.time = std::stod(getVal("time"));
            std::string type = getVal("type");

            if (type == "tap") {
                ev.type = NoteType::Tap;
                ev.data = TapData{std::stof(getVal("lane"))};
            } else if (type == "hold") {
                ev.type = NoteType::Hold;
                ev.data = HoldData{std::stof(getVal("lane")), std::stof(getVal("duration"))};
            } else if (type == "flick") {
                ev.type = NoteType::Flick;
                ev.data = FlickData{std::stof(getVal("lane")), std::stoi(getVal("direction").empty() ? "1" : getVal("direction"))};
            } else if (type == "arc") {
                ev.type = NoteType::Arc;
                ArcData arc;
                arc.startPos.x = std::stof(getVal("startX"));
                arc.startPos.y = std::stof(getVal("startY"));
                arc.endPos.x = std::stof(getVal("endX"));
                arc.endPos.y = std::stof(getVal("endY"));
                arc.duration = std::stof(getVal("duration"));
                arc.color = std::stoi(getVal("color").empty() ? "0" : getVal("color"));
                ev.data = arc;
            }

            chart.notes.push_back(ev);
            pos = objEnd + 1;
        }
    }

    return chart;
}

ChartData ChartLoader::loadBandori(const std::string& path) {
    ChartData chart;
    std::ifstream f(path);
    if (!f.is_open()) throw std::runtime_error("Cannot open: " + path);

    std::string content((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());

    // Minimal JSON parser for Bandori format
    auto findValue = [&](const std::string& key) -> std::string {
        auto pos = content.find("\"" + key + "\"");
        if (pos == std::string::npos) return "";
        pos = content.find(":", pos) + 1;
        while (pos < content.size() && (content[pos] == ' ' || content[pos] == '\t')) pos++;
        if (content[pos] == '"') {
            pos++;
            auto end = content.find('"', pos);
            return content.substr(pos, end - pos);
        }
        auto end = pos;
        while (end < content.size() && content[end] != ',' && content[end] != '}' && content[end] != '\n') end++;
        return content.substr(pos, end - pos);
    };

    chart.title = findValue("title");
    chart.artist = findValue("artist");
    chart.offset = std::stof(findValue("offset").empty() ? "0" : findValue("offset"));

    // Parse notes array
    auto notesPos = content.find("\"notes\"");
    if (notesPos != std::string::npos) {
        auto arrayStart = content.find('[', notesPos);
        auto arrayEnd = content.find(']', arrayStart);
        std::string notesStr = content.substr(arrayStart + 1, arrayEnd - arrayStart - 1);

        uint32_t noteID = 0;
        size_t pos = 0;
        while ((pos = notesStr.find('{', pos)) != std::string::npos) {
            auto objEnd = notesStr.find('}', pos);
            std::string noteObj = notesStr.substr(pos, objEnd - pos + 1);

            auto getVal = [&](const std::string& k) {
                auto p = noteObj.find("\"" + k + "\"");
                if (p == std::string::npos) return std::string("");
                p = noteObj.find(":", p) + 1;
                while (p < noteObj.size() && (noteObj[p] == ' ' || noteObj[p] == '\t')) p++;
                if (noteObj[p] == '"') {
                    p++;
                    auto e = noteObj.find('"', p);
                    return noteObj.substr(p, e - p);
                }
                auto e = p;
                while (e < noteObj.size() && noteObj[e] != ',' && noteObj[e] != '}') e++;
                return noteObj.substr(p, e - p);
            };

            NoteEvent ev{};
            ev.id = noteID++;
            ev.time = std::stod(getVal("time"));
            std::string laneStr = getVal("lane");
            int lane = laneStr.empty() ? 0 : std::stoi(laneStr);
            std::string type = getVal("type");

            if (type == "Single") {
                ev.type = NoteType::Tap;
                ev.data = TapData{static_cast<float>(lane)};
            } else if (type == "Long") {
                ev.type = NoteType::Hold;
                float duration = std::stof(getVal("endTime")) - ev.time;
                ev.data = HoldData{static_cast<float>(lane), duration};
            } else if (type == "Flick") {
                ev.type = NoteType::Flick;
                ev.data = FlickData{static_cast<float>(lane), 1};
            }

            chart.notes.push_back(ev);
            pos = objEnd + 1;
        }
    }

    return chart;
}

ChartData ChartLoader::loadPhigros(const std::string& path) {
    ChartData chart;
    chart.title = "Phigros Chart";

    std::ifstream f(path);
    if (!f.is_open()) throw std::runtime_error("Cannot open: " + path);

    std::string content((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());

    // Parse judgment lines
    size_t linePos = 0;
    while ((linePos = content.find("\"judgeLineList\"", linePos)) != std::string::npos) {
        auto arrayStart = content.find('[', linePos);
        auto arrayEnd = content.find(']', arrayStart);

        size_t pos = arrayStart;
        while ((pos = content.find("{", pos)) != std::string::npos && pos < arrayEnd) {
            JudgmentLineEvent lineEvent{};
            lineEvent.time = 0.0;
            lineEvent.position = {0.5f, 0.5f};
            lineEvent.rotation = 0.0f;
            lineEvent.speed = 1.0f;

            // Parse notes on this line
            auto notesStart = content.find("\"notesAbove\"", pos);
            if (notesStart != std::string::npos && notesStart < arrayEnd) {
                auto notesArrayStart = content.find('[', notesStart);
                auto notesArrayEnd = content.find(']', notesArrayStart);

                size_t notePos = notesArrayStart;
                uint32_t noteID = 0;
                while ((notePos = content.find("{", notePos)) != std::string::npos && notePos < notesArrayEnd) {
                    NoteEvent ev{};
                    ev.id = noteID++;
                    ev.type = NoteType::Tap;

                    PhigrosNoteData pData{};
                    pData.posOnLine = 0.0f;
                    pData.subType = NoteType::Tap;
                    ev.data = pData;

                    lineEvent.attachedNotes.push_back(ev);
                    notePos++;
                }
            }

            chart.judgmentLines.push_back(lineEvent);
            pos++;
        }
        break;
    }

    return chart;
}

ChartData ChartLoader::loadArcaea(const std::string& path) {
    ChartData chart;
    chart.title = "Arcaea Chart";

    std::ifstream f(path);
    if (!f.is_open()) throw std::runtime_error("Cannot open: " + path);

    std::string line;
    uint32_t noteID = 0;
    while (std::getline(f, line)) {
        if (line.rfind("arc(", 0) == 0) {
            NoteEvent ev{};
            ev.type = NoteType::Arc;
            ev.id = noteID++;
            ArcData arc{};
            double endTime;
            sscanf(line.c_str(), "arc(%lf,%lf,%f,%f,%*[^,],%f,%f,%d",
                   &ev.time, &endTime, &arc.startPos.x, &arc.endPos.x,
                   &arc.startPos.y, &arc.endPos.y, &arc.color);
            ev.time /= 1000.0;
            arc.duration = (endTime / 1000.0) - ev.time;
            ev.data = arc;
            chart.notes.push_back(ev);
        } else if (line.rfind("hold(", 0) == 0) {
            NoteEvent ev{};
            ev.type = NoteType::Hold;
            ev.id = noteID++;
            double endTime;
            int lane;
            sscanf(line.c_str(), "hold(%lf,%lf,%d)", &ev.time, &endTime, &lane);
            ev.time /= 1000.0;
            HoldData hold;
            hold.laneX = static_cast<float>(lane);
            hold.duration = (endTime / 1000.0) - ev.time;
            ev.data = hold;
            chart.notes.push_back(ev);
        } else if (line.rfind("(", 0) == 0) {
            NoteEvent ev{};
            ev.type = NoteType::Tap;
            ev.id = noteID++;
            int lane;
            sscanf(line.c_str(), "(%lf,%d)", &ev.time, &lane);
            ev.time /= 1000.0;
            TapData tap{};
            tap.laneX = static_cast<float>(lane);
            ev.data = tap;
            chart.notes.push_back(ev);
        }
    }
    return chart;
}

ChartData ChartLoader::loadCytus(const std::string& path) {
    ChartData chart;
    chart.title = "Cytus Chart";

    std::ifstream f(path);
    if (!f.is_open()) throw std::runtime_error("Cannot open: " + path);

    std::string content((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());

    uint32_t noteID = 0;
    size_t pos = 0;
    while ((pos = content.find("<note", pos)) != std::string::npos) {
        auto end = content.find("/>", pos);
        std::string noteTag = content.substr(pos, end - pos);

        NoteEvent ev{};
        ev.id = noteID++;

        auto getAttr = [&](const std::string& attr) {
            auto p = noteTag.find(attr + "=\"");
            if (p == std::string::npos) return 0.0f;
            p += attr.length() + 2;
            return std::stof(noteTag.substr(p, noteTag.find('"', p) - p));
        };

        ev.time = getAttr("time");
        float x = getAttr("x");
        float y = getAttr("y");
        int type = static_cast<int>(getAttr("type"));

        if (type == 0) {
            ev.type = NoteType::Tap;
            ev.data = TapData{x};
        } else if (type == 1) {
            ev.type = NoteType::Hold;
            ev.data = HoldData{x, getAttr("duration")};
        }

        chart.notes.push_back(ev);
        pos = end;
    }

    return chart;
}

ChartData ChartLoader::loadLanota(const std::string& path) {
    ChartData chart;
    chart.title = "Lanota Chart";

    std::ifstream f(path);
    if (!f.is_open()) throw std::runtime_error("Cannot open: " + path);

    std::string line;
    uint32_t noteID = 0;
    while (std::getline(f, line)) {
        if (line.empty() || line[0] == '#') continue;

        std::istringstream iss(line);
        double time;
        int ring;
        float angle;
        int type;

        if (iss >> time >> ring >> angle >> type) {
            NoteEvent ev{};
            ev.id = noteID++;
            ev.time = time;

            if (type == 0) {
                ev.type = NoteType::Tap;
                LanotaRingData ringData{angle, ring};
                ev.data = ringData;
            } else if (type == 1) {
                ev.type = NoteType::Hold;
                LanotaRingData ringData{angle, ring};
                ev.data = ringData;
            }

            chart.notes.push_back(ev);
        }
    }

    return chart;
}
