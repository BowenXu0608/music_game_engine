# Unified Chart Format (UCF)

## Overview

A simple, universal JSON format for all rhythm game types. Users can create charts without learning 5 different formats.

## Format Specification

```json
{
  "version": "1.0",
  "metadata": {
    "title": "Song Name",
    "artist": "Artist Name",
    "charter": "Charter Name",
    "difficulty": "Hard",
    "level": 12
  },
  "audio": {
    "file": "song.ogg",
    "offset": 0.0,
    "previewStart": 30.0
  },
  "timing": {
    "bpm": 120.0,
    "timeSignature": "4/4"
  },
  "gameMode": "bandori",
  "notes": [
    {
      "time": 1.0,
      "type": "tap",
      "lane": 3
    },
    {
      "time": 2.0,
      "type": "hold",
      "lane": 2,
      "duration": 1.0
    },
    {
      "time": 3.5,
      "type": "flick",
      "lane": 5,
      "direction": 1
    }
  ]
}
```

## Note Types

### Common Properties
- `time` (float): Note timing in seconds
- `type` (string): Note type identifier
- `id` (optional int): Auto-generated if not provided

### Bandori/Cytus Notes
```json
{"time": 1.0, "type": "tap", "lane": 3}
{"time": 2.0, "type": "hold", "lane": 2, "duration": 1.0}
{"time": 3.0, "type": "flick", "lane": 5, "direction": 1}
```

### Arcaea Notes
```json
{"time": 1.0, "type": "tap", "lane": 2}
{"time": 2.0, "type": "arc", "startX": 0.25, "endX": 0.75,
 "startY": 0.5, "endY": 1.0, "duration": 1.0, "color": 0}
{"time": 3.0, "type": "hold", "lane": 3, "duration": 0.5}
```

### Phigros Notes
```json
{"time": 1.0, "type": "tap", "line": 0, "posOnLine": 0.5}
{"time": 2.0, "type": "hold", "line": 0, "posOnLine": 0.3, "duration": 1.0}
{"time": 3.0, "type": "flick", "line": 1, "posOnLine": -0.2}
```

With judgment lines:
```json
"judgmentLines": [
  {
    "id": 0,
    "events": [
      {"time": 0.0, "x": 0.5, "y": 0.5, "rotation": 0.0, "speed": 1.0}
    ]
  }
]
```

### Lanota Notes
```json
{"time": 1.0, "type": "tap", "ring": 0, "angle": 45.0}
{"time": 2.0, "type": "hold", "ring": 1, "angle": 90.0, "duration": 1.0}
```

## Benefits

1. **Single format to learn** - Users only need JSON
2. **Game-agnostic** - Same structure for all modes
3. **Easy to generate** - Simple scripts can create charts
4. **Human-readable** - Easy to edit manually
5. **Extensible** - Add custom properties without breaking parsers

## Migration

Old formats still supported via `ChartLoader::load()` auto-detection.
New unified format uses `.ucf.json` extension or `"version"` field detection.
