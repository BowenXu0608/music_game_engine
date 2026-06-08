#include "MaterialSlots.h"

namespace {

// Drop2D (2D drop). Drag and Slide intentionally excluded — this mode authors
// Click + Hold + Flick only.
const std::vector<MaterialSlotInfo> kDrop2DSlots = {
    {0,  "Click Note",       "",           MaterialKind::Unlit, {1.f, 0.8f, 0.2f, 1.f},   {0,0,0,0}},
    {1,  "Body",              "Hold Note", MaterialKind::Unlit, {0.2f, 0.8f, 1.f, 0.85f}, {0,0,0,0}},
    {2,  "Body (Active)",     "Hold Note", MaterialKind::Glow,  {0.3f, 0.8f, 1.f, 0.95f}, {1.f, 0.f, 0.f, 0.f}},
    {3,  "Head",              "Hold Note", MaterialKind::Unlit, {0.2f, 0.8f, 1.f, 1.f},   {0,0,0,0}},
    {4,  "Head (Active)",     "Hold Note", MaterialKind::Glow,  {0.3f, 0.8f, 1.f, 1.f},   {1.f, 0.f, 0.f, 0.f}},
    {8,  "Sample Marker",     "Hold Note", MaterialKind::Unlit, {1.f, 0.95f, 0.3f, 0.95f},{0,0,0,0}},
    {5,  "Flick Note",        "",          MaterialKind::Unlit, {1.f, 0.3f, 0.3f, 1.f},   {0,0,0,0}},
    // Track Surface is a filled quad behind everything else — supports any
    // MaterialKind (Unlit, Gradient, Scroll, Pulse, Glow).
    {12, "Track Surface",     "Playfield", MaterialKind::Unlit, {0.08f, 0.10f, 0.18f, 0.8f}, {0,0,0,0}},
    // Playfield line-batch consumers below honor tint only.
    {9,  "Lane Divider",      "Playfield", MaterialKind::Unlit, {1.f, 1.f, 1.f, 0.2f},    {0,0,0,0}},
    {10, "Hit Zone Line",     "Playfield", MaterialKind::Unlit, {1.f, 0.9f, 0.2f, 1.f},   {0,0,0,0}},
    {11, "Hit Zone Glow",     "Playfield", MaterialKind::Unlit, {1.f, 0.9f, 0.2f, 0.3f},  {0,0,0,0}},
};

// Phigros: 1-slot (judgment-line note marker).
const std::vector<MaterialSlotInfo> kPhigrosSlots = {
    {0, "Judgment Note", "", MaterialKind::Unlit, {1.f, 1.f, 1.f, 1.f}, {0,0,0,0}},
};

// ScanLine. Slide Path + Scan Line are line-batch consumers (tint only).
const std::vector<MaterialSlotInfo> kScanLineSlots = {
    {0,  "Click Note",        "",           MaterialKind::Unlit, {1.f, 1.f, 1.f, 1.f},      {0,0,0,0}},
    {1,  "Body",              "Hold Note",  MaterialKind::Unlit, {0.3f, 0.7f, 1.f, 0.45f},  {0,0,0,0}},
    {2,  "Head",              "Hold Note",  MaterialKind::Unlit, {0.3f, 0.7f, 1.f, 1.f},    {0,0,0,0}},
    {3,  "Tail Cap",          "Hold Note",  MaterialKind::Unlit, {0.3f, 0.7f, 1.f, 0.8f},   {0,0,0,0}},
    {4,  "Flick Note",        "",           MaterialKind::Unlit, {1.f, 0.75f, 0.35f, 1.f},  {0,0,0,0}},
    {5,  "Head",              "Slide Note", MaterialKind::Unlit, {0.85f, 0.5f, 1.f, 1.f},   {0,0,0,0}},
    {6,  "Node",              "Slide Note", MaterialKind::Unlit, {1.f, 1.f, 1.f, 1.f},      {0,0,0,0}},
    {7,  "Path",              "Slide Note", MaterialKind::Unlit, {0.85f, 0.5f, 1.f, 0.55f}, {0,0,0,0}},
    {8,  "Scan Line Core",    "Playfield",  MaterialKind::Unlit, {1.f, 1.f, 1.f, 0.9f},     {0,0,0,0}},
    {9,  "Scan Line Glow",    "Playfield",  MaterialKind::Unlit, {1.f, 1.f, 1.f, 0.07f},    {0,0,0,0}},
    {10, "Hit Ring",          "",           MaterialKind::Unlit, {1.f, 1.f, 1.f, 0.85f},    {0,0,0,0}},
};

// Circle. Note Fill and Note Shadow are shared between Click and
// Flick — both note types use the same curved-tile visual.
const std::vector<MaterialSlotInfo> kCircleSlots = {
    {0,  "Fill",              "Click Note", MaterialKind::Unlit, {1.f, 0.85f, 0.3f, 1.f}, {0,0,0,0}},
    {1,  "Shadow",            "Click Note", MaterialKind::Unlit, {0.f, 0.f, 0.f, 0.5f},   {0,0,0,0}},
    {5,  "Body",              "Hold Note",  MaterialKind::Unlit, {0.55f, 0.8f, 1.f, 0.85f}, {0,0,0,0}},
    {6,  "Body (Active)",     "Hold Note",  MaterialKind::Glow,  {0.4f, 0.9f, 1.f, 0.95f},  {1.f, 0.f, 0.f, 0.f}},
    {7,  "Head",              "Hold Note",  MaterialKind::Unlit, {0.8f, 0.95f, 1.f, 1.f},   {0,0,0,0}},
    {8,  "Head (Active)",     "Hold Note",  MaterialKind::Glow,  {0.4f, 0.9f, 1.f, 1.f},    {1.f, 0.f, 0.f, 0.f}},
    {9,  "Sample Marker",     "Hold Note",  MaterialKind::Unlit, {1.f, 0.95f, 0.3f, 0.95f}, {0,0,0,0}},
    {10, "Inner Spawn Disk",  "Playfield",  MaterialKind::Unlit, {0.35f, 0.55f, 0.9f, 0.6f}, {0,0,0,0}},
    {11, "Outer Hit Ring",    "Playfield",  MaterialKind::Unlit, {0.5f, 0.7f, 1.f, 0.8f},    {0,0,0,0}},
};

// Drop3D (3D drop). Notes/taps/arcs are drawn via MeshRenderer's pipeline-per-
// MaterialKind path, so all 5 kinds work on these slots. Arc "Blue"/"Red" and
// "ArcTap Tile" default to Glow so their outward normals feed rim-lighting;
// flat surfaces (ground, gate bars, shadows) default to Unlit because their
// camera-facing normals produce zero rim anyway.
const std::vector<MaterialSlotInfo> kDrop3DSlots = {
    {0,  "Click Note",    "",             MaterialKind::Unlit,    {1.f,  0.9f,  0.5f,  1.f},    {0,0,0,0}},
    {1,  "Flick Note",    "",             MaterialKind::Unlit,    {1.f,  0.35f, 0.35f, 1.f},    {0,0,0,0}},
    {2,  "Tile",          "ArcTap Note",  MaterialKind::Glow,     {1.f,  1.f,   1.f,   1.f},    {0,0,0,0}},
    {3,  "Shadow",        "ArcTap Note",  MaterialKind::Unlit,    {0.05f, 0.08f, 0.15f, 0.55f}, {0,0,0,0}},
    {4,  "Blue",          "Arc Note",     MaterialKind::Glow,     {0.4f,  0.8f,  1.f,   0.9f},  {0,0,0,0}},
    {5,  "Red",           "Arc Note",     MaterialKind::Glow,     {1.f,   0.4f,  0.7f,  0.9f},  {0,0,0,0}},
    {6,  "Blue Shadow",   "Arc Note",     MaterialKind::Unlit,    {0.08f, 0.22f, 0.35f, 0.55f}, {0,0,0,0}},
    {7,  "Red Shadow",    "Arc Note",     MaterialKind::Unlit,    {0.30f, 0.08f, 0.22f, 0.55f}, {0,0,0,0}},
    // Ground defaults to Gradient (near→far fade). tint = near colour,
    // params.rgb = far colour, params.w = 0 (vertical).
    {8,  "Ground",        "Playfield",    MaterialKind::Gradient, {0.15f, 0.15f, 0.25f, 1.f},   {0.05f, 0.05f, 0.15f, 0.f}},
    {9,  "Judgment Bar",  "Playfield",    MaterialKind::Unlit,    {1.f,   0.95f, 0.55f, 1.f},   {0,0,0,0}},
    {10, "Sky Line",      "Playfield",    MaterialKind::Unlit,    {0.55f, 0.80f, 1.f,   0.55f}, {0,0,0,0}},
    {11, "Side Posts",    "Playfield",    MaterialKind::Unlit,    {0.80f, 0.82f, 0.92f, 0.50f}, {0,0,0,0}},
};

} // namespace

const std::vector<MaterialSlotInfo>& getMaterialSlotsForMode(MaterialModeKey mode) {
    switch (mode) {
        case MaterialModeKey::Drop2D: return kDrop2DSlots;
        case MaterialModeKey::Phigros: return kPhigrosSlots;
        case MaterialModeKey::ScanLine:   return kScanLineSlots;
        case MaterialModeKey::Circle:  return kCircleSlots;
        case MaterialModeKey::Drop3D:  return kDrop3DSlots;
        default:                       return kDrop2DSlots;
    }
}

namespace {
std::string slugify(const char* s) {
    std::string out;
    bool lastUnd = true;   // suppress leading underscore
    for (; s && *s; ++s) {
        char c = *s;
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
        if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) {
            out += c;
            lastUnd = false;
        } else if (!lastUnd) {
            out += '_';
            lastUnd = true;
        }
    }
    while (!out.empty() && out.back() == '_') out.pop_back();
    return out;
}
} // namespace

std::string materialSlotSlug(const MaterialSlotInfo& slot) {
    std::string name = slugify(slot.displayName);
    if (slot.group && slot.group[0]) {
        std::string g = slugify(slot.group);
        if (!g.empty()) return g + "_" + name;
    }
    return name;
}

const char* materialModeName(MaterialModeKey mode) {
    switch (mode) {
        case MaterialModeKey::Drop2D: return "drop2d";
        case MaterialModeKey::Phigros: return "phigros";
        case MaterialModeKey::ScanLine:   return "scanline";
        case MaterialModeKey::Circle:  return "circle";
        case MaterialModeKey::Drop3D:  return "drop3d";
        default:                       return "unknown";
    }
}

std::string normalizeModeToken(const std::string& token) {
    if (token == "bandori") return "drop2d";
    if (token == "arcaea")  return "drop3d";
    if (token == "cytus")   return "scanline";
    if (token == "lanota")  return "circle";
    return token;
}

MaterialModeKey detectChartMode(const std::string& stem) {
    // Engine convention: "<song>_<modeKey>_<difficulty>".
    if (stem.find("_drop3d_")  != std::string::npos) return MaterialModeKey::Drop3D;
    if (stem.find("_drop2d_")  != std::string::npos) return MaterialModeKey::Drop2D;
    if (stem.find("_circle_")  != std::string::npos) return MaterialModeKey::Circle;
    if (stem.find("_scan_")    != std::string::npos) return MaterialModeKey::ScanLine;
    if (stem.find("_phigros_") != std::string::npos) return MaterialModeKey::Phigros;
    return MaterialModeKey::Drop2D;
}
