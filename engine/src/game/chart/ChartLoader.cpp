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

// Find the matching closing brace for an opening '{' at position `start`,
// correctly handling nested braces and brackets so notes containing
// "waypoints": [{...}, {...}] still have their outer brace identified.
static size_t findMatchingBrace(const std::string& s, size_t start) {
    int depth = 1;
    for (size_t i = start + 1; i < s.size(); ++i) {
        char c = s[i];
        if (c == '{') ++depth;
        else if (c == '}') { if (--depth == 0) return i; }
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
        if (pos >= content.size()) return "";
        if (content[pos] == '"') {
            pos++;
            auto end = content.find('"', pos);
            if (end == std::string::npos) return "";
            return content.substr(pos, end - pos);
        }
        auto end = pos;
        while (end < content.size() && content[end] != ',' && content[end] != '}' && content[end] != '\n') end++;
        return content.substr(pos, end - pos);
    };

    chart.title = findValue("title");
    chart.artist = findValue("artist");
    chart.offset = std::stof(findValue("offset").empty() ? "0" : findValue("offset"));

    // Parse timing block → TimingPoints
    // UCF schema: "timing": { "bpm": 120.0, "timeSignature": "4/4", "bpm_changes": [...] }
    {
        int defaultMeter = 4;
        std::string ts = findValue("timeSignature");
        if (!ts.empty() && ts[0] >= '1' && ts[0] <= '9')
            defaultMeter = std::stoi(ts);

        // Check for bpm_changes array (dynamic BPM)
        auto bpmChangesPos = content.find("\"bpm_changes\"");
        if (bpmChangesPos != std::string::npos) {
            auto arrStart = content.find('[', bpmChangesPos);
            if (arrStart != std::string::npos) {
                auto arrEnd = findMatchingBracket(content, arrStart);
                if (arrEnd == std::string::npos) arrEnd = content.find(']', arrStart);
                std::string arrStr = content.substr(arrStart + 1, arrEnd - arrStart - 1);

                size_t objPos = 0;
                while ((objPos = arrStr.find('{', objPos)) != std::string::npos) {
                    auto objEnd = arrStr.find('}', objPos);
                    if (objEnd == std::string::npos) break;
                    std::string obj = arrStr.substr(objPos, objEnd - objPos + 1);

                    auto getVal = [&](const std::string& k) -> std::string {
                        auto p = obj.find("\"" + k + "\"");
                        if (p == std::string::npos) return "";
                        p = obj.find(':', p) + 1;
                        while (p < obj.size() && (obj[p] == ' ' || obj[p] == '\t')) p++;
                        auto e = p;
                        while (e < obj.size() && obj[e] != ',' && obj[e] != '}') e++;
                        return obj.substr(p, e - p);
                    };

                    std::string tStr = getVal("time");
                    std::string bpmStr = getVal("bpm");
                    if (!tStr.empty() && !bpmStr.empty()) {
                        TimingPoint tp{};
                        tp.time  = std::stod(tStr);
                        tp.bpm   = std::stof(bpmStr);
                        tp.meter = defaultMeter;
                        chart.timingPoints.push_back(tp);
                    }
                    objPos = objEnd + 1;
                }
            }
        }

        // Fallback: single BPM at t=0
        if (chart.timingPoints.empty()) {
            std::string bpmStr = findValue("bpm");
            TimingPoint tp{};
            tp.time  = 0.0;
            tp.bpm   = bpmStr.empty() ? 120.f : std::stof(bpmStr);
            tp.meter = defaultMeter;
            chart.timingPoints.push_back(tp);
        }
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
            // Use brace matching so notes containing nested objects (e.g.
            // "waypoints": [{...}, {...}]) still have their outer brace
            // identified correctly.
            auto objEnd = findMatchingBrace(notesStr, pos);
            if (objEnd == std::string::npos) break;
            std::string noteObj = notesStr.substr(pos, objEnd - pos + 1);

            auto getVal = [&](const std::string& k) {
                auto p = noteObj.find("\"" + k + "\"");
                if (p == std::string::npos) return std::string("");
                p = noteObj.find(":", p) + 1;
                while (p < noteObj.size() && (noteObj[p] == ' ' || noteObj[p] == '\t')) p++;
                if (p >= noteObj.size()) return std::string("");
                if (noteObj[p] == '"') {
                    p++;
                    auto e = noteObj.find('"', p);
                    if (e == std::string::npos) return std::string("");
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

            auto clampSpan = [&]() {
                int s = getInt("laneSpan", 1);
                if (s < 1) s = 1;
                if (s > 3) s = 3;
                return s;
            };
            if (type == "tap") {
                ev.type = NoteType::Tap;
                ev.data = TapData{getFloat("lane"), clampSpan()};
            } else if (type == "slide") {
                ev.type = NoteType::Slide;
                TapData td{};
                td.laneX    = getFloat("lane");
                td.laneSpan = clampSpan();
                td.duration = getFloat("duration");
                // "samples": [t1, t2, ...] — slide tick offsets
                auto sp = noteObj.find("\"samples\"");
                if (sp != std::string::npos) {
                    auto lb = noteObj.find('[', sp);
                    auto rb = noteObj.find(']', lb);
                    if (lb != std::string::npos && rb != std::string::npos) {
                        std::string body = noteObj.substr(lb + 1, rb - lb - 1);
                        size_t q = 0;
                        while (q < body.size()) {
                            while (q < body.size() && (body[q]==' '||body[q]==','||body[q]=='\t')) q++;
                            if (q >= body.size()) break;
                            size_t e = q;
                            while (e < body.size() && body[e] != ',' && body[e] != ' ') e++;
                            try { td.samplePoints.push_back(std::stof(body.substr(q, e - q))); }
                            catch (...) {}
                            q = e;
                        }
                    }
                }
                ev.data = std::move(td);
            } else if (type == "hold") {
                ev.type = NoteType::Hold;
                HoldData hd{};
                hd.laneX    = getFloat("lane");
                hd.duration = getFloat("duration");
                hd.laneSpan = clampSpan();

                // Cross-lane fields (optional; omitted for straight holds)
                std::string endLaneStr = getVal("endLane");
                if (!endLaneStr.empty()) {
                    hd.endLaneX = std::stof(endLaneStr);
                    std::string tStr = getVal("transition");
                    if      (tStr == "angle90")  hd.transition = HoldTransition::Angle90;
                    else if (tStr == "curve")    hd.transition = HoldTransition::Curve;
                    else if (tStr == "rhomboid") hd.transition = HoldTransition::Rhomboid;
                    else                         hd.transition = HoldTransition::Straight;
                    hd.transitionLen = getFloat("transitionLen");
                    std::string tsStr = getVal("transitionStart");
                    hd.transitionStart = tsStr.empty() ? -1.f : std::stof(tsStr);
                } else {
                    hd.endLaneX = -1.f;
                }

                // Multi-waypoint path: parse "waypoints": [{t,lane,len,style}, ...]
                auto wpKey = noteObj.find("\"waypoints\"");
                if (wpKey != std::string::npos) {
                    auto lb = noteObj.find('[', wpKey);
                    auto rb = noteObj.find(']', lb);
                    if (lb != std::string::npos && rb != std::string::npos) {
                        std::string body = noteObj.substr(lb + 1, rb - lb - 1);
                        size_t p = 0;
                        while ((p = body.find('{', p)) != std::string::npos) {
                            auto e = body.find('}', p);
                            if (e == std::string::npos) break;
                            std::string obj = body.substr(p, e - p + 1);
                            auto getF = [&](const std::string& k, float def) {
                                auto kp = obj.find("\"" + k + "\"");
                                if (kp == std::string::npos) return def;
                                kp = obj.find(':', kp) + 1;
                                while (kp < obj.size() && (obj[kp]==' '||obj[kp]=='\t')) kp++;
                                size_t ee = kp;
                                while (ee < obj.size() && obj[ee] != ',' && obj[ee] != '}') ee++;
                                try { return std::stof(obj.substr(kp, ee - kp)); } catch (...) { return def; }
                            };
                            auto getS = [&](const std::string& k) {
                                auto kp = obj.find("\"" + k + "\"");
                                if (kp == std::string::npos) return std::string();
                                kp = obj.find(':', kp) + 1;
                                while (kp < obj.size() && (obj[kp]==' '||obj[kp]=='\t'||obj[kp]=='"')) kp++;
                                size_t ee = kp;
                                while (ee < obj.size() && obj[ee] != ',' && obj[ee] != '}' && obj[ee] != '"') ee++;
                                return obj.substr(kp, ee - kp);
                            };
                            HoldWaypoint hw{};
                            hw.tOffset       = getF("t", 0.f);
                            hw.lane          = (int)getF("lane", 0.f);
                            hw.transitionLen = getF("len", 0.f);
                            std::string st = getS("style");
                            if      (st == "angle90")  hw.style = HoldTransition::Angle90;
                            else if (st == "rhomboid") hw.style = HoldTransition::Rhomboid;
                            else if (st == "straight") hw.style = HoldTransition::Straight;
                            else                       hw.style = HoldTransition::Curve;
                            hd.waypoints.push_back(hw);
                            p = e + 1;
                        }
                        if (!hd.waypoints.empty()) {
                            hd.endLaneX = static_cast<float>(hd.waypoints.back().lane);
                        }
                    }
                }

                // Sample points: parse "samples": [t1, t2, ...]
                auto sp = noteObj.find("\"samples\"");
                if (sp != std::string::npos) {
                    auto lb = noteObj.find('[', sp);
                    auto rb = noteObj.find(']', lb);
                    if (lb != std::string::npos && rb != std::string::npos) {
                        std::string body = noteObj.substr(lb + 1, rb - lb - 1);
                        size_t q = 0;
                        while (q < body.size()) {
                            while (q < body.size() && (body[q] == ' ' || body[q] == ',' || body[q] == '\t')) q++;
                            if (q >= body.size()) break;
                            size_t e = q;
                            while (e < body.size() && body[e] != ',' && body[e] != ' ') e++;
                            try {
                                hd.samplePoints.push_back({std::stof(body.substr(q, e - q))});
                            } catch (...) {}
                            q = e;
                        }
                    }
                }

                ev.data = hd;
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
                // Accept both old ("curveXEase") and new ("easeX") field names
                float ex = getFloat("easeX");
                arc.curveXEase  = (ex != 0.f) ? ex : getFloat("curveXEase");
                float ey = getFloat("easeY");
                arc.curveYEase  = (ey != 0.f) ? ey : getFloat("curveYEase");
                arc.color       = getInt("color");
                // Accept both "void" and "isVoid" field names
                std::string voidStr = getVal("void");
                if (voidStr.empty()) voidStr = getVal("isVoid");
                arc.isVoid = (voidStr == "true" || voidStr == "1");
                ev.data = arc;
            } else if (type == "arctap") {
                ev.type = NoteType::ArcTap;
                TapData td{};
                td.laneX = getFloat("lane");
                // Read arc position if available (from editor export)
                float ax = getFloat("arcX");
                float ay = getFloat("arcY");
                if (ax != 0.f || ay != 0.f) {
                    td.laneX = ax;
                    td.scanY = ay;
                }
                ev.data = td;
            } else if (type == "ring") {
                ev.type = NoteType::Ring;
                int span = getInt("laneSpan");
                if (span < 1) span = 1;
                if (span > 3) span = 3;
                ev.data = LanotaRingData{getFloat("angle"), getInt("ringIndex"), span};
            } else {
                // Unknown type — skip
                pos = objEnd + 1;
                continue;
            }

            // ── Scan-line coordinates block: "scan": {x, y, endY, path} ─
            // Distributes the fields across whichever variant is currently
            // assigned to ev.data. Only written by the scan-line editor.
            auto scanKey = noteObj.find("\"scan\"");
            if (scanKey != std::string::npos) {
                auto lb = noteObj.find('{', scanKey);
                auto rb = (lb != std::string::npos) ? findMatchingBrace(noteObj, lb)
                                                    : std::string::npos;
                if (lb != std::string::npos && rb != std::string::npos) {
                    std::string body = noteObj.substr(lb, rb - lb + 1);

                    auto getF = [&](const std::string& k, float def) -> float {
                        auto kp = body.find("\"" + k + "\"");
                        if (kp == std::string::npos) return def;
                        kp = body.find(':', kp) + 1;
                        while (kp < body.size() && (body[kp]==' '||body[kp]=='\t')) kp++;
                        size_t ee = kp;
                        while (ee < body.size() && body[ee] != ',' && body[ee] != '}') ee++;
                        try { return std::stof(body.substr(kp, ee - kp)); }
                        catch (...) { return def; }
                    };

                    float sx = getF("x", 0.f);
                    float sy = getF("y", 0.f);
                    float sey = getF("endY", -1.f);
                    int   sweeps = static_cast<int>(getF("sweeps", 0.f));

                    // Parse path: [[x,y], [x,y], ...]
                    std::vector<std::pair<float,float>> path;
                    auto pk = body.find("\"path\"");
                    if (pk != std::string::npos) {
                        auto plb = body.find('[', pk);
                        auto prb = (plb != std::string::npos)
                                   ? findMatchingBracket(body, plb)
                                   : std::string::npos;
                        if (plb != std::string::npos && prb != std::string::npos) {
                            std::string pbody = body.substr(plb + 1, prb - plb - 1);
                            size_t q = 0;
                            while (q < pbody.size()) {
                                auto ilb = pbody.find('[', q);
                                if (ilb == std::string::npos) break;
                                auto irb = pbody.find(']', ilb);
                                if (irb == std::string::npos) break;
                                std::string pair = pbody.substr(ilb + 1, irb - ilb - 1);
                                // Split on comma
                                auto comma = pair.find(',');
                                if (comma != std::string::npos) {
                                    try {
                                        float px = std::stof(pair.substr(0, comma));
                                        float py = std::stof(pair.substr(comma + 1));
                                        path.emplace_back(px, py);
                                    } catch (...) {}
                                }
                                q = irb + 1;
                            }
                        }
                    }

                    if (auto* tap = std::get_if<TapData>(&ev.data)) {
                        tap->scanX = sx;
                        tap->scanY = sy;
                        if (!path.empty()) tap->scanPath = std::move(path);
                    } else if (auto* hold = std::get_if<HoldData>(&ev.data)) {
                        hold->scanX           = sx;
                        hold->scanY           = sy;
                        hold->scanEndY        = sey;
                        hold->scanHoldSweeps  = sweeps;
                    } else if (auto* flick = std::get_if<FlickData>(&ev.data)) {
                        flick->scanX = sx;
                        flick->scanY = sy;
                    }
                }
            }

            chart.notes.push_back(ev);
            pos = objEnd + 1;
        }
    }

    // ── Lanota / circle-mode disk animation ────────────────────────────
    // "diskAnimation": { "rotations": [...], "moves": [...], "scales": [...] }
    // Each entry: {"startTime": 1.0, "duration": 0.5, "target": 3.14 | [x,y] | 0.8, "easing": "sineInOut"}
    {
        auto daKey = content.find("\"diskAnimation\"");
        if (daKey != std::string::npos) {
            auto daBraceL = content.find('{', daKey);
            auto daBraceR = (daBraceL != std::string::npos)
                            ? findMatchingBrace(content, daBraceL)
                            : std::string::npos;
            if (daBraceL != std::string::npos && daBraceR != std::string::npos) {
                std::string daBody = content.substr(daBraceL + 1, daBraceR - daBraceL - 1);

                auto parseEasing = [](const std::string& s) {
                    if (s == "linear")      return DiskEasing::Linear;
                    if (s == "quadInOut")   return DiskEasing::QuadInOut;
                    if (s == "cubicInOut")  return DiskEasing::CubicInOut;
                    return DiskEasing::SineInOut;
                };
                auto findArray = [&](const std::string& key) -> std::string {
                    auto kp = daBody.find("\"" + key + "\"");
                    if (kp == std::string::npos) return "";
                    auto lb = daBody.find('[', kp);
                    if (lb == std::string::npos) return "";
                    auto rb = findMatchingBracket(daBody, lb);
                    if (rb == std::string::npos) return "";
                    return daBody.substr(lb + 1, rb - lb - 1);
                };
                auto objGetStr = [](const std::string& obj, const std::string& k) {
                    auto p = obj.find("\"" + k + "\"");
                    if (p == std::string::npos) return std::string();
                    p = obj.find(':', p) + 1;
                    while (p < obj.size() && (obj[p]==' '||obj[p]=='\t')) p++;
                    if (p < obj.size() && obj[p] == '"') {
                        p++;
                        auto e = obj.find('"', p);
                        return obj.substr(p, e - p);
                    }
                    auto e = p;
                    while (e < obj.size() && obj[e] != ',' && obj[e] != '}') e++;
                    return obj.substr(p, e - p);
                };
                auto objGetFloat = [&](const std::string& obj, const std::string& k, float def) {
                    std::string v = objGetStr(obj, k);
                    if (v.empty()) return def;
                    try { return std::stof(v); } catch (...) { return def; }
                };
                auto objGetDouble = [&](const std::string& obj, const std::string& k, double def) {
                    std::string v = objGetStr(obj, k);
                    if (v.empty()) return def;
                    try { return std::stod(v); } catch (...) { return def; }
                };
                auto objGetVec2 = [](const std::string& obj, glm::vec2 def) {
                    auto p = obj.find("\"target\"");
                    if (p == std::string::npos) return def;
                    auto lb = obj.find('[', p);
                    auto rb = (lb != std::string::npos) ? obj.find(']', lb) : std::string::npos;
                    if (lb == std::string::npos || rb == std::string::npos) return def;
                    std::string inner = obj.substr(lb + 1, rb - lb - 1);
                    auto comma = inner.find(',');
                    if (comma == std::string::npos) return def;
                    try {
                        return glm::vec2(std::stof(inner.substr(0, comma)),
                                         std::stof(inner.substr(comma + 1)));
                    } catch (...) { return def; }
                };

                // Rotations
                {
                    std::string arr = findArray("rotations");
                    size_t p = 0;
                    while ((p = arr.find('{', p)) != std::string::npos) {
                        auto e = arr.find('}', p);
                        if (e == std::string::npos) break;
                        std::string obj = arr.substr(p, e - p + 1);
                        DiskRotationEvent dev{};
                        dev.startTime   = objGetDouble(obj, "startTime", 0.0);
                        dev.duration    = objGetDouble(obj, "duration",  0.0);
                        dev.targetAngle = objGetFloat (obj, "target",    0.f);
                        dev.easing      = parseEasing (objGetStr(obj, "easing"));
                        chart.diskAnimation.rotations.push_back(dev);
                        p = e + 1;
                    }
                }
                // Moves (target is a [x,y] array, so match with findMatchingBrace)
                {
                    std::string arr = findArray("moves");
                    size_t p = 0;
                    while ((p = arr.find('{', p)) != std::string::npos) {
                        auto e = findMatchingBrace(arr, p);
                        if (e == std::string::npos) break;
                        std::string obj = arr.substr(p, e - p + 1);
                        DiskMoveEvent dev{};
                        dev.startTime = objGetDouble(obj, "startTime", 0.0);
                        dev.duration  = objGetDouble(obj, "duration",  0.0);
                        dev.target    = objGetVec2  (obj, {0.f, 0.f});
                        dev.easing    = parseEasing (objGetStr(obj, "easing"));
                        chart.diskAnimation.moves.push_back(dev);
                        p = e + 1;
                    }
                }
                // Scales
                {
                    std::string arr = findArray("scales");
                    size_t p = 0;
                    while ((p = arr.find('{', p)) != std::string::npos) {
                        auto e = arr.find('}', p);
                        if (e == std::string::npos) break;
                        std::string obj = arr.substr(p, e - p + 1);
                        DiskScaleEvent dev{};
                        dev.startTime   = objGetDouble(obj, "startTime", 0.0);
                        dev.duration    = objGetDouble(obj, "duration",  0.0);
                        dev.targetScale = objGetFloat (obj, "target",    1.f);
                        dev.easing      = parseEasing (objGetStr(obj, "easing"));
                        chart.diskAnimation.scales.push_back(dev);
                        p = e + 1;
                    }
                }
            }
        }
    }

    // ── Scan-line speed events ──────────────────────────────────────────
    // "scanSpeedEvents": [{ "startTime": 5.0, "duration": 2.0, "targetSpeed": 2.0, "easing": "sineInOut" }]
    {
        auto ssKey = content.find("\"scanSpeedEvents\"");
        if (ssKey != std::string::npos) {
            auto arrStart = content.find('[', ssKey);
            if (arrStart != std::string::npos) {
                auto arrEnd = findMatchingBracket(content, arrStart);
                if (arrEnd != std::string::npos) {
                    std::string arr = content.substr(arrStart + 1, arrEnd - arrStart - 1);
                    auto parseEasingLocal = [](const std::string& s) {
                        if (s == "linear")     return DiskEasing::Linear;
                        if (s == "quadInOut")  return DiskEasing::QuadInOut;
                        if (s == "cubicInOut") return DiskEasing::CubicInOut;
                        return DiskEasing::SineInOut;
                    };
                    auto objStr = [](const std::string& obj, const std::string& k) {
                        auto p = obj.find("\"" + k + "\"");
                        if (p == std::string::npos) return std::string();
                        p = obj.find(':', p) + 1;
                        while (p < obj.size() && (obj[p]==' '||obj[p]=='\t')) p++;
                        if (p < obj.size() && obj[p] == '"') {
                            p++;
                            auto e = obj.find('"', p);
                            if (e == std::string::npos) return std::string();
                            return obj.substr(p, e - p);
                        }
                        auto e = p;
                        while (e < obj.size() && obj[e] != ',' && obj[e] != '}') e++;
                        return obj.substr(p, e - p);
                    };
                    auto objDbl = [&](const std::string& obj, const std::string& k, double def) {
                        std::string v = objStr(obj, k);
                        if (v.empty()) return def;
                        try { return std::stod(v); } catch (...) { return def; }
                    };
                    auto objFlt = [&](const std::string& obj, const std::string& k, float def) {
                        std::string v = objStr(obj, k);
                        if (v.empty()) return def;
                        try { return std::stof(v); } catch (...) { return def; }
                    };

                    size_t p = 0;
                    while ((p = arr.find('{', p)) != std::string::npos) {
                        auto e = arr.find('}', p);
                        if (e == std::string::npos) break;
                        std::string obj = arr.substr(p, e - p + 1);
                        ScanSpeedEvent ev{};
                        ev.startTime   = objDbl(obj, "startTime", 0.0);
                        ev.duration    = objDbl(obj, "duration",  0.0);
                        ev.targetSpeed = objFlt(obj, "targetSpeed", 1.f);
                        ev.easing      = parseEasingLocal(objStr(obj, "easing"));
                        chart.scanSpeedEvents.push_back(ev);
                        p = e + 1;
                    }
                }
            }
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
        if (pos >= content.size()) return "";
        if (content[pos] == '"') {
            pos++;
            auto end = content.find('"', pos);
            if (end == std::string::npos) return "";
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
                if (p >= noteObj.size()) return std::string("");
                if (noteObj[p] == '"') {
                    p++;
                    auto e = noteObj.find('"', p);
                    if (e == std::string::npos) return std::string("");
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
