#include "MaterialSlots.h"

namespace {

// Bandori (2D drop). Drag and Slide intentionally excluded — this mode authors
// Click + Hold + Flick only.
const std::vector<MaterialSlotInfo> kBandoriSlots = {
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

// Cytus (ScanLine). Slide Path + Scan Line are line-batch consumers (tint only).
const std::vector<MaterialSlotInfo> kCytusSlots = {
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

// Lanota (Circle). Note Fill and Note Shadow are shared between Click and
// Flick — both note types use the same curved-tile visual.
const std::vector<MaterialSlotInfo> kLanotaSlots = {
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

// Arcaea: placeholder — Phase 3.
const std::vector<MaterialSlotInfo> kArcaeaSlots = {};

} // namespace

const std::vector<MaterialSlotInfo>& getMaterialSlotsForMode(MaterialModeKey mode) {
    switch (mode) {
        case MaterialModeKey::Bandori: return kBandoriSlots;
        case MaterialModeKey::Phigros: return kPhigrosSlots;
        case MaterialModeKey::Cytus:   return kCytusSlots;
        case MaterialModeKey::Lanota:  return kLanotaSlots;
        case MaterialModeKey::Arcaea:  return kArcaeaSlots;
        default:                       return kBandoriSlots;
    }
}
