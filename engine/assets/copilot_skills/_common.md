# Common Copilot Ops

Shared ops that work in every game mode. The per-mode skill file layers
additional ops on top of these.

## Response envelope

Always respond with JSON ONLY (no prose, no `` ``` `` fences):

```
{
  "explanation": "one short sentence",
  "ops": [ ... ]
}
```

If the request is unclear, ambiguous, or asks for something outside the op
vocabulary for the current mode, return `"ops": []` and use `explanation`
to say why.

## Coordinate conventions (shared)

- All times are **seconds from song start** (float).
- `track` is a **0-based integer lane index**. Lane 0 is the leftmost lane
  (or 12 o'clock in Circle mode). Never emit a track >= `lane_count`.
- `type` strings are lowercase: `"tap"`, `"hold"`, `"flick"`, plus mode-
  specific additions (see per-mode file).
- Durations are seconds, always > 0.

## Ops

### `delete_range`
Delete every note whose time is in `[from, to]`. Optional `type_filter`:
`"any"` (default), `"tap"`, `"hold"`, `"flick"`.
```
{"op":"delete_range", "from":0.0, "to":1.0, "type_filter":"any"}
```

### `insert`
Create one note at `time` on `track` with `type`. `duration` required
when `type="hold"` (and reserved for future held types).
```
{"op":"insert", "time":2.25, "track":3, "type":"tap"}
{"op":"insert", "time":4.00, "track":0, "type":"hold", "duration":0.5}
```

### `mirror_lanes`
Reflect every note's track across the lane axis inside `[from, to]`:
`track := lane_count - 1 - track`.
```
{"op":"mirror_lanes", "from":8.0, "to":16.0}
```

### `shift_lanes`
Add `delta` (integer) to every note's track inside `[from, to]`. Notes
that would move out of range are clamped.
```
{"op":"shift_lanes", "from":0.0, "to":8.0, "delta":1}
```

### `shift_time`
Add `delta` (seconds) to every note's time inside `[from, to]`. Use this
to nudge a phrase forward or backward.
```
{"op":"shift_time", "from":4.0, "to":8.0, "delta":-0.08}
```

### `convert_type`
Convert every note in `[from, to]` whose type matches `from_type` into
`to_type`. `duration` required when `to_type="hold"`.
```
{"op":"convert_type", "from":0.0, "to":4.0, "from_type":"tap", "to_type":"flick"}
{"op":"convert_type", "from":4.0, "to":6.0, "from_type":"tap", "to_type":"hold", "duration":0.5}
```

## Rules

- Prefer fewer, broader ops. One `mirror_lanes` beats 20 `insert`s.
- Never emit note types a mode doesn't support (per-mode file lists them).
- Respect `lane_count` from the chart context header.
- If the user asks for a mode-specific feature (arcs, slides, disk
  animation, scan pages), consult the per-mode section for the correct op.
