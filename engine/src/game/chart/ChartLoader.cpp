#include "ChartLoader.h"
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <algorithm>

ChartData ChartLoader::load(const std::string& path) {
    std::ifstream f(path);
    if (!f.is_open()) throw std::runtime_error("Cannot open: " + path);

    // Dispatch by extension for non-JSON legacy formats first
    auto dotPos = path.rfind('.');
    std::string ext;
    if (dotPos != std::string::npos) {
        ext = path.substr(dotPos + 1);
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    }

    if (ext == "pec" || ext == "pgr") return loadPhigros(path);
    if (ext == "aff")   return loadArcaea(path);
    if (ext == "xml")   return loadCytus(path);
    if (ext == "lan")   return loadLanota(path);

    // For JSON files, read the whole file and check for "version" field
    // to distinguish unified format from Bandori format
    if (ext == "json") {
        std::string content((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        f.close();
        if (content.find("\"version\"") != std::string::npos) {
            return loadUnified(path);
        }
        return loadBandori(path);
    }

    throw std::runtime_error("Unknown chart format: " + ext);
}

// Find the matching closing bracket for an opening '[' at position `start`,
// correctly handling nested brackets.
static size_t findMatchingBracket(const std::string& s, size_t start) {
    int depth = 1;
    for (size_t i = start + 1; i < s.size(); ++i) {
        if (s[i] == '[') ++depth;
        else if (s[i] == ']') { if (--depth == 0) return i; }
    }
    return std::string::npos;
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

    // Parse timing block → single TimingPoint at t=0
    // UCF schema: "timing": { "bpm": 120.0, "timeSignature": "4/4" }
    {
        std::string bpmStr = findValue("bpm");
        TimingPoint tp{};
        tp.time  = 0.0;
        tp.bpm   = bpmStr.empty() ? 120.f : std::stof(bpmStr);
        tp.meter = 4;
        std::string ts = findValue("timeSignature");
        if (!ts.empty() && ts[0] >= '1' && ts[0] <= '9')
            tp.meter = std::stoi(ts);
        chart.timingPoints.push_back(tp);
    }

    // Parse notes array (handles nested arrays safely)
    auto notesPos = content.find("\"notes\"");
    if (notesPos != std::string::npos) {
        auto arrayStart = content.find('[', notesPos);
        auto arrayEnd   = findMatchingBracket(content, arrayStart);
        if (arrayEnd == std::string::npos) arrayEnd = content.size();
        std::string notesStr = content.substr(arrayStart + 1, arrayEnd - arrayStart - 1);

        uint32_t noteID = 0;
        size_t pos = 0;
        while ((pos = notesStr.find('{', pos)) != std::string::npos) {
            auto objEnd = notesStr.find('}', pos);
            if (objEnd == std::string::npos) break;
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

            auto getFloat = [&](const std::string& k, float def = 0.f) -> float {
                std::string v = getVal(k);
                return v.empty() ? def : std::stof(v);
            };
            auto getInt = [&](const std::string& k, int def = 0) -> int {
                std::string v = getVal(k);
                return v.empty() ? def : std::stoi(v);
            };

            NoteEvent ev{};
            ev.id = noteID++;
            ev.time = std::stod(getVal("time"));
            std::string type = getVal("type");

            if (type == "tap") {
                ev.type = NoteType::Tap;
                ev.data = TapData{getFloat("lane")};
            } else if (type == "slide") {
                ev.type = NoteType::Slide;
                ev.data = TapData{getFloat("lane")};
            } else if (type == "hold") {
                ev.type = NoteType::Hold;
                ev.data = HoldData{getFloat("lane"), getFloat("duration")};
            } else if (type == "flick") {
                ev.type = NoteType::Flick;
                ev.data = FlickData{getFloat("lane"), getInt("direction")};
            } else if (type == "drag") {
                ev.type = NoteType::Drag;
                ev.data = TapData{getFloat("lane")};
            } else if (type == "arc") {
                ev.type = NoteType::Arc;
                ArcData arc{};
                arc.startPos.x  = getFloat("startX");
                arc.startPos.y  = getFloat("startY");
                arc.endPos.x    = getFloat("endX");
                arc.endPos.y    = getFloat("endY");
                arc.duration    = getFloat("duration");
                arc.curveXEase  = getFloat("curveXEase");
                arc.curveYEase  = getFloat("curveYEase");
                arc.color       = getInt("color");
                std::string voidStr = getVal("isVoid");
                arc.isVoid = (voidStr == "true" || voidStr == "1");
                ev.data = arc;
            } else if (type == "arctap") {
                ev.type = NoteType::ArcTap;
                ev.data = TapData{getFloat("lane")};
            } else if (type == "ring") {
                ev.type = NoteType::Ring;
                ev.data = LanotaRingData{getFloat("angle"), getInt("ringIndex")};
            } else {
                // Unknown type — skip
                pos = objEnd + 1;
                continue;
            }

            chart.notes.push_back(ev);
            pos = objEnd + 1;
        }
    }

    computeBeatPositions(chart);
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

    // Parse system array → TimingPoints (BPM change events)
    auto sysPos = content.find("\"system\"");
    if (sysPos != std::string::npos) {
        auto arrayStart = content.find('[', sysPos);
        auto arrayEnd   = content.find(']', arrayStart);
        std::string sysStr = content.substr(arrayStart + 1, arrayEnd - arrayStart - 1);

        size_t pos = 0;
        while ((pos = sysStr.find('{', pos)) != std::string::npos) {
            auto objEnd = sysStr.find('}', pos);
            std::string obj = sysStr.substr(pos, objEnd - pos + 1);

            auto getVal = [&](const std::string& k) -> std::string {
                auto p = obj.find("\"" + k + "\"");
                if (p == std::string::npos) return "";
                p = obj.find(":", p) + 1;
                while (p < obj.size() && (obj[p] == ' ' || obj[p] == '\t')) p++;
                auto e = p;
                while (e < obj.size() && obj[e] != ',' && obj[e] != '}') e++;
                return obj.substr(p, e - p);
            };

            std::string tStr   = getVal("time");
            std::string bpmStr = getVal("bpm");
            if (!tStr.empty() && !bpmStr.empty()) {
                TimingPoint tp{};
                tp.time  = std::stod(tStr);
                tp.bpm   = std::stof(bpmStr);
                tp.meter = 4;
                chart.timingPoints.push_back(tp);
            }
            pos = objEnd + 1;
        }
    }

    // Ensure there is always at least one timing point
    if (chart.timingPoints.empty()) {
        std::string bpmStr = findValue("bpm");
        TimingPoint tp{};
        tp.time  = 0.0;
        tp.bpm   = bpmStr.empty() ? 120.f : std::stof(bpmStr);
        tp.meter = 4;
        chart.timingPoints.push_back(tp);
    }

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
                std::string endStr = getVal("endTime");
                float duration = endStr.empty() ? 0.f : static_cast<float>(std::stod(endStr) - ev.time);
                ev.data = HoldData{static_cast<float>(lane), duration};
            } else if (type == "Flick") {
                ev.type = NoteType::Flick;
                std::string dirStr = getVal("direction");
                int dir = dirStr.empty() ? 0 : std::stoi(dirStr);
                ev.data = FlickData{static_cast<float>(lane), dir};
            } else if (type == "Slide") {
                ev.type = NoteType::Slide;
                ev.data = TapData{static_cast<float>(lane)};
            }

            chart.notes.push_back(ev);
            pos = objEnd + 1;
        }
    }

    computeBeatPositions(chart);
    return chart;
}

ChartData ChartLoader::loadPhigros(const std::string& path) {
    ChartData chart;
    chart.title = "Phigros Chart";

    std::ifstream f(path);
    if (!f.is_open()) throw std::runtime_error("Cannot open: " + path);

    std::string content((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());

    // Helper: extract a JSON value from a substring given a key
    auto extractVal = [](const std::string& src, const std::string& key) -> std::string {
        auto pos = src.find("\"" + key + "\"");
        if (pos == std::string::npos) return "";
        pos = src.find(":", pos) + 1;
        while (pos < src.size() && (src[pos] == ' ' || src[pos] == '\t')) pos++;
        if (pos < src.size() && src[pos] == '"') {
            pos++;
            auto end = src.find('"', pos);
            return src.substr(pos, end - pos);
        }
        auto end = pos;
        while (end < src.size() && src[end] != ',' && src[end] != '}' && src[end] != ']' && src[end] != '\n') end++;
        return src.substr(pos, end - pos);
    };

    auto toFloat = [](const std::string& s, float def) -> float {
        return s.empty() ? def : std::stof(s);
    };
    auto toDouble = [](const std::string& s, double def) -> double {
        return s.empty() ? def : std::stod(s);
    };

    // Parse top-level "bpm" field
    float bpm = toFloat(extractVal(content, "bpm"), 120.f);
    chart.timingPoints.push_back({0.0, bpm, 4});

    // Parse notes from a notes array section, appending to lineEvent.attachedNotes
    auto parseNotes = [&](const std::string& arrayContent, JudgmentLineEvent& lineEvent,
                          uint32_t& noteID, bool above) {
        size_t pos = 0;
        while ((pos = arrayContent.find('{', pos)) != std::string::npos) {
            auto objEnd = arrayContent.find('}', pos);
            if (objEnd == std::string::npos) break;
            std::string obj = arrayContent.substr(pos, objEnd - pos + 1);

            NoteEvent ev{};
            ev.id = noteID++;

            // Phigros note types: 1=tap, 2=drag, 3=hold, 4=flick
            int typeInt = static_cast<int>(toFloat(extractVal(obj, "type"), 1.f));
            float posOnLine = toFloat(extractVal(obj, "positionX"), 0.f);
            float holdTime  = toFloat(extractVal(obj, "holdTime"), 0.f);

            // Time in Phigros is specified in beats (floorPosition), convert via BPM
            // floorPosition is the accumulated "floor" value; time field if present is in beats
            double noteTime = toDouble(extractVal(obj, "time"), 0.0);
            // Convert from beat count to seconds
            ev.time = noteTime * (60.0 / bpm);

            NoteType subType = NoteType::Tap;
            switch (typeInt) {
                case 1: subType = NoteType::Tap;   ev.type = NoteType::Tap;   break;
                case 2: subType = NoteType::Drag;  ev.type = NoteType::Drag;  break;
                case 3: subType = NoteType::Hold;  ev.type = NoteType::Hold;  break;
                case 4: subType = NoteType::Flick; ev.type = NoteType::Flick; break;
                default: subType = NoteType::Tap;  ev.type = NoteType::Tap;   break;
            }

            PhigrosNoteData pData{};
            pData.posOnLine = posOnLine;
            pData.subType   = subType;
            pData.duration  = holdTime * (60.f / bpm);  // convert beats to seconds
            ev.data = pData;

            lineEvent.attachedNotes.push_back(ev);
            pos = objEnd + 1;
        }
    };

    // Parse judgment lines
    auto listPos = content.find("\"judgeLineList\"");
    if (listPos != std::string::npos) {
        auto listArrayStart = content.find('[', listPos);
        auto listArrayEnd   = findMatchingBracket(content, listArrayStart);
        if (listArrayEnd == std::string::npos) listArrayEnd = content.size();

        uint32_t noteID = 0;
        size_t pos = listArrayStart + 1;
        // Iterate judgment line objects — find each '{' at depth 1
        while (pos < listArrayEnd) {
            pos = content.find('{', pos);
            if (pos == std::string::npos || pos >= listArrayEnd) break;

            // Find the matching '}' for this line object (handles nested braces)
            int depth = 1;
            size_t objEnd = pos + 1;
            while (objEnd < listArrayEnd && depth > 0) {
                if (content[objEnd] == '{') ++depth;
                else if (content[objEnd] == '}') --depth;
                ++objEnd;
            }
            std::string lineObj = content.substr(pos, objEnd - pos);

            JudgmentLineEvent lineEvent{};
            lineEvent.time     = 0.0;
            lineEvent.position = {toFloat(extractVal(lineObj, "posX"), 0.5f),
                                  toFloat(extractVal(lineObj, "posY"), 0.5f)};
            lineEvent.rotation = toFloat(extractVal(lineObj, "rotation"), 0.f);
            lineEvent.speed    = toFloat(extractVal(lineObj, "speed"), 1.f);

            // Parse notesAbove
            auto abovePos = lineObj.find("\"notesAbove\"");
            if (abovePos != std::string::npos) {
                auto arrStart = lineObj.find('[', abovePos);
                auto arrEnd   = findMatchingBracket(lineObj, arrStart);
                if (arrStart != std::string::npos && arrEnd != std::string::npos) {
                    std::string arrContent = lineObj.substr(arrStart + 1, arrEnd - arrStart - 1);
                    parseNotes(arrContent, lineEvent, noteID, true);
                }
            }

            // Parse notesBelow
            auto belowPos = lineObj.find("\"notesBelow\"");
            if (belowPos != std::string::npos) {
                auto arrStart = lineObj.find('[', belowPos);
                auto arrEnd   = findMatchingBracket(lineObj, arrStart);
                if (arrStart != std::string::npos && arrEnd != std::string::npos) {
                    std::string arrContent = lineObj.substr(arrStart + 1, arrEnd - arrStart - 1);
                    parseNotes(arrContent, lineEvent, noteID, false);
                }
            }

            chart.judgmentLines.push_back(lineEvent);
            pos = objEnd;
        }
    }

    computeBeatPositions(chart);
    return chart;
}

// Map Arcaea easing string to a numeric ease value for ArcData.
// Arcaea uses: s, b, si, so, sisi, siso, sosi, soso
static float parseArcaeaEase(const std::string& e) {
    if (e == "s")    return 0.f;   // linear
    if (e == "b")    return 1.f;   // bezier (generic ease)
    if (e == "si")   return 2.f;   // sine-in
    if (e == "so")   return -2.f;  // sine-out
    if (e == "sisi") return 3.f;   // sine-in-sine-in
    if (e == "siso") return -3.f;  // sine-in-sine-out
    if (e == "sosi") return 4.f;   // sine-out-sine-in
    if (e == "soso") return -4.f;  // sine-out-sine-out
    return 0.f; // default linear
}

ChartData ChartLoader::loadArcaea(const std::string& path) {
    ChartData chart;
    chart.title = "Arcaea Chart";

    std::ifstream f(path);
    if (!f.is_open()) throw std::runtime_error("Cannot open: " + path);

    std::string line;
    uint32_t noteID = 0;
    while (std::getline(f, line)) {
        // Strip trailing \r from Windows line endings
        if (!line.empty() && line.back() == '\r') line.pop_back();

        // timing(time_ms, bpm, meter);
        if (line.rfind("timing(", 0) == 0) {
            double timeMs = 0.0;
            float bpm = 120.f, meter = 4.f;
            sscanf(line.c_str(), "timing(%lf,%f,%f)", &timeMs, &bpm, &meter);
            TimingPoint tp{};
            tp.time  = timeMs / 1000.0;
            tp.bpm   = bpm;
            tp.meter = static_cast<int>(meter);
            chart.timingPoints.push_back(tp);
        }
        // arc(startMs,endMs,startX,endX,easeType,startY,endY,color,fx,isVoid)[arctap(ms),...];
        else if (line.rfind("arc(", 0) == 0) {
            NoteEvent ev{};
            ev.type = NoteType::Arc;
            ev.id = noteID++;
            ArcData arc{};

            // Manual parsing — sscanf can't handle the string easing field well
            auto inner = line.substr(4); // skip "arc("
            auto closeParen = inner.find(')');
            std::string params = inner.substr(0, closeParen);

            // Split by comma
            std::vector<std::string> parts;
            {
                std::istringstream ss(params);
                std::string tok;
                while (std::getline(ss, tok, ',')) parts.push_back(tok);
            }

            // arc has at least 9 fields: startMs,endMs,startX,endX,easeType,startY,endY,color,fx
            // Optional 10th: isVoid (true/false)
            if (parts.size() >= 9) {
                double startMs = std::stod(parts[0]);
                double endMs   = std::stod(parts[1]);
                arc.startPos.x = std::stof(parts[2]);
                arc.endPos.x   = std::stof(parts[3]);
                std::string easeType = parts[4];
                arc.startPos.y = std::stof(parts[5]);
                arc.endPos.y   = std::stof(parts[6]);
                arc.color      = std::stoi(parts[7]);
                // parts[8] = fx (sound effect name, not stored)

                arc.curveXEase = parseArcaeaEase(easeType);
                arc.curveYEase = arc.curveXEase; // Arcaea uses same easing for both axes

                ev.time = startMs / 1000.0;
                arc.duration = (endMs / 1000.0) - ev.time;

                // Check for isVoid — may appear as 10th param or after closing paren
                arc.isVoid = false;
                if (parts.size() >= 10) {
                    std::string voidStr = parts[9];
                    // Trim whitespace and closing chars
                    while (!voidStr.empty() && (voidStr.back() == ')' || voidStr.back() == ';'))
                        voidStr.pop_back();
                    arc.isVoid = (voidStr == "true");
                }
            }

            ev.data = arc;
            chart.notes.push_back(ev);

            // Parse arctap sub-notes: [arctap(ms),arctap(ms),...]
            auto bracketStart = inner.find('[');
            if (bracketStart != std::string::npos) {
                auto bracketEnd = inner.find(']', bracketStart);
                std::string arctapStr = inner.substr(bracketStart + 1, bracketEnd - bracketStart - 1);
                size_t atPos = 0;
                while ((atPos = arctapStr.find("arctap(", atPos)) != std::string::npos) {
                    atPos += 7; // skip "arctap("
                    auto atEnd = arctapStr.find(')', atPos);
                    double atMs = std::stod(arctapStr.substr(atPos, atEnd - atPos));

                    NoteEvent atEv{};
                    atEv.type = NoteType::ArcTap;
                    atEv.id = noteID++;
                    atEv.time = atMs / 1000.0;
                    // ArcTap position is interpolated from the parent arc at its timestamp
                    float t = (arc.duration > 0.f)
                        ? static_cast<float>((atEv.time - ev.time) / arc.duration)
                        : 0.f;
                    t = std::clamp(t, 0.f, 1.f);
                    float atX = arc.startPos.x + (arc.endPos.x - arc.startPos.x) * t;
                    atEv.data = TapData{atX};
                    chart.notes.push_back(atEv);

                    atPos = atEnd + 1;
                }
            }
        }
        // hold(startMs,endMs,lane);
        else if (line.rfind("hold(", 0) == 0) {
            NoteEvent ev{};
            ev.type = NoteType::Hold;
            ev.id = noteID++;
            double startMs, endMs;
            int lane;
            sscanf(line.c_str(), "hold(%lf,%lf,%d)", &startMs, &endMs, &lane);
            ev.time = startMs / 1000.0;
            HoldData hold{};
            hold.laneX = static_cast<float>(lane);
            hold.duration = (endMs / 1000.0) - ev.time;
            ev.data = hold;
            chart.notes.push_back(ev);
        }
        // (time_ms,lane); — ground tap note
        else if (line.rfind("(", 0) == 0) {
            NoteEvent ev{};
            ev.type = NoteType::Tap;
            ev.id = noteID++;
            double timeMs;
            int lane;
            sscanf(line.c_str(), "(%lf,%d)", &timeMs, &lane);
            ev.time = timeMs / 1000.0;
            TapData tap{};
            tap.laneX = static_cast<float>(lane);
            ev.data = tap;
            chart.notes.push_back(ev);
        }
    }

    if (chart.timingPoints.empty())
        chart.timingPoints.push_back({0.0, 120.f, 4});
    computeBeatPositions(chart);
    return chart;
}

ChartData ChartLoader::loadCytus(const std::string& path) {
    ChartData chart;
    chart.title = "Cytus Chart";

    std::ifstream f(path);
    if (!f.is_open()) throw std::runtime_error("Cannot open: " + path);

    std::string content((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());

    // Parse BPM events: <bpm_event time="..." bpm="..."/>
    {
        size_t bpmPos = 0;
        while ((bpmPos = content.find("<bpm_event", bpmPos)) != std::string::npos) {
            auto end = content.find("/>", bpmPos);
            std::string tag = content.substr(bpmPos, end - bpmPos);

            auto getAttr = [&](const std::string& attr) -> float {
                auto p = tag.find(attr + "=\"");
                if (p == std::string::npos) return 0.f;
                p += attr.size() + 2;
                return std::stof(tag.substr(p, tag.find('"', p) - p));
            };

            TimingPoint tp{};
            tp.time  = getAttr("time");
            tp.bpm   = getAttr("bpm");
            tp.meter = 4;
            if (tp.bpm > 0.f) chart.timingPoints.push_back(tp);
            bpmPos = end;
        }
    }

    if (chart.timingPoints.empty())
        chart.timingPoints.push_back({0.0, 120.f, 4});

    uint32_t noteID = 0;
    size_t pos = 0;
    while ((pos = content.find("<note", pos)) != std::string::npos) {
        // Handle both self-closing <note .../> and <note ...></note>
        auto selfClose = content.find("/>", pos);
        auto tagClose  = content.find(">", pos);
        size_t end = selfClose;
        if (tagClose < selfClose) {
            // Check if it's a self-closing tag or a regular close
            if (content[tagClose - 1] != '/') {
                // Find </note>
                auto closeTag = content.find("</note>", tagClose);
                end = (closeTag != std::string::npos) ? closeTag + 6 : selfClose;
            }
        }
        if (end == std::string::npos) break;

        std::string noteTag = content.substr(pos, end - pos);

        NoteEvent ev{};
        ev.id = noteID++;

        auto getAttrStr = [&](const std::string& attr) -> std::string {
            auto p = noteTag.find(attr + "=\"");
            if (p == std::string::npos) return "";
            p += attr.length() + 2;
            auto e = noteTag.find('"', p);
            return noteTag.substr(p, e - p);
        };

        auto getAttrD = [&](const std::string& attr) -> double {
            std::string s = getAttrStr(attr);
            return s.empty() ? 0.0 : std::stod(s);
        };

        auto getAttrF = [&](const std::string& attr) -> float {
            std::string s = getAttrStr(attr);
            return s.empty() ? 0.f : std::stof(s);
        };

        ev.time = getAttrD("time");
        float x = getAttrF("x");
        int type = static_cast<int>(getAttrF("type"));

        // Cytus note types: 0=tap, 1=hold, 2=drag
        if (type == 0) {
            ev.type = NoteType::Tap;
            ev.data = TapData{x};
        } else if (type == 1) {
            ev.type = NoteType::Hold;
            ev.data = HoldData{x, getAttrF("duration")};
        } else if (type == 2) {
            ev.type = NoteType::Drag;
            ev.data = TapData{x};
        } else if (type == 3) {
            ev.type = NoteType::Flick;
            ev.data = FlickData{x, static_cast<int>(getAttrF("direction"))};
        }

        chart.notes.push_back(ev);
        pos = end + 1;
    }

    computeBeatPositions(chart);
    return chart;
}

void ChartLoader::computeBeatPositions(ChartData& chart) {
    if (chart.timingPoints.empty()) return;

    // Sort here so callers don't have to — the else break in assignBeat
    // requires ascending order, and not every loader was sorting first.
    std::sort(chart.timingPoints.begin(), chart.timingPoints.end(),
              [](const TimingPoint& a, const TimingPoint& b){ return a.time < b.time; });

    // Pre-compute accumulated beat count at the start of each timing segment.
    const auto& tps = chart.timingPoints;
    std::vector<double> accum(tps.size(), 0.0);
    for (size_t i = 1; i < tps.size(); ++i) {
        double segDuration = tps[i].time - tps[i - 1].time;
        accum[i] = accum[i - 1] + segDuration * (tps[i - 1].bpm / 60.0);
    }

    // Assign beatPosition to one note given the pre-built accum table.
    auto assignBeat = [&](NoteEvent& note) {
        size_t seg = 0;
        for (size_t i = 1; i < tps.size(); ++i) {
            if (tps[i].time <= note.time) seg = i;
            else break;
        }
        note.beatPosition = accum[seg] + (note.time - tps[seg].time) * (tps[seg].bpm / 60.0);
    };

    for (auto& note : chart.notes)
        assignBeat(note);

    // Phigros: notes live on judgment lines, not in chart.notes
    for (auto& line : chart.judgmentLines)
        for (auto& note : line.attachedNotes)
            assignBeat(note);
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

        // BPM directive: "bpm <time> <value>"
        if (line.rfind("bpm ", 0) == 0) {
            std::istringstream iss(line.substr(4));
            double t; float bpm;
            if (iss >> t >> bpm) {
                TimingPoint tp{};
                tp.time  = t;
                tp.bpm   = bpm;
                tp.meter = 4;
                chart.timingPoints.push_back(tp);
            }
            continue;
        }

        std::istringstream iss(line);
        double time;
        int ring;
        float angle;
        int type;

        if (iss >> time >> ring >> angle >> type) {
            NoteEvent ev{};
            ev.id = noteID++;
            ev.time = time;

            // Lanota note types: 0=tap, 1=hold, 2=flick
            if (type == 0) {
                ev.type = NoteType::Tap;
                ev.data = LanotaRingData{angle, ring};
            } else if (type == 1) {
                ev.type = NoteType::Hold;
                // Read optional hold duration (5th field)
                float duration = 0.f;
                iss >> duration;
                // Store as HoldData so duration is accessible; laneX encodes the ring+angle
                ev.data = HoldData{angle, duration};
            } else if (type == 2) {
                ev.type = NoteType::Flick;
                int direction = 0;
                iss >> direction;
                ev.data = FlickData{angle, direction};
            } else {
                ev.type = NoteType::Tap;
                ev.data = LanotaRingData{angle, ring};
            }

            chart.notes.push_back(ev);
        }
    }

    if (chart.timingPoints.empty())
        chart.timingPoints.push_back({0.0, 120.f, 4});
    computeBeatPositions(chart);
    return chart;
}
