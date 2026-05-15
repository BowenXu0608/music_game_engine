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

### `set_material`
Set the **PBR material** of one note slot for the current mode. Works in
every mode. `slot` is the slot's name (see the per-mode "Material slots"
list); matching is case/space-insensitive so `"Hold Body"`, `"hold_body"`
all work. Every property is **optional** — emit only the ones the user
asked to change; omitted fields keep their current value.

- `base_color`: `[r,g,b,a]` floats 0..1 (a optional, defaults 1).
- `metallic`: 0..1 (0 = dielectric/plastic, 1 = metal).
- `roughness`: 0..1 (0 = mirror-sharp highlight, 1 = matte).
- `emissive_color`: `[r,g,b]` 0..1 and `emissive_intensity`: float ≥ 0
  (self-illumination / glow; needs intensity > 0 to show).
- `base_color_texture` / `normal_texture`: project-relative image paths
  (only if the user names a texture file). Setting `normal_texture` turns
  on normal mapping.

```
{"op":"set_material", "slot":"Tap Note", "base_color":[1.0,0.84,0.0,1.0], "metallic":1.0, "roughness":0.25}
{"op":"set_material", "slot":"Hold Body", "emissive_color":[0.2,0.8,1.0], "emissive_intensity":2.0}
```

Notes/colour requests like "make the tap notes shiny gold" → one
`set_material` with `metallic≈1`, low `roughness`, gold `base_color`.
"Make hold notes glow cyan" → `emissive_color` + `emissive_intensity`.
Material changes take effect the next time the chart is previewed/played.

## Rules

- Prefer fewer, broader ops. One `mirror_lanes` beats 20 `insert`s.
- Never emit note types a mode doesn't support (per-mode file lists them).
- Respect `lane_count` from the chart context header.
- If the user asks for a mode-specific feature (arcs, slides, disk
  animation, scan pages), consult the per-mode section for the correct op.
