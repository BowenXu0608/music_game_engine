# Chart Parser System

## Status: ✅ IMPLEMENTED

The chart parser system supports a **unified format** plus legacy formats for compatibility.

## Unified Chart Format (Recommended)

**File Extension:** `.json` with `"version"` field
**Format:** Simple, universal JSON for all game modes

```json
{
  "version": "1.0",
  "metadata": {
    "title": "Song Name",
    "artist": "Artist Name"
  },
  "audio": {
    "file": "song.ogg",
    "offset": 0.0
  },
  "timing": {
    "bpm": 120.0
  },
  "gameMode": "bandori",
  "notes": [
    {"time": 1.0, "type": "tap", "lane": 3},
    {"time": 2.0, "type": "hold", "lane": 2, "duration": 1.0},
    {"time": 3.5, "type": "flick", "lane": 5, "direction": 1}
  ]
}
```

**Benefits:**
- Single format for all game modes
- Easy to learn and use
- Human-readable and editable
- Extensible for custom game modes

## Legacy Format Support

All original formats still work for compatibility:
- Bandori (JSON without version field)
- Arcaea (AFF)
- Cytus (XML)
- Phigros (PEC/PGR)
- Lanota (LAN)

## Usage

```cpp
#include "game/chart/ChartLoader.h"

// Auto-detects format (unified or legacy)
ChartData chart = ChartLoader::load("path/to/chart.json");
```

## Test Charts

- `test_charts/unified_demo.json` - Unified format example
- `test_charts/bandori_demo.json` - Legacy Bandori format
- `test_charts/arcaea_demo.aff` - Legacy Arcaea format
- `test_charts/cytus_demo.xml` - Legacy Cytus format
- `test_charts/lanota_demo.lan` - Legacy Lanota format
