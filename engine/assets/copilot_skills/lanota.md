# Lanota (Circle) Skill

## Brief

Radial chart inspired by Lanota. Notes travel **outward** from an inner
spawn disk to an outer hit ring. Lanes wrap around the disk like clock
positions, and the whole disk can rotate / move / scale over time via
keyframed animation.

## Concepts

- **Lane / track** — discrete 0-based angular lane. `trackCount` typically
  7, range 3..36. Lane 0 is at 12 o'clock; lanes increase clockwise:
  `angle = π/2 − (lane / trackCount) * 2π`.
- **Ring** — concentric ring around the disk; ring 0 is the outer hit
  ring. Inner rings are higher-index.
- **Lane span** — a note can span 1/2/3 adjacent lanes (`laneSpan` field).
  The visual center shifts clockwise by `(span-1)/2` lanes.
- **Disk animation** — three independent keyframed channels: rotation
  (radians, absolute), move (world XY), scale (multiplier). Each keyframe
  has `startTime`, `duration`, `target`, and an `easing` enum
  (`linear`, `sineInOut`, `quadInOut`, `cubicInOut`).
- **Hold transitions** — like other drop modes, holds can cross lanes via
  a multi-waypoint path. Styles: `straight`, `angle90`, `curve`,
  `rhomboid`.

## Supported note types

| Type  | Meaning | Notes |
|-------|---------|-------|
| tap   | single tap at `(time, track, laneSpan)` | default |
| hold  | sustained press, `duration > 0` | supports multi-waypoint path |
| flick | swipe at `(time, track)` | scored like a tap |

No arcs, no arctaps, no slides in this mode.

## Constraints

- `track` must be in `[0, lane_count)`.
- `laneSpan` clamped to `[1, 3]`.
- `duration` > 0 for holds.
- Disk animation events must not have negative `duration`. `easing` must
  be one of the four enum values above.
- Disk rotation `target` is in **radians**, absolute (not delta).

## Mode-specific ops

### `add_hold_waypoint`
Append a lane-change waypoint to an existing Hold. Identify the hold by
its start time; `at_time` is the absolute time along the body; `lane` is
the target angular lane (0..trackCount-1, 0 at 12 o'clock, clockwise);
`style` is one of `"straight"`, `"angle90"`, `"curve"`, `"rhomboid"`.
```
{"op":"add_hold_waypoint", "note_time":4.0, "at_time":4.8,
 "lane":3, "style":"curve"}
```

### `remove_hold_waypoint`
Remove the waypoint of the hold at `note_time` whose absolute time
equals `at_time` (1 ms tolerance).
```
{"op":"remove_hold_waypoint", "note_time":4.0, "at_time":4.8}
```

### `set_hold_transition`
Set the transition style on every Hold in `[from, to]` — applies to the
legacy single-transition field AND every waypoint.
```
{"op":"set_hold_transition", "from":16.0, "to":32.0, "style":"rhomboid"}
```

### `add_disk_rotation`
Rotate the disk to an **absolute** `target` angle (radians) over
`duration` seconds starting at `start_time`. `easing` is one of
`"linear"`, `"sineInOut"`, `"quadInOut"`, `"cubicInOut"`.
```
{"op":"add_disk_rotation", "start_time":16.0, "duration":2.0,
 "target":3.14159, "easing":"sineInOut"}
```

### `add_disk_move`
Move the disk center to world `target` `[x, y]` over `duration`.
```
{"op":"add_disk_move", "start_time":16.0, "duration":1.5,
 "target":[0.25, -0.10], "easing":"sineInOut"}
```

### `add_disk_scale`
Scale the disk by `target` multiplier (1.0 = default) over `duration`.
```
{"op":"add_disk_scale", "start_time":16.0, "duration":1.0,
 "target":1.5, "easing":"cubicInOut"}
```

### `delete_disk_event`
Remove one disk event. `kind` is `"rotation"`, `"move"`, or `"scale"`;
matches by `start_time` (1 ms tolerance).
```
{"op":"delete_disk_event", "kind":"rotation", "start_time":16.0}
```

## Common ops

All six shared ops from `_common.md`: `delete_range`, `insert`,
`mirror_lanes`, `shift_lanes`, `shift_time`, `convert_type`.

## Examples

### Example 1 — mirror a phrase across the disk
User: *"Mirror the notes between 16 and 24s."*
```
{
  "explanation": "Mirror lanes in 16..24s across the disk axis.",
  "ops": [{"op":"mirror_lanes", "from":16.0, "to":24.0}]
}
```

### Example 2 — nudge a chorus earlier
User: *"The chorus at 40s feels 50ms late, pull it forward."*
```
{
  "explanation": "Shift all notes from 40s onward 50ms earlier.",
  "ops": [{"op":"shift_time", "from":40.0, "to":9999.0, "delta":-0.05}]
}
```

### Example 3 — rotate the disk 90 degrees
User: *"Rotate the disk 90 degrees over 2 seconds at 16s."*
```
{
  "explanation": "Rotation keyframe at 16s, duration 2s, target=pi/2 (~1.5708 rad).",
  "ops": [{"op":"add_disk_rotation", "start_time":16.0, "duration":2.0,
           "target":1.5708, "easing":"sineInOut"}]
}
```

### Example 4 — rotate lanes instead of the disk
User: *"Rotate all the notes at 32..36s one lane clockwise."*
```
{
  "explanation": "Shift lanes +1 in 32..36s (one clockwise step).",
  "ops": [{"op":"shift_lanes", "from":32.0, "to":36.0, "delta":1}]
}
```

### Example 5 — keep the arrangement but swap note types
User: *"Make all flicks between 48 and 56s into regular taps."*
```
{
  "explanation": "Convert flicks in 48..56s to taps.",
  "ops": [{"op":"convert_type", "from":48.0, "to":56.0,
           "from_type":"flick", "to_type":"tap"}]
}
```
