// Song Editor — parameterized by `mode`. Source of truth: uploads/ui_design.md §5.

// ───────── Per-mode config ─────────
const MODE_LABELS = {
  drop2d:   'Drop 2D',
  drop3d:   'Drop 3D',
  circle:   'Circle',
  scanline: 'Scan Line',
};

const MODE_CFG = {
  drop2d: {
    label: 'Drop 2D',
    accent: 'cyan',
    // §5.1 Bandori — DropNotes 2D
    trackRange: [3, 12], trackDefault: 7,
    noteTypes: ['Click', 'Hold', 'Flick'], // NO Slide, NO Arc, NO ArcTap
    materialSlots: ['Tap Note', 'Hold Note (Body)', 'Hold Note (Head)', 'Hold Note (Tail)', 'Flick Note', 'Lane Divider', 'Judgment Bar'],
    sliders: [
      { label: 'Tracks', value: 7, min: 3, max: 12, accent: 'cyan' },
      { label: 'Default note width', value: 1, min: 1, max: 4, accent: 'cyan' },
    ],
    sceneExtras: null, // no curve editor / disk fx / scan paging
    aiTuning: ['autoFlickPct', 'autoHoldMin', 'autoAntiJack', 'laneCooldownMs', 'thinNps'],
    noteSpeedScales: 'SCROLL_SPEED',
    badge: { tag: 'HARD 9.7', color: 'magenta' },
  },
  drop3d: {
    label: 'Drop 3D',
    accent: 'magenta',
    // §5.2 Arcaea — DropNotes 3D
    trackRange: [3, 12], trackDefault: 4,
    noteTypes: ['Click', 'Hold', 'Flick', 'Arc', 'ArcTap'],
    materialSlots: ['Tap Note', 'Hold Note', 'Arc', 'ArcTap', 'Sky Bar', 'Judgment Bar'],
    sliders: [
      { label: 'Tracks', value: 4, min: 3, max: 12, accent: 'magenta' },
      { label: 'Sky height', value: 1.0, min: -1, max: 3, accent: 'magenta' },
    ],
    sceneExtras: 'arcCurve', // 120px Arc Height Curve editor
    aiTuning: ['autoFlickPct', 'autoHoldMin', 'autoAntiJack', 'laneCooldownMs', 'thinNps'],
    noteSpeedScales: 'SCROLL_SPEED',
    badge: { tag: 'PRES 10.2', color: 'magenta' },
  },
  circle: {
    label: 'Circle',
    accent: 'lime',
    // §5.3 Lanota — Circle
    trackRange: [3, 36], trackDefault: 12,
    noteTypes: ['Click', 'Hold', 'Flick'],
    materialSlots: ['Tap', 'Hold', 'Flick', 'Disk Surface', 'Ring', 'Lane Divider', 'Judgment Bar'],
    sliders: [
      { label: 'Tracks', value: 12, min: 3, max: 36, accent: 'lime' },
      { label: 'Disk inner radius', value: 0.9, min: 0.2, max: 3.0, accent: 'lime' },
      { label: 'Disk base radius',  value: 2.4, min: 1.0, max: 6.0, accent: 'lime' },
      { label: 'Disk ring spacing', value: 0.6, min: 0.1, max: 1.5, accent: 'lime' },
      { label: 'Disk initial scale', value: 1.0, min: 0.3, max: 5.0, accent: 'lime' },
    ],
    sceneExtras: 'diskFx', // 34px Disk FX keyframe strip
    aiTuning: ['autoFlickPct', 'autoHoldMin', 'autoAntiJack', 'laneCooldownMs', 'thinNps'],
    noteSpeedScales: 'APPROACH_SECS',
    badge: { tag: 'CHAOS 8.5', color: 'lime' },
    extras: ['Reset disk defaults'],
  },
  scanline: {
    label: 'Scan Line',
    accent: 'amber',
    // §5.4 Cytus — Scan Line
    trackRange: null, // lane-less
    trackDefault: null,
    noteTypes: ['Click', 'Hold', 'Flick', 'Slide'], // ONLY mode with Slide
    materialSlots: ['Scan Line', 'Tap', 'Hold', 'Flick', 'Slide', 'Track', 'Judgment Bar'],
    sliders: [
      { label: 'Page duration', value: 1.62, min: 0.2, max: 5.0, suffix: 's', accent: 'amber', hint: '240/BPM s default' },
    ],
    sceneExtras: 'scanPages', // paginated authoring
    aiTuning: ['autoFlickPct', 'autoHoldMin', 'autoAntiJack', 'scanTimeGapMs', 'thinNps'],
    noteSpeedScales: null, // ignored
    badge: { tag: 'CHAOS 9.1', color: 'amber' },
  },
};

function SongEditor({ density = 'mid', mode = 'drop2d', tool = 'tap', sidebarLeft = true, accent = 'cyan', projectName = 'BandoriSandbox', songName = 'Forest Place' }) {
  const cfg = MODE_CFG[mode] || MODE_CFG.drop2d;
  return (
    <Surface>
      {/* Top toolbar */}
      <div style={{
        height: 44, padding: '0 12px',
        display: 'flex', alignItems: 'center', gap: 10,
        background: 'var(--bg-panel)', borderBottom: '1px solid var(--border)',
      }}>
        <div style={{
          width: 22, height: 22, borderRadius: 5,
          background: 'linear-gradient(135deg, var(--cyan), var(--magenta))',
          display: 'flex', alignItems: 'center', justifyContent: 'center',
          boxShadow: '0 0 10px rgba(34,230,255,0.4)',
        }}>
          <span style={{ color: '#000', fontSize: 10, fontWeight: 800 }}>M</span>
        </div>
        <div style={{ display: 'flex', alignItems: 'center', gap: 6 }}>
          <span style={{ fontSize: 11, color: 'var(--text-mid)' }}>{projectName}</span>
          <Icon d={I.chevR} size={11} stroke="var(--text-low)" />
          <span style={{ fontSize: 11, color: 'var(--text-mid)' }}>{songName}</span>
          <Icon d={I.chevR} size={11} stroke="var(--text-low)" />
          <Pill color={cfg.badge.color} small>{cfg.badge.tag}</Pill>
          <Pill color={cfg.accent} small>{cfg.label}</Pill>
        </div>

        <div style={{ flex: 1 }}></div>

        <div style={{ display: 'flex', alignItems: 'center', gap: 6, padding: '0 8px', borderRadius: 5, background: 'var(--bg-panel-2)', border: '1px solid var(--border)', height: 28 }}>
          <div style={{ width: 6, height: 6, borderRadius: 3, background: 'var(--lime)', boxShadow: '0 0 6px var(--lime)' }}></div>
          <span className="mono" style={{ fontSize: 10, color: 'var(--text-mid)' }}>auto-save · 12s ago</span>
        </div>
        <Btn kind="outline" size="sm" icon={I.download}>Save</Btn>
        <Btn kind="success" size="sm" icon={I.play} glow>Test Game</Btn>
      </div>

      <div style={{ flex: 1, display: 'flex', minHeight: 0, flexDirection: sidebarLeft ? 'row' : 'row-reverse' }}>
        {/* LEFT SIDEBAR — Basic / Note / Material tabs */}
        <div style={{ width: 240, borderRight: sidebarLeft ? '1px solid var(--border)' : 'none', borderLeft: sidebarLeft ? 'none' : '1px solid var(--border)', display: 'flex', flexDirection: 'column', background: 'var(--bg-base)' }}>
          <Tabs items={[
            { id: 'b', label: 'Basic' },
            { id: 'n', label: 'Note' },
            { id: 'm', label: 'Material' },
          ]} value="b" onChange={()=>{}} />
          <div style={{ padding: 6, borderBottom: '1px solid var(--divider)', display: 'flex', alignItems: 'center', gap: 6 }}>
            <Icon d={I.search} size={11} stroke="var(--text-low)" />
            <span style={{ fontSize: 11, color: 'var(--text-low)', flex: 1 }}>Search…</span>
          </div>
          <div style={{ flex: 1, overflow: 'auto', padding: 4 }}>
            {/* Game Mode */}
            <SectionHeader label="Game mode" />
            <div style={{ padding: '0 10px 10px', display: 'flex', flexDirection: 'column', gap: 3 }}>
              {Object.keys(MODE_LABELS).map(k => (
                <Btn key={k} kind="ghost" size="sm" active={k === mode}>{MODE_LABELS[k]}</Btn>
              ))}
            </div>

            {/* Mode-specific sliders */}
            <SectionHeader label={cfg.label + ' settings'} />
            <div style={{ padding: '0 10px 10px', display: 'flex', flexDirection: 'column', gap: 8 }}>
              {cfg.sliders.map((s, i) => (
                <Slider key={i} label={s.label} value={s.value} min={s.min} max={s.max} suffix={s.suffix} accent={s.accent} />
              ))}
              {cfg.extras && cfg.extras.map((e, i) => (
                <Btn key={i} kind="outline" size="sm">{e}</Btn>
              ))}
              {mode === 'scanline' && (
                <div style={{ fontSize: 10, color: 'var(--text-low)', padding: '4px 0', lineHeight: 1.4 }}>
                  Lane-less · sweep-driven · note speed ignored
                </div>
              )}
            </div>

            {/* Audio + Preview Clip */}
            <SectionHeader label="Audio" />
            <div style={{ padding: '0 10px 10px', display: 'flex', flexDirection: 'column', gap: 6, fontSize: 11 }}>
              <div style={{ display: 'flex', justifyContent: 'space-between' }}><span style={{ color: 'var(--text-low)' }}>File</span><span style={{ color: 'var(--text-hi)' }}>forest_place.ogg</span></div>
              <div style={{ display: 'flex', justifyContent: 'space-between' }}><span style={{ color: 'var(--text-low)' }}>BPM</span><span className="mono" style={{ color: 'var(--' + cfg.accent + ')' }}>148.0</span></div>
              <div style={{ display: 'flex', justifyContent: 'space-between' }}><span style={{ color: 'var(--text-low)' }}>Duration</span><span className="mono" style={{ color: 'var(--text-hi)' }}>3:12.448</span></div>
              <Slider label="Audio offset" value={0} min={-200} max={200} suffix="ms" accent={cfg.accent} />
            </div>

            {/* Judgment + Scoring */}
            <SectionHeader label="Judgment + scoring" />
            <div style={{ padding: '0 10px 10px', display: 'flex', flexDirection: 'column', gap: 4, fontSize: 11 }}>
              {[
                { l: 'Perfect', v: '50ms', s: '1000', c: 'lime' },
                { l: 'Good',    v: '100ms', s: '500', c: 'cyan' },
                { l: 'Bad',     v: '150ms', s: '0', c: 'amber' },
              ].map((j, i) => (
                <div key={i} style={{ display: 'flex', alignItems: 'center', gap: 6, padding: '4px 6px', borderRadius: 3, background: 'var(--bg-panel-2)' }}>
                  <div style={{ width: 6, height: 6, borderRadius: 3, background: `var(--${j.c})`, boxShadow: `0 0 6px var(--${j.c})` }}></div>
                  <span style={{ flex: 1, color: 'var(--text-mid)' }}>{j.l}</span>
                  <span className="mono" style={{ color: 'var(--text-hi)', fontSize: 10 }}>{j.v}</span>
                  <span className="mono" style={{ color: 'var(--text-low)', fontSize: 10 }}>{j.s}</span>
                </div>
              ))}
              <div style={{ display: 'flex', justifyContent: 'space-between', padding: '4px 6px', fontSize: 10, color: 'var(--text-low)' }}>
                <span>Total score</span><span className="mono" style={{ color: 'var(--text-hi)' }}>auto · 1,000,000</span>
              </div>
            </div>

            {/* HUD */}
            <SectionHeader label="HUD" />
            <div style={{ padding: '0 10px 10px', display: 'flex', flexDirection: 'column', gap: 4, fontSize: 11 }}>
              <div style={{ color: 'var(--text-low)', fontSize: 10, letterSpacing: '0.05em' }}>Score · Combo</div>
              <div style={{ display: 'flex', gap: 4 }}>
                <Field label="Pos" value="top·right" mono />
                <Field label="Size" value="32" mono />
              </div>
            </div>

            {/* Camera */}
            <SectionHeader label="Camera" />
            <div style={{ padding: '0 10px 10px' }}>
              <div style={{ display: 'flex', gap: 4, marginBottom: 6 }}>
                <Field label="X" value="0.0" mono />
                <Field label="Y" value="12.0" mono />
                <Field label="Z" value="14.0" mono />
              </div>
              <Slider label="FOV" value={55} min={20} max={120} suffix="°" accent={cfg.accent} />
            </div>

            {/* Background */}
            <SectionHeader label="Background" />
            <div style={{ padding: '0 10px 10px' }}>
              <Placeholder label="bg.png" h={44} color={cfg.accent} />
            </div>

            {/* AI gear */}
            <SectionHeader label="AI" right={<IconBtn icon={I.settings} size={18} />} />
            <div style={{ padding: '0 10px 10px', fontSize: 10, color: 'var(--text-low)' }}>
              Tuning · {cfg.aiTuning.length} fields
            </div>
          </div>
        </div>

        {/* CENTER — toolbar + scene + (optional curve/disk strip) + timeline + waveform */}
        <div style={{ flex: 1, display: 'flex', flexDirection: 'column', minWidth: 0, background: 'var(--bg-void)' }}>
          {/* Scan Line: Pages strip sits at the very top (paginated authoring) */}
          {cfg.sceneExtras === 'scanPages' && <ScanPagesStrip />}

          {/* Circle: Disk FX strip sits ABOVE the scene (per §4.4) */}
          {cfg.sceneExtras === 'diskFx' && <DiskFxStrip />}

          {/* Scene preview — mode-specific */}
          <div style={{ flex: '1.4', minHeight: 0, position: 'relative', borderBottom: '1px solid var(--border)', background: '#000', overflow: 'hidden' }}>
            <div style={{ position: 'absolute', top: 10, right: 10, zIndex: 5, display: 'flex', gap: 4, padding: 4, background: 'rgba(13,13,16,0.85)', backdropFilter: 'blur(10px)', borderRadius: 6, border: '1px solid var(--border-hi)' }}>
              <IconBtn icon={I.refresh} title="Reset view" />
            </div>
            {mode === 'drop2d'   && <Drop2DScene />}
            {mode === 'drop3d'   && <Drop3DScene />}
            {mode === 'circle'   && <CircleScene />}
            {mode === 'scanline' && <ScanLineScene />}
            <div style={{ position: 'absolute', bottom: 10, left: '50%', transform: 'translateX(-50%)', display: 'flex', gap: 6, alignItems: 'center', padding: '4px 10px', borderRadius: 4, background: 'rgba(0,0,0,0.6)', border: '1px solid var(--border)' }}>
              <Pill color={cfg.accent} small>{cfg.label}</Pill>
              <span style={{ fontSize: 10, color: 'var(--text-low)' }}>{cfg.noteTypes.join(' · ')}</span>
            </div>
          </div>

          {/* Note toolbar — sits BELOW the scene, ABOVE the timeline (per real-app screenshot) */}
          <NoteToolbar mode={mode} cfg={cfg} tool={tool} />

          {/* Chart timeline — hidden for Scan Line (paginated authoring; the scene IS the chart, per §sys7) */}
          {mode !== 'scanline' && (
            <div style={{ flex: '1', minHeight: 0, display: 'flex', flexDirection: 'column', borderBottom: '1px solid var(--border)' }}>
              <ChartTimeline mode={mode} cfg={cfg} />
            </div>
          )}

          {/* Drop 3D: Arc Height Curve strip sits BELOW the timeline (per §4.4 "above the timeline" → editor area, real-app screenshot shows it below the lane bands) */}
          {cfg.sceneExtras === 'arcCurve' && <ArcCurveStrip />}

          {/* Waveform */}
          <div style={{ height: 100, borderBottom: '1px solid var(--border)', background: 'var(--bg-panel)' }}>
            <Waveform accent={cfg.accent} />
          </div>

          {/* Transport */}
          <div style={{ height: 44, padding: '0 14px', display: 'flex', alignItems: 'center', gap: 12, background: 'var(--bg-panel)' }}>
            <div style={{ display: 'flex', gap: 4 }}>
              <IconBtn icon={I.prev} size={32} />
              <IconBtn icon={I.play} size={32} active glow color="var(--lime)" />
              <IconBtn icon={I.pause} size={32} />
              <IconBtn icon={I.stop} size={32} />
              <IconBtn icon={I.next} size={32} />
            </div>
            <div className="mono" style={{ fontSize: 16, fontWeight: 600, color: `var(--${cfg.accent})`, textShadow: `0 0 10px var(--${cfg.accent})` }}>01:42.318</div>
            <span className="mono" style={{ fontSize: 11, color: 'var(--text-low)' }}>/ 03:12.448</span>
            <div style={{ flex: 1 }}></div>
            <Btn kind="ghost" size="sm" icon={I.diamond}>Snap 1/16</Btn>
            <Btn kind="ghost" size="sm" iconRight={I.chevD}>BPM 148</Btn>
          </div>
        </div>

        {/* RIGHT — Copilot / Audit tabs (per §4.4) */}
        <div style={{ width: 280, borderLeft: sidebarLeft ? '1px solid var(--border)' : 'none', borderRight: sidebarLeft ? 'none' : '1px solid var(--border)', display: 'flex', flexDirection: 'column', background: 'var(--bg-base)' }}>
          <Tabs items={[
            { id: 'c', label: 'Copilot' },
            { id: 'a', label: 'Audit' },
          ]} value="c" onChange={()=>{}} />

          {/* Copilot conversation */}
          <div style={{ flex: 1, overflow: 'auto', padding: 10, display: 'flex', flexDirection: 'column', gap: 10, minHeight: 0 }}>
            <div style={{ alignSelf: 'flex-start', maxWidth: '92%' }}>
              <div style={{ display: 'flex', alignItems: 'center', gap: 6, marginBottom: 4 }}>
                <div style={{ width: 16, height: 16, borderRadius: 4, background: `linear-gradient(135deg, var(--${cfg.accent}), var(--magenta))` }}></div>
                <span style={{ fontSize: 9, color: `var(--${cfg.accent})`, fontWeight: 600, letterSpacing: '0.05em' }}>COPILOT</span>
              </div>
              <div style={{ padding: 8, borderRadius: 5, background: 'var(--bg-panel-2)', border: '1px solid var(--border)', fontSize: 11, color: 'var(--text-mid)', lineHeight: 1.4 }}>
                Describe a chart edit in plain language — e.g. "mirror lanes between 1:24 and 1:48", "convert chorus flicks to taps". I'll preview the change, you Apply or Undo.
              </div>
            </div>
            <div style={{ alignSelf: 'flex-end', maxWidth: '88%', padding: 8, borderRadius: 5, background: `color-mix(in oklch, var(--${cfg.accent}) 12%, transparent)`, border: `1px solid color-mix(in oklch, var(--${cfg.accent}) 35%, transparent)`, fontSize: 11, color: 'var(--text-hi)' }}>
              Mirror lanes between 1:24 and 1:48.
            </div>
            <div style={{ alignSelf: 'flex-start', maxWidth: '92%' }}>
              <div style={{ padding: 8, borderRadius: 5, background: 'var(--bg-panel-2)', border: '1px solid var(--border)', fontSize: 11, color: 'var(--text-mid)', lineHeight: 1.4 }}>
                Will mirror <span style={{ color: `var(--${cfg.accent})` }}>47 notes</span> across 24s. Preview shown on timeline.
                <div style={{ marginTop: 8, display: 'flex', gap: 4 }}>
                  <Btn kind="primary" size="sm" icon={I.check} glow>Apply</Btn>
                  <Btn kind="ghost" size="sm" icon={I.undo}>Undo</Btn>
                  <IconBtn icon={I.settings} size={22} title="Tuning" />
                </div>
              </div>
            </div>
          </div>

          {/* Composer */}
          <div style={{ padding: 8, borderTop: '1px solid var(--divider)' }}>
            <div style={{ background: 'var(--bg-panel-2)', border: '1px solid var(--border-hi)', borderRadius: 5, padding: 8, display: 'flex', alignItems: 'center', gap: 6 }}>
              <span style={{ flex: 1, fontSize: 11, color: 'var(--text-low)' }}>Ask Copilot to edit this chart…</span>
              <IconBtn icon={I.send} size={22} color={`var(--${cfg.accent})`} glow />
            </div>
          </div>
        </div>
      </div>
    </Surface>
  );
}

// ───────── Note toolbar (mode-filtered) ─────────
function NoteToolbar({ mode, cfg, tool }) {
  // Order from §4.4: Marker · <note types> · Analyze · Clr Mrk · Thin · Undo · Place · AI · Clr Note · Audit
  const noteIcons = { Click: I.tap, Hold: I.hold, Flick: I.flick, Slide: I.slide, Arc: I.arc, ArcTap: I.diamond };
  const noteColors = { Click: 'cyan', Hold: 'lime', Flick: 'magenta', Slide: 'amber', Arc: 'violet', ArcTap: 'cyan' };
  const accent = cfg.accent;
  return (
    <div style={{ height: 38, padding: '0 10px', display: 'flex', alignItems: 'center', gap: 4, background: 'var(--bg-panel)', borderBottom: '1px solid var(--border)' }}>
      <Btn kind="ghost" size="sm" icon={I.flag}>Marker</Btn>
      <div style={{ width: 1, height: 18, background: 'var(--border)', margin: '0 4px' }}></div>
      {cfg.noteTypes.map((t, i) => {
        const isActive = t.toLowerCase() === tool;
        const c = noteColors[t];
        return (
          <div key={t} style={{
            display: 'flex', alignItems: 'center', gap: 6,
            height: 26, padding: '0 10px', borderRadius: 4,
            fontSize: 11, fontWeight: 600, letterSpacing: '0.02em',
            cursor: 'pointer', userSelect: 'none',
            background: isActive ? `color-mix(in oklch, var(--${c}) 18%, transparent)` : 'transparent',
            color: isActive ? `var(--${c})` : 'var(--text-mid)',
            border: '1px solid ' + (isActive ? `var(--${c})` : 'var(--border)'),
            boxShadow: isActive ? `0 0 10px color-mix(in oklch, var(--${c}) 50%, transparent)` : 'none',
          }}>
            <span style={{
              width: 14, height: 14, borderRadius: 3,
              background: isActive ? `var(--${c})` : `color-mix(in oklch, var(--${c}) 22%, transparent)`,
              border: '1px solid ' + (isActive ? `var(--${c})` : `color-mix(in oklch, var(--${c}) 50%, transparent)`),
              boxShadow: isActive ? `0 0 6px var(--${c})` : 'none',
            }}></span>
            <span>{t}</span>
            <span className="mono" style={{
              fontSize: 9, opacity: 0.6, padding: '0 3px',
              border: '1px solid currentColor', borderRadius: 2,
            }}>{i + 1}</span>
          </div>
        );
      })}
      <div style={{ width: 1, height: 18, background: 'var(--border)', margin: '0 4px' }}></div>
      <Btn kind="ghost" size="sm">Analyze</Btn>
      <Btn kind="ghost" size="sm">Clr Mrk</Btn>
      <Btn kind="ghost" size="sm">Thin</Btn>
      <Btn kind="ghost" size="sm" icon={I.undo}>Undo</Btn>
      <Btn kind="ghost" size="sm">Place</Btn>
      <Btn kind="ghost" size="sm" icon={I.sparkle}>AI</Btn>
      <Btn kind="ghost" size="sm">Clr Note</Btn>
      <div style={{ flex: 1 }}></div>
      <Btn kind="ghost" size="sm" icon={I.flag}>Audit</Btn>
    </div>
  );
}

// ───────── Drop 2D scene ─────────
function Drop2DScene() {
  const lanes = 7;
  const notes = [
    { lane: 2, t: 0.15, type: 'tap', c: 'cyan' },
    { lane: 4, t: 0.18, type: 'tap', c: 'cyan' },
    { lane: 0, t: 0.32, type: 'flick', c: 'magenta' },
    { lane: 6, t: 0.32, type: 'flick', c: 'magenta' },
    { lane: 3, t: 0.48, type: 'tap', c: 'cyan' },
    { lane: 1, t: 0.62, type: 'hold', c: 'lime', extent: 0.18 },
    { lane: 5, t: 0.62, type: 'hold', c: 'lime', extent: 0.18 },
    { lane: 3, t: 0.78, type: 'tap', c: 'cyan' },
    { lane: 2, t: 0.92, type: 'tap', c: 'cyan', selected: true },
    { lane: 4, t: 0.92, type: 'tap', c: 'cyan' },
  ];
  return (
    <div style={{ position: 'absolute', inset: 0,
      background: 'radial-gradient(ellipse at 50% 100%, #0d1a2e 0%, #050208 60%, #000 100%)' }}>
      <svg viewBox="0 0 800 500" preserveAspectRatio="none" style={{ position: 'absolute', inset: 0, width: '100%', height: '100%' }}>
        <defs>
          <linearGradient id="d2track" x1="0" y1="0" x2="0" y2="1">
            <stop offset="0%" stopColor="rgba(34,230,255,0)" />
            <stop offset="100%" stopColor="rgba(34,230,255,0.1)" />
          </linearGradient>
        </defs>
        {/* Drop 2D = vertical lanes, NOT perspective trapezoid */}
        <rect x="200" y="40" width="400" height="440" fill="url(#d2track)" stroke="rgba(34,230,255,0.2)" />
        {Array.from({length: lanes + 1}).map((_, i) => {
          const x = 200 + (i / lanes) * 400;
          const isEdge = i === 0 || i === lanes;
          return <line key={i} x1={x} y1={40} x2={x} y2={480}
            stroke={isEdge ? 'rgba(34,230,255,0.6)' : 'rgba(34,230,255,0.18)'}
            strokeWidth={isEdge ? 1.5 : 1} />
        })}
        {[0.2, 0.4, 0.6, 0.8].map((t, i) => (
          <line key={i} x1="200" y1={40 + t * 440} x2="600" y2={40 + t * 440}
            stroke="rgba(255,255,255,0.06)" strokeDasharray="3 4" />
        ))}
        {/* Judgment line at bottom */}
        <line x1="200" y1="470" x2="600" y2="470" stroke="var(--cyan)" strokeWidth="3" />
        <line x1="200" y1="470" x2="600" y2="470" stroke="var(--cyan)" strokeWidth="8" opacity="0.3" filter="blur(4px)" />
        {/* Notes */}
        {notes.map((n, i) => {
          const colors = { cyan: '#22e6ff', magenta: '#ff3df0', lime: '#7dff5a' };
          const col = colors[n.c];
          const x = 200 + (n.lane + 0.5) / lanes * 400;
          const y = 40 + (1 - n.t) * 440;
          const w = 44, h = 10;
          if (n.type === 'hold') {
            const y2 = 40 + (1 - (n.t - n.extent)) * 440;
            return (
              <g key={i}>
                <rect x={x - w/2} y={y2} width={w} height={y - y2} fill={col} opacity="0.35" />
                <rect x={x - w/2} y={y2} width={w} height={y - y2} fill="none" stroke={col} strokeWidth="2" />
                <rect x={x - w/2} y={y - h/2} width={w} height={h} fill={col} rx={h/2} />
              </g>
            );
          }
          return (
            <g key={i}>
              <rect x={x - w/2 - 2} y={y - h/2 - 2} width={w + 4} height={h + 4}
                fill="none" stroke={col} strokeOpacity="0.4" rx={(h+4)/2} style={{ filter: 'blur(3px)' }} />
              <rect x={x - w/2} y={y - h/2} width={w} height={h} fill={col} rx={h/2} />
              {n.type === 'flick' && (
                <polygon points={`${x},${y - h*1.5} ${x - h*0.7},${y - h/2} ${x + h*0.7},${y - h/2}`} fill={col} />
              )}
              {n.selected && (
                <rect x={x - w/2 - 4} y={y - h/2 - 4} width={w + 8} height={h + 8}
                  fill="none" stroke="#fff" strokeWidth="1" strokeDasharray="3 2" rx={(h+8)/2} />
              )}
            </g>
          );
        })}
      </svg>
    </div>
  );
}

// ───────── Drop 3D scene (Arcaea-style perspective + arc) ─────────
function Drop3DScene() {
  const lanes = 4;
  return (
    <div style={{ position: 'absolute', inset: 0,
      background: 'radial-gradient(ellipse at 50% 100%, #2e0d1a 0%, #050208 60%, #000 100%)',
      perspective: 1000 }}>
      <div style={{ position: 'absolute', top: '15%', left: '50%', transform: 'translateX(-50%)', width: 400, height: 400, borderRadius: '50%', background: 'radial-gradient(circle, rgba(255,61,240,0.15), transparent 60%)', filter: 'blur(40px)' }}></div>
      <svg viewBox="0 0 800 500" preserveAspectRatio="none" style={{ position: 'absolute', inset: 0, width: '100%', height: '100%' }}>
        {/* Perspective trapezoid */}
        <polygon points="320,80 480,80 700,500 100,500" fill="rgba(255,61,240,0.05)" stroke="rgba(255,61,240,0.2)" strokeWidth="1" />
        {Array.from({length: lanes + 1}).map((_, i) => {
          const f = i / lanes;
          const xTop = 320 + f * 160, xBot = 100 + f * 600;
          const isEdge = i === 0 || i === lanes;
          return <line key={i} x1={xTop} y1={80} x2={xBot} y2={500}
            stroke={isEdge ? 'rgba(255,61,240,0.6)' : 'rgba(255,61,240,0.18)'}
            strokeWidth={isEdge ? 1.5 : 1} />
        })}
        {/* Sky bar */}
        <line x1="280" y1="170" x2="520" y2="170" stroke="rgba(160,112,255,0.5)" strokeWidth="2" strokeDasharray="6 3" />
        <text x="540" y="174" fill="rgba(160,112,255,0.7)" fontSize="10" fontFamily="JetBrains Mono">sky h=1.0</text>
        {/* Judgment line */}
        <line x1="100" y1="490" x2="700" y2="490" stroke="var(--magenta)" strokeWidth="3" />
        <line x1="100" y1="490" x2="700" y2="490" stroke="var(--magenta)" strokeWidth="8" opacity="0.3" filter="blur(4px)" />
        {/* Floor notes */}
        {[
          { lane: 1, t: 0.25 }, { lane: 2, t: 0.40 }, { lane: 0, t: 0.55 }, { lane: 3, t: 0.55 },
          { lane: 1, t: 0.70 }, { lane: 2, t: 0.70, sel: true }, { lane: 0, t: 0.85 },
        ].map((n, i) => {
          const tt = 1 - n.t;
          const xT = 320 + (n.lane + 0.5) / lanes * 160, xB = 100 + (n.lane + 0.5) / lanes * 600;
          const x = xT + (xB - xT) * tt, y = 80 + tt * 420, w = 30 + tt * 60, h = 8 + tt * 8;
          return <g key={i}>
            <rect x={x - w/2} y={y - h/2} width={w} height={h} fill="#ff3df0" rx={h/2} />
            {n.sel && <rect x={x-w/2-3} y={y-h/2-3} width={w+6} height={h+6} fill="none" stroke="#fff" strokeDasharray="3 2" rx={(h+6)/2} />}
          </g>;
        })}
        {/* Arc — cyan, with waypoints */}
        <path d="M 250 180 Q 350 220, 380 280 T 500 400" fill="none" stroke="#22e6ff" strokeWidth="6" opacity="0.85" />
        <path d="M 250 180 Q 350 220, 380 280 T 500 400" fill="none" stroke="#22e6ff" strokeWidth="14" opacity="0.25" filter="blur(4px)" />
        {[
          { x: 250, y: 180 }, { x: 380, y: 280 }, { x: 500, y: 400 },
        ].map((wp, i) => (
          <g key={i}>
            <circle cx={wp.x} cy={wp.y} r="6" fill="#22e6ff" stroke="#fff" strokeWidth="1.5" />
          </g>
        ))}
        {/* Arc — pink */}
        <path d="M 540 200 Q 480 290, 360 380" fill="none" stroke="#ff7ae0" strokeWidth="6" opacity="0.85" />
        {/* ArcTap diamonds on sky */}
        {[{ x: 300, y: 168 }, { x: 460, y: 168 }].map((p, i) => (
          <g key={i} transform={`translate(${p.x} ${p.y}) rotate(45)`}>
            <rect x={-6} y={-6} width={12} height={12} fill="#fff" stroke="#22e6ff" strokeWidth="2" />
          </g>
        ))}
      </svg>
    </div>
  );
}

// ───────── Circle scene (Lanota-style radial disk) ─────────
function CircleScene() {
  const tracks = 12;
  return (
    <div style={{ position: 'absolute', inset: 0,
      background: 'radial-gradient(ellipse at 50% 50%, #0d2e1a 0%, #050208 70%, #000 100%)' }}>
      <svg viewBox="0 0 800 500" preserveAspectRatio="xMidYMid meet" style={{ position: 'absolute', inset: 0, width: '100%', height: '100%' }}>
        {/* Disk rings */}
        {[80, 130, 180].map((r, i) => (
          <circle key={i} cx="400" cy="250" r={r} fill="none"
            stroke={i === 1 ? 'var(--lime)' : 'rgba(125,255,90,0.2)'}
            strokeWidth={i === 1 ? 2 : 1} />
        ))}
        {/* Inner radius shaded */}
        <circle cx="400" cy="250" r="80" fill="rgba(125,255,90,0.03)" stroke="rgba(125,255,90,0.4)" strokeDasharray="4 3" />
        {/* Hit ring (base radius) — glowing */}
        <circle cx="400" cy="250" r="130" fill="none" stroke="var(--lime)" strokeWidth="6" opacity="0.3" filter="blur(4px)" />
        {/* Lane spokes */}
        {Array.from({length: tracks}).map((_, i) => {
          const a = (i / tracks) * Math.PI * 2 - Math.PI / 2;
          const x1 = 400 + Math.cos(a) * 80, y1 = 250 + Math.sin(a) * 80;
          const x2 = 400 + Math.cos(a) * 200, y2 = 250 + Math.sin(a) * 200;
          return <line key={i} x1={x1} y1={y1} x2={x2} y2={y2} stroke="rgba(125,255,90,0.15)" strokeWidth="1" />;
        })}
        {/* Notes — radial */}
        {[
          { lane: 0, r: 110, type: 'tap', c: '#7dff5a' },
          { lane: 2, r: 100, type: 'tap', c: '#7dff5a' },
          { lane: 5, r: 130, type: 'tap', c: '#7dff5a', sel: true },
          { lane: 7, r: 95, type: 'flick', c: '#ff7a3d' },
          { lane: 9, r: 115, type: 'tap', c: '#7dff5a' },
          { lane: 3, r: 90, type: 'hold', c: '#22e6ff', span: 2 },
        ].map((n, i) => {
          const a = (n.lane / tracks) * Math.PI * 2 - Math.PI / 2;
          const x = 400 + Math.cos(a) * n.r, y = 250 + Math.sin(a) * n.r;
          if (n.type === 'hold') {
            const a2 = ((n.lane + n.span) / tracks) * Math.PI * 2 - Math.PI / 2;
            const x2 = 400 + Math.cos(a2) * n.r, y2 = 250 + Math.sin(a2) * n.r;
            return <g key={i}>
              <path d={`M ${x} ${y} A ${n.r} ${n.r} 0 0 1 ${x2} ${y2}`} fill="none" stroke={n.c} strokeWidth="8" opacity="0.4" />
              <path d={`M ${x} ${y} A ${n.r} ${n.r} 0 0 1 ${x2} ${y2}`} fill="none" stroke={n.c} strokeWidth="3" />
              <circle cx={x} cy={y} r="6" fill={n.c} />
            </g>;
          }
          return <g key={i}>
            <circle cx={x} cy={y} r="10" fill={n.c} stroke="#fff" strokeWidth={n.sel ? 2 : 0.5} strokeDasharray={n.sel ? '3 2' : null} />
            {n.type === 'flick' && (
              <polygon
                points={`${x + Math.cos(a)*14},${y + Math.sin(a)*14} ${x + Math.cos(a-0.3)*8},${y + Math.sin(a-0.3)*8} ${x + Math.cos(a+0.3)*8},${y + Math.sin(a+0.3)*8}`}
                fill={n.c} />
            )}
          </g>;
        })}
        {/* Center label */}
        <text x="400" y="254" textAnchor="middle" fill="rgba(125,255,90,0.7)" fontSize="11" fontFamily="JetBrains Mono">12 tracks</text>
      </svg>
    </div>
  );
}

// ───────── Scan Line scene (Cytus-style sweeping bar) ─────────
function ScanLineScene() {
  return (
    <div style={{ position: 'absolute', inset: 0,
      background: 'radial-gradient(ellipse at 50% 50%, #2e1a0d 0%, #050208 70%, #000 100%)' }}>
      <svg viewBox="0 0 800 500" preserveAspectRatio="none" style={{ position: 'absolute', inset: 0, width: '100%', height: '100%' }}>
        {/* Page area — full canvas, lane-less */}
        <rect x="40" y="40" width="720" height="420" fill="rgba(255,179,71,0.04)" stroke="rgba(255,179,71,0.3)" strokeDasharray="4 3" />
        <text x="48" y="32" fill="rgba(255,179,71,0.7)" fontSize="10" fontFamily="JetBrains Mono">Page 7 · 1.62s · ↓ down sweep</text>
        {/* Notes scattered freely — no lane grid */}
        {[
          { x: 150, y: 120, type: 'tap',   c: '#ffb547' },
          { x: 280, y: 180, type: 'tap',   c: '#ffb547' },
          { x: 420, y: 150, type: 'flick', c: '#ff3df0' },
          { x: 560, y: 220, type: 'tap',   c: '#ffb547' },
          { x: 200, y: 320, type: 'hold',  c: '#7dff5a', span: 80 },
          { x: 480, y: 360, type: 'slide', c: '#22e6ff', dx: 120, dy: -40 },
          { x: 640, y: 300, type: 'tap',   c: '#ffb547', sel: true },
        ].map((n, i) => {
          if (n.type === 'hold') {
            return <g key={i}>
              <rect x={n.x - 12} y={n.y - 6} width={n.span} height={12} fill={n.c} opacity="0.5" rx={6} />
              <rect x={n.x - 12} y={n.y - 6} width={n.span} height={12} fill="none" stroke={n.c} rx={6} />
            </g>;
          }
          if (n.type === 'slide') {
            return <g key={i}>
              <line x1={n.x} y1={n.y} x2={n.x + n.dx} y2={n.y + n.dy} stroke={n.c} strokeWidth="3" />
              <line x1={n.x} y1={n.y} x2={n.x + n.dx} y2={n.y + n.dy} stroke={n.c} strokeWidth="10" opacity="0.3" filter="blur(3px)" />
              <circle cx={n.x} cy={n.y} r="8" fill={n.c} />
              <circle cx={n.x + n.dx} cy={n.y + n.dy} r="6" fill={n.c} stroke="#fff" strokeWidth="1" />
            </g>;
          }
          return <g key={i}>
            <circle cx={n.x} cy={n.y} r="11" fill={n.c} stroke={n.sel ? '#fff' : 'none'} strokeDasharray={n.sel ? '3 2' : null} />
            {n.type === 'flick' && (
              <polygon points={`${n.x + 14},${n.y} ${n.x + 4},${n.y - 6} ${n.x + 4},${n.y + 6}`} fill={n.c} />
            )}
          </g>;
        })}
        {/* Scan line — sweeping horizontal bar */}
        <line x1="40" y1="280" x2="760" y2="280" stroke="var(--amber)" strokeWidth="3" />
        <line x1="40" y1="280" x2="760" y2="280" stroke="var(--amber)" strokeWidth="14" opacity="0.4" filter="blur(5px)" />
        <polygon points="40,272 28,280 40,288" fill="var(--amber)" />
        <polygon points="760,272 772,280 760,288" fill="var(--amber)" />
        <text x="400" y="276" textAnchor="middle" fill="#000" fontSize="10" fontWeight="700" fontFamily="JetBrains Mono">SCAN</text>
      </svg>
    </div>
  );
}

// ───────── Mode-specific scene-area extras ─────────
function ArcCurveStrip() {
  return (
    <div style={{ height: 120, background: 'var(--bg-panel)', borderBottom: '1px solid var(--border)', position: 'relative' }}>
      <div style={{ position: 'absolute', top: 6, left: 10, fontSize: 10, color: 'var(--text-low)', letterSpacing: '0.1em', textTransform: 'uppercase' }}>Arc height curve · 4 waypoints</div>
      <svg viewBox="0 0 800 120" preserveAspectRatio="none" style={{ width: '100%', height: '100%' }}>
        <line x1="0" y1="60" x2="800" y2="60" stroke="var(--divider)" />
        <path d="M 40 90 C 200 50, 350 30, 480 70 S 700 100, 760 50" fill="none" stroke="var(--magenta)" strokeWidth="2" />
        {[{ x: 40, y: 90 }, { x: 280, y: 40 }, { x: 480, y: 70 }, { x: 760, y: 50 }].map((p, i) => (
          <g key={i}>
            <circle cx={p.x} cy={p.y} r="6" fill="#ff3df0" stroke="#fff" strokeWidth="1.5" />
          </g>
        ))}
      </svg>
    </div>
  );
}

function DiskFxStrip() {
  return (
    <div style={{ height: 34, background: 'var(--bg-panel)', borderBottom: '1px solid var(--border)', display: 'flex', alignItems: 'center', padding: '0 10px', gap: 10 }}>
      <span style={{ fontSize: 10, color: 'var(--text-low)', letterSpacing: '0.1em', textTransform: 'uppercase' }}>Disk FX</span>
      <div style={{ flex: 1, position: 'relative', height: 18, background: 'var(--bg-panel-2)', borderRadius: 3 }}>
        {[0.12, 0.28, 0.45, 0.61, 0.78, 0.91].map((p, i) => (
          <div key={i} style={{ position: 'absolute', left: `${p*100}%`, top: 2, bottom: 2, width: 8, transform: 'translateX(-50%)', background: 'var(--lime)', clipPath: 'polygon(50% 0, 100% 50%, 50% 100%, 0 50%)' }}></div>
        ))}
      </div>
      <Btn kind="ghost" size="sm">+ Keyframe</Btn>
      <span style={{ fontSize: 10, color: 'var(--text-low)' }}>SineInOut</span>
    </div>
  );
}

function ScanPagesStrip() {
  const pages = [
    { dur: 1.62, dir: 'down', spd: 1.0 },
    { dur: 1.62, dir: 'up',   spd: 1.0 },
    { dur: 1.62, dir: 'down', spd: 1.5 },
    { dur: 0.81, dir: 'up',   spd: 1.0, partial: true },
    { dur: 1.62, dir: 'down', spd: 1.0 },
    { dur: 1.62, dir: 'up',   spd: 0.75 },
    { dur: 1.62, dir: 'down', spd: 1.0, active: true },
    { dur: 1.62, dir: 'up',   spd: 1.0 },
  ];
  return (
    <div style={{ height: 56, background: 'var(--bg-panel)', borderBottom: '1px solid var(--border)', display: 'flex', alignItems: 'center', padding: '0 10px', gap: 6, overflowX: 'auto' }}>
      <span style={{ fontSize: 10, color: 'var(--text-low)', letterSpacing: '0.1em', textTransform: 'uppercase', flexShrink: 0 }}>Pages</span>
      {pages.map((p, i) => (
        <div key={i} style={{
          width: 70, height: 38, flexShrink: 0,
          borderRadius: 4, padding: 4,
          background: p.active ? 'rgba(255,179,71,0.15)' : 'var(--bg-panel-2)',
          border: `1px solid ${p.active ? 'var(--amber)' : 'var(--border)'}`,
          boxShadow: p.active ? '0 0 8px rgba(255,179,71,0.5)' : 'none',
          display: 'flex', flexDirection: 'column', gap: 2, position: 'relative',
        }}>
          <div className="mono" style={{ fontSize: 8, color: 'var(--text-mid)' }}>{i+1} · {p.dir}{p.partial ? ' ✂' : ''}</div>
          <div style={{ fontSize: 9, color: 'var(--text-low)' }}>{p.dur.toFixed(2)}s</div>
          {p.spd !== 1.0 && <div style={{ position: 'absolute', top: 2, right: 4, fontSize: 8, color: 'var(--amber)', fontFamily: 'JetBrains Mono' }}>{p.spd}×</div>}
        </div>
      ))}
      <Btn kind="ghost" size="sm">+ Page</Btn>
    </div>
  );
}

// ───────── Chart timeline ─────────
function ChartTimeline({ mode = 'drop2d', cfg }) {
  const TICKS = 16;
  const LANES = mode === 'scanline' ? 1 : (mode === 'circle' ? 12 : (mode === 'drop3d' ? 4 : 7));
  const accent = cfg.accent;
  const events = mode === 'scanline'
    ? [
        { l: 0, t: 1, c: 'amber' }, { l: 0, t: 3, c: 'amber' }, { l: 0, t: 4, c: 'amber' },
        { l: 0, t: 5, c: 'lime', hold: 3 }, { l: 0, t: 8, c: 'cyan', selected: true },
        { l: 0, t: 11, c: 'amber' }, { l: 0, t: 13, c: 'magenta' },
      ]
    : [
        { l: 2, t: 1, c: 'cyan' }, { l: 4, t: 1, c: 'cyan' },
        { l: 0, t: 3, c: 'magenta' }, { l: 6 % LANES, t: 3, c: 'magenta' },
        { l: 3 % LANES, t: 4, c: 'cyan' },
        { l: 1, t: 5, c: 'lime', hold: 3 }, { l: 5 % LANES, t: 5, c: 'lime', hold: 3 },
        { l: 3 % LANES, t: 8, c: 'cyan' },
        { l: 4 % LANES, t: 11, c: 'amber', selected: true },
      ];
  const colorMap = { cyan: '#22e6ff', magenta: '#ff3df0', lime: '#7dff5a', amber: '#ffb547' };
  return (
    <div style={{ flex: 1, display: 'flex', flexDirection: 'column', background: 'var(--bg-panel)', minHeight: 0 }}>
      <div style={{ height: 24, display: 'flex', alignItems: 'center', borderBottom: '1px solid var(--divider)', background: 'var(--bg-panel-2)', position: 'relative' }}>
        <div style={{ width: 60, fontSize: 10, color: 'var(--text-low)', textTransform: 'uppercase', letterSpacing: '0.1em', paddingLeft: 10 }}>Time</div>
        <div style={{ flex: 1, position: 'relative', height: '100%' }}>
          {Array.from({length: TICKS+1}).map((_, i) => (
            <div key={i} style={{ position: 'absolute', left: `${(i/TICKS)*100}%`, top: 0, bottom: 0, borderLeft: i % 4 === 0 ? '1px solid var(--border-hi)' : '1px solid var(--border)' }}>
              {i % 4 === 0 && <span className="mono" style={{ position: 'absolute', top: 4, left: 4, fontSize: 9, color: 'var(--text-low)' }}>{String(Math.floor(i/4)).padStart(2,'0')}:0{i%4}</span>}
            </div>
          ))}
          <div style={{ position: 'absolute', left: '54%', top: 0, bottom: 0, width: 2, background: `var(--${accent})`, boxShadow: `0 0 8px var(--${accent})`, zIndex: 3 }}>
            <div style={{ position: 'absolute', top: -4, left: -5, width: 12, height: 8, background: `var(--${accent})`, clipPath: 'polygon(0 0, 100% 0, 50% 100%)' }}></div>
          </div>
        </div>
      </div>
      <div style={{ flex: 1, overflow: 'auto', minHeight: 0 }}>
        {Array.from({length: LANES}).map((_, lane) => (
          <div key={lane} style={{ height: 28, display: 'flex', borderBottom: '1px solid var(--divider)', position: 'relative' }}>
            <div style={{ width: 60, padding: '0 8px', display: 'flex', alignItems: 'center', justifyContent: 'space-between', borderRight: '1px solid var(--divider)', background: 'var(--bg-panel-2)' }}>
              <span className="mono" style={{ fontSize: 10, color: 'var(--text-low)' }}>{mode === 'scanline' ? 'free' : `L${lane+1}`}</span>
              <Icon d={I.eye} size={10} stroke="var(--text-low)" />
            </div>
            <div style={{ flex: 1, position: 'relative', background: lane % 2 === 0 ? 'var(--bg-panel)' : 'var(--bg-panel-2)' }}>
              {Array.from({length: TICKS}).map((_, t) => (
                <div key={t} style={{ position: 'absolute', left: `${(t/TICKS)*100}%`, top: 0, bottom: 0, width: 1, background: t % 4 === 0 ? 'var(--border-hi)' : 'var(--divider)' }}></div>
              ))}
              <div style={{ position: 'absolute', left: '54%', top: 0, bottom: 0, width: 1, background: `var(--${accent})`, opacity: 0.5, zIndex: 2 }}></div>
              {events.filter(e => e.l === lane).map((e, i) => {
                const x = (e.t / TICKS) * 100;
                const col = colorMap[e.c];
                if (e.hold) {
                  const w = (e.hold / TICKS) * 100;
                  return <div key={i} style={{ position: 'absolute', left: `${x}%`, top: 6, height: 16, width: `${w}%`, background: `linear-gradient(90deg, ${col}, ${col}aa)`, borderRadius: 3, border: `1px solid ${col}`, boxShadow: `0 0 6px ${col}88` }}></div>;
                }
                return <div key={i} style={{ position: 'absolute', left: `${x}%`, top: 8, width: 12, height: 12, transform: 'translateX(-50%)', background: col, borderRadius: 2, boxShadow: `0 0 8px ${col}`, border: e.selected ? '1px solid #fff' : 'none' }}></div>;
              })}
            </div>
          </div>
        ))}
      </div>
    </div>
  );
}

// ───────── Waveform ─────────
function Waveform({ accent = 'cyan' }) {
  const N = 240;
  const env = Array.from({length: N}, (_, i) => {
    const x = i / N;
    const peak = Math.exp(-Math.pow((x - 0.55) * 5, 2)) * 0.6;
    const base = 0.25 + Math.sin(x * 30) * 0.1 + Math.sin(x * 73) * 0.08;
    const noise = (Math.sin(i * 1.3) * Math.sin(i * 2.7) + 1) * 0.15;
    return Math.min(0.95, Math.max(0.08, base + peak + noise));
  });
  const markers = [0.05, 0.12, 0.18, 0.24, 0.30, 0.36, 0.42, 0.46, 0.50, 0.54, 0.56, 0.60, 0.64, 0.68, 0.72, 0.78, 0.84, 0.90];
  const accentColors = { cyan: '#22e6ff', magenta: '#ff3df0', lime: '#7dff5a', amber: '#ffb547' };
  const c = accentColors[accent] || accentColors.cyan;
  return (
    <div style={{ height: '100%', position: 'relative', overflow: 'hidden', background: 'linear-gradient(180deg, #0a0a0d, #050507)' }}>
      <svg viewBox={`0 0 ${N} 100`} preserveAspectRatio="none" style={{ position: 'absolute', inset: 0, width: '100%', height: '100%' }}>
        {env.map((v, i) => (
          <line key={i} x1={i + 0.5} y1={50 - v*45} x2={i + 0.5} y2={50 + v*45}
            stroke={i / N < 0.54 ? c : c + '55'} strokeWidth="1" />
        ))}
      </svg>
      {markers.map((m, i) => (
        <div key={i} style={{ position: 'absolute', left: `${m*100}%`, top: 4, bottom: 4, width: 1, background: 'rgba(255,179,71,0.6)', borderTop: '4px solid var(--amber)', borderBottom: '4px solid var(--amber)' }}></div>
      ))}
      <div style={{ position: 'absolute', left: '54%', top: 0, bottom: 0, width: 2, background: c, boxShadow: `0 0 10px ${c}`, zIndex: 3 }}></div>
      <div className="mono" style={{ position: 'absolute', bottom: 4, left: 6, fontSize: 9, color: 'var(--text-low)' }}>00:00</div>
      <div className="mono" style={{ position: 'absolute', bottom: 4, right: 6, fontSize: 9, color: 'var(--text-low)' }}>03:12</div>
    </div>
  );
}

window.SongEditor = SongEditor;
