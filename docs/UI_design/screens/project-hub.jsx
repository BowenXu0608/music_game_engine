// Project Hub — browse + create + import projects

const PROJECTS = [
  { name: 'BandoriSandbox',  mode: 'Drop 2D',       songs: 12, modified: '2026-04-28 14:32', accent: 'cyan',    selected: true },
  { name: 'NeonArc',          mode: 'Drop 3D',       songs: 24, modified: '2026-04-27 22:11', accent: 'magenta' },
  { name: 'CytusCanon',       mode: 'Scan Line',     songs: 8,  modified: '2026-04-25 09:48', accent: 'amber' },
  { name: 'LanotaCircle',     mode: 'Circle',        songs: 18, modified: '2026-04-22 16:05', accent: 'lime' },
  { name: 'midnight_drive',   mode: 'Drop 2D',       songs: 6,  modified: '2026-04-20 11:30', accent: 'cyan' },
  { name: 'astral_voyage',    mode: 'Drop 3D',       songs: 15, modified: '2026-04-18 03:12', accent: 'magenta' },
];

function ProjectHub() {
  return (
    <Surface>
      <TopBar
        crumbs={['Project Hub']}
        right={
          <IconBtn icon={I.settings} title="Settings" />
        }
      />

      <div style={{ flex: 1, display: 'flex', minHeight: 0, padding: 14, gap: 14 }}>
        {/* Left rail */}
        <div style={{ width: 220, display: 'flex', flexDirection: 'column', gap: 10 }}>
          <Panel>
            <div style={{ padding: '4px 8px 12px' }}>
              {[
                { icon: I.folder, label: 'All projects', active: true, count: 6 },
                { icon: I.download, label: 'Recent', count: 3 },
                { icon: I.star, label: 'Starred', count: 2 },
              ].map((it, i) => (
                <div key={i} style={{
                  display: 'flex', alignItems: 'center', gap: 10,
                  padding: '8px 10px', borderRadius: 6,
                  background: it.active ? 'rgba(34,230,255,0.08)' : 'transparent',
                  color: it.active ? 'var(--text-hi)' : 'var(--text-mid)',
                  fontSize: 12, cursor: 'pointer',
                  borderLeft: it.active ? '2px solid var(--cyan)' : '2px solid transparent',
                  marginLeft: -2,
                }}>
                  <Icon d={it.icon} size={14} stroke={it.active ? 'var(--cyan)' : 'currentColor'} />
                  <span style={{ flex: 1 }}>{it.label}</span>
                  {it.count != null && <span style={{ fontSize: 10, color: 'var(--text-low)', fontFamily: 'JetBrains Mono, monospace' }}>{it.count}</span>}
                </div>
              ))}
            </div>
          </Panel>
        </div>

        {/* Main area */}
        <div style={{ flex: 1, display: 'flex', flexDirection: 'column', gap: 12, minWidth: 0 }}>
          {/* Header row */}
          <div style={{ display: 'flex', alignItems: 'center', gap: 12 }}>
            <div>
              <div style={{ fontSize: 22, fontWeight: 600, letterSpacing: '-0.01em' }}>Projects</div>
              <div style={{ fontSize: 11, color: 'var(--text-low)', marginTop: 2 }}>
                <span className="mono">6 projects</span> · last opened <span className="mono">BandoriSandbox</span>
              </div>
            </div>
            <div style={{ flex: 1 }}></div>
            <div style={{
              display: 'flex', alignItems: 'center', gap: 6,
              height: 30, padding: '0 10px', borderRadius: 6,
              background: 'var(--bg-panel)',
              border: '1px solid var(--border)',
              width: 240,
            }}>
              <Icon d={I.search} size={13} stroke="var(--text-low)" />
              <span style={{ fontSize: 12, color: 'var(--text-low)', flex: 1 }}>Search projects…</span>
              <span className="mono" style={{ fontSize: 9, color: 'var(--text-low)', padding: '1px 4px', border: '1px solid var(--border)', borderRadius: 3 }}>⌘K</span>
            </div>
            <Btn kind="outline" icon={I.upload}>Add file</Btn>
            <Btn kind="primary" icon={I.plus} glow>Create game</Btn>
          </div>

          {/* Filter row */}
          <div style={{ display: 'flex', alignItems: 'center', gap: 8 }}>
            <Pill color="cyan" solid small>All</Pill>
            <Pill color="cyan" small>Drop 2D</Pill>
            <Pill color="magenta" small>Drop 3D</Pill>
            <Pill color="amber" small>Scan Line</Pill>
            <Pill color="lime" small>Circle</Pill>
            <div style={{ flex: 1 }}></div>
            <span style={{ fontSize: 11, color: 'var(--text-low)' }}>Sort:</span>
            <Btn kind="ghost" size="sm" iconRight={I.chevD}>Last modified</Btn>
            <IconBtn icon={I.filter} size={26} title="Filter" />
          </div>

          <div style={{ flex: 1, display: 'flex', gap: 12, minHeight: 0 }}>
          {/* Project rows */}
          <Panel style={{ flex: 1 }} scroll>
            <div style={{
              display: 'grid', gridTemplateColumns: '40px 2fr 1fr 1fr 1fr 100px',
              padding: '8px 14px',
              borderBottom: '1px solid var(--divider)',
              fontSize: 10, fontWeight: 600, letterSpacing: '0.1em',
              color: 'var(--text-low)', textTransform: 'uppercase',
            }}>
              <span></span><span>Name</span><span>Mode</span><span>Songs</span><span>Modified</span><span></span>
            </div>
            {PROJECTS.map((p, i) => (
              <div key={i} style={{
                display: 'grid', gridTemplateColumns: '40px 2fr 1fr 1fr 1fr 100px',
                alignItems: 'center',
                padding: '10px 14px',
                borderBottom: i < PROJECTS.length - 1 ? '1px solid var(--divider)' : 'none',
                background: p.selected ? 'linear-gradient(90deg, rgba(34,230,255,0.06), transparent 70%)' : 'transparent',
                borderLeft: p.selected ? '2px solid var(--cyan)' : '2px solid transparent',
                marginLeft: -1, cursor: 'pointer',
              }}>
                <div style={{
                  width: 28, height: 28, borderRadius: 6,
                  background: `color-mix(in oklch, var(--${p.accent}) 14%, var(--bg-panel-3))`,
                  border: `1px solid color-mix(in oklch, var(--${p.accent}) 30%, transparent)`,
                  display: 'flex', alignItems: 'center', justifyContent: 'center',
                  boxShadow: p.selected ? `0 0 10px color-mix(in oklch, var(--${p.accent}) 50%, transparent)` : 'none',
                }}>
                  <Icon d={I.music} size={13} stroke={`var(--${p.accent})`} />
                </div>
                <div>
                  <div style={{ fontSize: 13, fontWeight: 500, color: 'var(--text-hi)' }}>{p.name}</div>
                  <div style={{ fontSize: 10, color: 'var(--text-low)', fontFamily: 'JetBrains Mono, monospace', marginTop: 2 }}>
                    Projects/{p.name}/
                  </div>
                </div>
                <div><Pill color={p.accent} small>{p.mode}</Pill></div>
                <div className="mono" style={{ fontSize: 11, color: 'var(--text-mid)' }}>{p.songs}</div>
                <div className="mono" style={{ fontSize: 11, color: 'var(--text-mid)' }}>{p.modified}</div>
                <div style={{ display: 'flex', justifyContent: 'flex-end', gap: 4 }}>
                  <IconBtn icon={I.chevR} size={24} />
                </div>
              </div>
            ))}
          </Panel>

          {/* Detail / Build panel for selected project */}
          <Panel style={{ width: 280, flexShrink: 0 }}>
            <ProjectDetail />
          </Panel>
          </div>
        </div>
      </div>
    </Surface>
  );
}

// ── Project detail / APK build ────────────────────────────────────────────
// Mirrors ProjectHub.cpp's right-side info + Package APK flow.

function ProjectDetail() {
  const [build, setBuild] = React.useState('idle'); // idle | running | done | error
  const [progress, setProgress] = React.useState(0);
  const [log, setLog] = React.useState([]);

  React.useEffect(() => {
    if (build !== 'running') return;
    setProgress(0);
    setLog(['$ ./build.sh BandoriSandbox']);
    const steps = [
      [ 8, 'Resolving project structure…'],
      [22, 'Compiling shaders (glslc)… 14 frag, 6 vert'],
      [38, 'Bundling chart assets (24 charts)…'],
      [54, 'Packaging audio (.ogg, .wav)…'],
      [70, 'Building native libs (arm64-v8a)…'],
      [84, 'Generating AndroidManifest.xml…'],
      [94, 'Signing APK with debug keystore…'],
      [100, '✓ APK built: BandoriSandbox-debug.apk (38.4 MB)'],
    ];
    let i = 0;
    const id = setInterval(() => {
      const [p, line] = steps[i];
      setProgress(p);
      setLog(prev => [...prev, line]);
      i++;
      if (i >= steps.length) {
        clearInterval(id);
        setTimeout(() => setBuild('done'), 300);
      }
    }, 380);
    return () => clearInterval(id);
  }, [build]);

  return (
    <div style={{ padding: 14, display: 'flex', flexDirection: 'column', gap: 12, height: '100%', minHeight: 0 }}>
      {/* Header */}
      <div style={{ display: 'flex', alignItems: 'center', gap: 10 }}>
        <div style={{
          width: 36, height: 36, borderRadius: 6,
          background: 'color-mix(in oklch, var(--cyan) 14%, var(--bg-panel-3))',
          border: '1px solid color-mix(in oklch, var(--cyan) 30%, transparent)',
          display: 'flex', alignItems: 'center', justifyContent: 'center',
          boxShadow: '0 0 12px color-mix(in oklch, var(--cyan) 40%, transparent)',
        }}>
          <Icon d={I.music} size={16} stroke="var(--cyan)" />
        </div>
        <div style={{ flex: 1, minWidth: 0 }}>
          <div style={{ fontSize: 13, fontWeight: 600, color: 'var(--text-hi)' }}>BandoriSandbox</div>
          <div style={{ fontSize: 10, color: 'var(--text-low)' }}>Drop 2D · 12 songs</div>
        </div>
      </div>

      {/* Metadata */}
      <div style={{
        background: 'var(--bg-panel-2)', border: '1px solid var(--border)',
        borderRadius: 6, padding: 10,
        display: 'flex', flexDirection: 'column', gap: 6,
      }}>
        {[
          ['Version',      '0.4.2'],
          ['Default chart','Forest Place / EXP'],
          ['Shader path',  'shaders/bandori.frag'],
          ['Last opened',  '2026-04-28 14:32'],
          ['Path',         'Projects/BandoriSandbox/'],
        ].map(([k, v]) => (
          <div key={k} style={{ display: 'flex', alignItems: 'baseline', gap: 8, fontSize: 11 }}>
            <span style={{ width: 88, color: 'var(--text-low)', textTransform: 'uppercase', letterSpacing: '0.04em', fontSize: 10 }}>{k}</span>
            <span className="mono" style={{ color: 'var(--text-mid)', flex: 1, textOverflow: 'ellipsis', overflow: 'hidden', whiteSpace: 'nowrap' }}>{v}</span>
          </div>
        ))}
      </div>

      {/* Open buttons */}
      <div style={{ display: 'flex', flexDirection: 'column', gap: 6 }}>
        <Btn kind="primary" size="md" icon={I.play} glow>Open Project</Btn>
        <div style={{ display: 'flex', gap: 6 }}>
          <Btn kind="ghost" size="sm" icon={I.folder}>Reveal</Btn>
          <Btn kind="ghost" size="sm" icon={I.upload}>Add file</Btn>
        </div>
      </div>

      {/* Build / package APK */}
      <div style={{
        marginTop: 4, padding: 12,
        background: 'rgba(255,179,71,0.04)',
        border: '1px solid color-mix(in oklch, var(--amber) 30%, var(--border))',
        borderRadius: 6,
        display: 'flex', flexDirection: 'column', gap: 8, minHeight: 0,
      }}>
        <div style={{ display: 'flex', alignItems: 'center', gap: 8 }}>
          <div style={{
            width: 22, height: 22, borderRadius: 4,
            background: 'rgba(255,179,71,0.15)',
            border: '1px solid var(--amber)',
            display: 'flex', alignItems: 'center', justifyContent: 'center',
            fontSize: 10, fontWeight: 700, color: 'var(--amber)', fontFamily: 'JetBrains Mono, monospace',
          }}>📦</div>
          <span style={{ fontSize: 11, fontWeight: 600, color: 'var(--text-hi)', textTransform: 'uppercase', letterSpacing: '0.08em' }}>Package APK</span>
          <div style={{ flex: 1 }}></div>
          {build === 'idle' && <Btn kind="outline" size="sm" icon={I.download} onClick={() => setBuild('running')}>Build</Btn>}
          {build === 'running' && <span className="mono" style={{ fontSize: 10, color: 'var(--amber)' }}>● BUILDING</span>}
          {build === 'done' && <Btn kind="success" size="sm" icon={I.check} onClick={() => setBuild('idle')}>Done</Btn>}
        </div>

        {build !== 'idle' && (
          <>
            <div style={{ height: 4, borderRadius: 2, background: 'var(--bg-panel-3)', overflow: 'hidden' }}>
              <div style={{
                width: progress + '%', height: '100%',
                background: build === 'done' ? 'var(--lime)' : 'var(--amber)',
                boxShadow: '0 0 8px ' + (build === 'done' ? 'var(--lime)' : 'var(--amber)'),
                transition: 'width 0.3s',
              }}></div>
            </div>
            <div className="mono" style={{
              flex: 1, minHeight: 0, overflow: 'auto',
              padding: 8, fontSize: 10, lineHeight: 1.5,
              background: '#000', borderRadius: 4,
              border: '1px solid var(--border)',
              color: 'var(--text-mid)',
              maxHeight: 160,
            }}>
              {log.map((line, i) => (
                <div key={i} style={{
                  color: line.startsWith('$') ? 'var(--cyan)'
                       : line.startsWith('✓') ? 'var(--lime)'
                       : 'var(--text-mid)',
                }}>{line}</div>
              ))}
            </div>
          </>
        )}
      </div>
    </div>
  );
}

window.ProjectHub = ProjectHub;