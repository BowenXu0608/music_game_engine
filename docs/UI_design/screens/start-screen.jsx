// Start Screen Editor — Background, Logo, Tap Text, Transition, Audio
// Mirrors StartScreenEditor.cpp: 5 CollapsingHeaders, Materials tab.

function StartScreenEditor() {
  const [tab, setTab] = React.useState('p');     // properties | materials
  const [logoType, setLogoType] = React.useState('text');
  const [glow, setGlow] = React.useState(true);
  const [transition, setTransition] = React.useState('fade');

  return (
    <Surface>
      <TopBar
        crumbs={['BandoriSandbox', 'Start Screen']}
        right={
          <div style={{ display: 'flex', gap: 8, alignItems: 'center' }}>
            <Btn kind="ghost" size="sm" icon={I.chevL}>Hub</Btn>
            <Btn kind="ghost" size="sm" iconRight={I.chevR}>Music Selection</Btn>
            <div style={{ width: 1, height: 20, background: 'var(--border)' }}></div>
            <Btn kind="success" size="md" icon={I.play} glow>Test Game</Btn>
          </div>
        }
      />

      <div style={{ flex: 1, display: 'flex', minHeight: 0 }}>
        {/* Hierarchy */}
        <div style={{ width: 220, borderRight: '1px solid var(--border)', display: 'flex', flexDirection: 'column' }}>
          <Tabs items={[{ id: 'h', label: 'Hierarchy' }]} value="h" onChange={()=>{}} />
          <div style={{ flex: 1, overflow: 'auto', padding: 4 }}>
            <SectionHeader label="Start Screen" />
            <SidebarItem icon={I.image}   label="Background"      indent={1} />
            <SidebarItem icon={I.tap}     label="Logo"            indent={1} active />
            <SidebarItem icon={I.tap}     label="Tap Text"        indent={1} />
            <SidebarItem icon={I.sparkle} label="Transition Effect" indent={1} />
            <SidebarItem icon={I.audio}   label="Audio"           indent={1} />
          </div>
        </div>

        {/* Preview */}
        <div style={{ flex: 1, display: 'flex', flexDirection: 'column', background: 'var(--bg-base)', minWidth: 0 }}>
          <div style={{
            height: 36, padding: '0 12px',
            display: 'flex', alignItems: 'center', gap: 10,
            borderBottom: '1px solid var(--border)',
          }}>
            <div style={{ display: 'flex', gap: 0, background: 'var(--bg-panel)', borderRadius: 5, padding: 2 }}>
              <Btn kind="ghost" size="sm" active>16:9</Btn>
              <Btn kind="ghost" size="sm">16:10</Btn>
              <Btn kind="ghost" size="sm">21:9</Btn>
              <Btn kind="ghost" size="sm">Custom</Btn>
            </div>
            <span className="mono" style={{ fontSize: 10, color: 'var(--text-low)' }}>1920×1080 · landscape</span>
            <div style={{ flex: 1 }}></div>
            <IconBtn icon={I.zoom} size={26} />
            <span className="mono" style={{ fontSize: 11, color: 'var(--text-mid)' }}>78%</span>
            <IconBtn icon={I.zoomout} size={26} />
          </div>

          <div style={{ flex: 1, padding: 24, display: 'flex', alignItems: 'center', justifyContent: 'center', minHeight: 0 }}>
            <div style={{
              aspectRatio: '16/9', maxHeight: '100%', maxWidth: '100%', width: '100%',
              borderRadius: 6, overflow: 'hidden', position: 'relative',
              background: `radial-gradient(ellipse at 30% 40%, #1a0d2e 0%, #050208 60%),
                           linear-gradient(180deg, #0a0518 0%, #000 100%)`,
              boxShadow: '0 0 0 1px var(--border-hi), 0 30px 80px rgba(0,0,0,0.6)',
            }}>
              <div style={{
                position: 'absolute', inset: 0,
                backgroundImage: 'linear-gradient(rgba(34,230,255,0.06) 1px, transparent 1px), linear-gradient(90deg, rgba(34,230,255,0.06) 1px, transparent 1px)',
                backgroundSize: '40px 40px',
                maskImage: 'radial-gradient(ellipse at center, black 30%, transparent 70%)',
              }}></div>
              <div style={{ position: 'absolute', top: '15%', left: '12%', width: 180, height: 180, borderRadius: '50%',
                background: 'radial-gradient(circle, rgba(34,230,255,0.4), transparent 60%)', filter: 'blur(20px)' }}></div>
              <div style={{ position: 'absolute', bottom: '10%', right: '8%', width: 220, height: 220, borderRadius: '50%',
                background: 'radial-gradient(circle, rgba(255,61,240,0.3), transparent 60%)', filter: 'blur(24px)' }}></div>

              <div style={{
                position: 'absolute', top: '38%', left: '50%', transform: 'translate(-50%,-50%)',
                fontSize: 88, fontWeight: 800, letterSpacing: '-0.04em',
                color: 'transparent',
                background: 'linear-gradient(180deg, #fff, #22e6ff 60%, #ff3df0)',
                WebkitBackgroundClip: 'text', backgroundClip: 'text',
                filter: glow ? 'drop-shadow(0 0 30px rgba(34,230,255,0.4))' : 'none',
              }}>MOSAIC</div>

              <div style={{
                position: 'absolute', top: '24%', left: '24%', width: '52%', height: '28%',
                border: '1px dashed var(--cyan)',
                boxShadow: '0 0 0 1px rgba(34,230,255,0.3)',
              }}>
                {[0,1,2,3].map(i => (
                  <div key={i} style={{
                    position: 'absolute',
                    top: i < 2 ? -3 : 'auto', bottom: i >= 2 ? -3 : 'auto',
                    left: i % 2 === 0 ? -3 : 'auto', right: i % 2 === 1 ? -3 : 'auto',
                    width: 6, height: 6, background: 'var(--cyan)', boxShadow: '0 0 6px var(--cyan)',
                  }}></div>
                ))}
              </div>

              <div style={{
                position: 'absolute', bottom: '14%', left: '50%', transform: 'translateX(-50%)',
                fontSize: 18, fontWeight: 500, color: 'rgba(255,255,255,0.85)',
                letterSpacing: '0.3em', textTransform: 'uppercase',
                animation: 'mge-pulse 2s infinite',
              }}>· Tap to Start ·</div>
              <style>{`@keyframes mge-pulse { 0%,100% { opacity:0.4 } 50% { opacity:1 } }`}</style>

              <div style={{ position: 'absolute', top: 8, left: 8, padding: '2px 8px', borderRadius: 4, background: 'rgba(0,0,0,0.5)', fontSize: 9, fontFamily: 'JetBrains Mono, monospace', color: 'var(--text-mid)' }}>16:9 · 1920×1080</div>
            </div>
          </div>
        </div>

        {/* Properties / Materials */}
        <div style={{ width: 296, borderLeft: '1px solid var(--border)', display: 'flex', flexDirection: 'column' }}>
          <Tabs
            items={[{ id: 'p', label: 'Properties', icon: I.settings }, { id: 'm', label: 'Materials', icon: I.sparkle }]}
            value={tab}
            onChange={setTab}
          />

          {tab === 'p' && (
            <div style={{ flex: 1, overflow: 'auto' }}>
              {/* ── Background ── */}
              <SectionHeader label="Background" right={<DefaultPill />} />
              <PropBody>
                <Placeholder label="bg.png — drop image or video" h={64} color="cyan" />
                <Row label="Image">
                  <Field value="assets/bg.png" mono />
                </Row>
              </PropBody>

              {/* ── Logo ── */}
              <SectionHeader label="Logo" right={<DefaultPill />} />
              <PropBody>
                <Row label="Type">
                  <SegBar
                    value={logoType}
                    onChange={setLogoType}
                    items={[{ id: 'text', label: 'Text' }, { id: 'image', label: 'Image' }]}
                  />
                </Row>
                {logoType === 'text' ? (
                  <>
                    <Row label="Text"><Field value="MOSAIC" /></Row>
                    <div style={{ display: 'flex', gap: 6, alignItems: 'flex-end' }}>
                      <div style={{ flex: 1 }}><Slider label="Font size" value={88} min={12} max={300} suffix="px" /></div>
                      <Pill on label="Bold" />
                    </div>
                    <Row label="Color">
                      <Swatch g="linear-gradient(180deg, #fff, #22e6ff, #ff3df0)" hex="#22E6FF" />
                    </Row>
                  </>
                ) : (
                  <Placeholder label="logo.png — drop here" h={56} />
                )}
                <div style={{ display: 'flex', gap: 6 }}>
                  <Field label="X" value="0.50" mono />
                  <Field label="Y" value="0.38" mono />
                </div>
                <Slider label="Scale" value={1.0} min={0.1} max={5.0} accent="cyan" />
                <ToggleRow label="Glow / Outline" value={glow} onChange={setGlow} />
                {glow && (
                  <>
                    <Row label="Glow color">
                      <Swatch hex="#22E6FF" />
                    </Row>
                    <Slider label="Glow radius" value={18} min={1} max={32} />
                  </>
                )}
              </PropBody>

              {/* ── Tap Text ── */}
              <SectionHeader label="Tap Text" right={<DefaultPill />} />
              <PropBody>
                <Field value="Tap to Start" />
                <Slider label="Size" value={18} min={8} max={72} suffix="px" />
              </PropBody>

              {/* ── Transition Effect ── */}
              <SectionHeader label="Transition Effect" right={<DefaultPill />} />
              <PropBody>
                <Row label="Effect">
                  <Dropdown
                    value={transition}
                    onChange={setTransition}
                    items={[
                      { id: 'fade',   label: 'Fade to Black' },
                      { id: 'slide',  label: 'Slide Left' },
                      { id: 'zoom',   label: 'Zoom In' },
                      { id: 'ripple', label: 'Ripple' },
                      { id: 'custom', label: 'Custom' },
                    ]}
                  />
                </Row>
                <Slider label="Duration" value={0.6} min={0.1} max={2.0} suffix="s" accent="magenta" />
                {transition === 'custom' && (
                  <>
                    <Row label="Script"><Field value="scripts/transition.lua" mono /></Row>
                    <div style={{ fontSize: 10, color: 'var(--text-low)', lineHeight: 1.4 }}>
                      Lua receives: <code style={{ color: 'var(--cyan)' }}>progress, tap_x, tap_y</code>
                    </div>
                  </>
                )}
              </PropBody>

              {/* ── Audio ── */}
              <SectionHeader label="Audio" right={<DefaultPill />} />
              <PropBody>
                <div style={{ fontSize: 10, color: 'var(--text-low)', textTransform: 'uppercase', letterSpacing: '0.08em' }}>BGM</div>
                <FileRow file="loop_main.ogg" />
                <Slider label="Volume" value={0.7} min={0} max={1} />
                <ToggleRow label="Loop" value={true} />
                <div style={{ height: 1, background: 'var(--border)', margin: '4px 0' }}></div>
                <div style={{ fontSize: 10, color: 'var(--text-low)', textTransform: 'uppercase', letterSpacing: '0.08em' }}>Tap SFX</div>
                <FileRow file="tap_start.wav" />
                <Slider label="Volume" value={0.85} min={0} max={1} accent="amber" />
              </PropBody>

              <div style={{ height: 24 }}></div>
            </div>
          )}

          {tab === 'm' && <MaterialsPanel />}
        </div>
      </div>
    </Surface>
  );
}

// ── Materials panel (Materials tab) ───────────────────────────────────────────
// Mirrors the right-side Materials column: list + builder for one Material.

function MaterialsPanel() {
  const [sel, setSel] = React.useState(0);
  const mats = [
    { name: 'logo_glow',     kind: 'Glow',     target: 'start / logo' },
    { name: 'bg_scroll',     kind: 'Scroll',   target: 'start / bg' },
    { name: 'note_pulse',    kind: 'Pulse',    target: '2d / tap_note' },
    { name: 'judgment_line', kind: 'Gradient', target: '2d / judgment_line' },
    { name: 'ring_shimmer',  kind: 'Custom',   target: '3d / ring' },
  ];
  return (
    <div style={{ flex: 1, overflow: 'auto', display: 'flex', flexDirection: 'column' }}>
      <div style={{ padding: '8px 12px', display: 'flex', alignItems: 'center', gap: 6, borderBottom: '1px solid var(--border)' }}>
        <span style={{ fontSize: 10, color: 'var(--text-low)', textTransform: 'uppercase', letterSpacing: '0.08em' }}>Library</span>
        <div style={{ flex: 1 }}></div>
        <Btn kind="ghost" size="sm" icon={I.plus}>New</Btn>
      </div>
      <div style={{ padding: 4 }}>
        {mats.map((m, i) => (
          <div key={m.name}
            onClick={() => setSel(i)}
            style={{
              padding: '6px 8px', borderRadius: 4, cursor: 'pointer',
              background: i === sel ? 'rgba(34,230,255,0.12)' : 'transparent',
              borderLeft: i === sel ? '2px solid var(--cyan)' : '2px solid transparent',
              display: 'flex', alignItems: 'center', gap: 8,
            }}>
            <div style={{ width: 6, height: 6, borderRadius: '50%', background: 'var(--cyan)', boxShadow: i === sel ? '0 0 6px var(--cyan)' : 'none' }}></div>
            <div style={{ flex: 1, minWidth: 0 }}>
              <div className="mono" style={{ fontSize: 11, color: 'var(--text-hi)' }}>{m.name}</div>
              <div style={{ fontSize: 9, color: 'var(--text-low)' }}>{m.kind} → {m.target}</div>
            </div>
          </div>
        ))}
      </div>

      <div style={{ borderTop: '1px solid var(--border)', padding: '8px 12px' }}>
        <span style={{ fontSize: 10, color: 'var(--text-low)', textTransform: 'uppercase', letterSpacing: '0.08em' }}>Edit · {mats[sel].name}</span>
      </div>
      <div style={{ padding: '0 12px 12px', display: 'flex', flexDirection: 'column', gap: 8 }}>
        <Row label="Target mode">
          <Dropdown value="start" items={[
            {id:'start',label:'start'},{id:'2d',label:'2d'},{id:'3d',label:'3d'},
            {id:'circle',label:'circle'},{id:'lanota',label:'lanota'},{id:'phigros',label:'phigros'}
          ]} />
        </Row>
        <Row label="Target slot">
          <Dropdown value="logo" items={[
            {id:'bg',label:'bg'},{id:'logo',label:'logo'},{id:'tap_text',label:'tap_text'}
          ]} />
        </Row>
        <Row label="Kind">
          <Dropdown value={mats[sel].kind.toLowerCase()} items={[
            {id:'unlit',label:'Unlit'},{id:'glow',label:'Glow'},{id:'scroll',label:'Scroll'},
            {id:'pulse',label:'Pulse'},{id:'gradient',label:'Gradient'},{id:'custom',label:'Custom'}
          ]} />
        </Row>
        <Row label="Texture"><Field value="assets/glow.png" mono /></Row>
        <Slider label="Intensity" value={1.4} min={0} max={4} accent="cyan" />
        <Slider label="Speed" value={0.5} min={0} max={4} />

        <div style={{ marginTop: 8, padding: 8, background: 'var(--bg-base)', borderRadius: 4, border: '1px solid var(--border)' }}>
          <div style={{ fontSize: 10, color: 'var(--text-low)', marginBottom: 6, textTransform: 'uppercase', letterSpacing: '0.08em' }}>AI Shader Generator</div>
          <div style={{ fontSize: 11, color: 'var(--text-mid)', lineHeight: 1.4, marginBottom: 6 }}>
            Describe the effect. AI writes <code style={{ color: 'var(--cyan)' }}>.frag</code>, compiles via glslc, retries on errors.
          </div>
          <div style={{ height: 56, padding: 6, background: 'var(--bg-panel)', borderRadius: 3, border: '1px solid var(--border-hi)', fontSize: 11, color: 'var(--text-hi)', fontStyle: 'italic' }}>
            "soft cyan glow that pulses with the beat, subtle scanlines"
          </div>
          <div style={{ display: 'flex', gap: 6, marginTop: 6 }}>
            <Btn kind="primary" size="sm" icon={I.sparkle}>Generate</Btn>
            <Btn kind="ghost" size="sm">Configure</Btn>
          </div>
        </div>
      </div>
    </div>
  );
}

// ── small helpers ─────────────────────────────────────────────────────────────

function PropBody({ children }) {
  return <div style={{ padding: '0 12px 12px', display: 'flex', flexDirection: 'column', gap: 8 }}>{children}</div>;
}

function DefaultPill() {
  return <span style={{ fontSize: 9, color: 'var(--cyan)', cursor: 'pointer', padding: '2px 6px', border: '1px solid var(--border-hi)', borderRadius: 3, textTransform: 'uppercase', letterSpacing: '0.08em' }}>Default</span>;
}

function Row({ label, children }) {
  return (
    <div>
      <div style={{ fontSize: 10, color: 'var(--text-low)', marginBottom: 3, textTransform: 'uppercase', letterSpacing: '0.06em' }}>{label}</div>
      {children}
    </div>
  );
}

function ToggleRow({ label, value, onChange }) {
  return (
    <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center' }}>
      <span style={{ fontSize: 11, color: 'var(--text-mid)' }}>{label}</span>
      <Toggle value={value} onChange={onChange} />
    </div>
  );
}

function Pill({ on, label }) {
  return (
    <div style={{
      padding: '5px 10px', borderRadius: 4, fontSize: 11, fontWeight: 600,
      background: on ? 'var(--cyan-dim)' : 'var(--bg-panel)',
      color: on ? 'var(--cyan)' : 'var(--text-mid)',
      border: '1px solid ' + (on ? 'var(--cyan)' : 'var(--border-hi)'),
      cursor: 'pointer', userSelect: 'none',
    }}>{label}</div>
  );
}

function Swatch({ g, hex }) {
  return (
    <div style={{ display: 'flex', gap: 6, alignItems: 'center' }}>
      <div style={{ width: 28, height: 24, borderRadius: 4, background: g || hex, border: '1px solid var(--border-hi)' }}></div>
      <Field value={hex} mono />
    </div>
  );
}

function FileRow({ file }) {
  return (
    <div style={{ display: 'flex', gap: 4, alignItems: 'center' }}>
      <div style={{ flex: 1 }}><Field value={file} mono /></div>
      <IconBtn icon={I.folder} size={26} />
      <IconBtn icon={I.x} size={26} />
    </div>
  );
}

function SegBar({ value, onChange, items }) {
  return (
    <div style={{ display: 'flex', gap: 0, background: 'var(--bg-panel)', borderRadius: 4, padding: 2, border: '1px solid var(--border)' }}>
      {items.map(it => (
        <div key={it.id} onClick={() => onChange && onChange(it.id)}
          style={{
            flex: 1, textAlign: 'center', padding: '4px 0', borderRadius: 3,
            fontSize: 11, fontWeight: 600, cursor: 'pointer',
            background: value === it.id ? 'var(--bg-elev)' : 'transparent',
            color: value === it.id ? 'var(--text-hi)' : 'var(--text-low)',
          }}>{it.label}</div>
      ))}
    </div>
  );
}

function Dropdown({ value, items, onChange }) {
  const cur = items.find(i => i.id === value) || items[0];
  return (
    <div style={{
      display: 'flex', alignItems: 'center', justifyContent: 'space-between',
      padding: '5px 8px', background: 'var(--bg-panel)',
      border: '1px solid var(--border-hi)', borderRadius: 4,
      fontSize: 11, color: 'var(--text-hi)', cursor: 'pointer',
    }}>
      <span>{cur.label}</span>
      <span style={{ color: 'var(--text-low)', fontSize: 9 }}>▾</span>
    </div>
  );
}

window.StartScreenEditor = StartScreenEditor;
