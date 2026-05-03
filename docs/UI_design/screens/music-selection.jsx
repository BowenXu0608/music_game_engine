// Music Selection Editor — Arcaea-style wheel + page bg + frosted overlay

const SETS = ['NEON', 'AURORA', 'ASTRAL', 'MIDNIGHT'];
const SONGS = [
  { name: 'Forest Place',    artist: 'Daira',         diff: 'EXP 9.7',  fc: true,  ap: true,  selected: true,  cover: 'magenta' },
  { name: 'Mana Kanagawa',   artist: 'Funky DL',      diff: 'HRD 8.4',  fc: true,  ap: false, cover: 'cyan' },
  { name: 'White Dove',      artist: '伍佰',           diff: 'NRM 6.0',  fc: false, ap: false, cover: 'amber' },
  { name: 'Lunar Tide',      artist: 'Voca-Lab',      diff: 'EXP 10.2', fc: true,  ap: true,  cover: 'lime' },
  { name: 'Stellar Drift',   artist: 'NebulaeFM',     diff: 'HRD 9.0',  fc: false, ap: false, cover: 'magenta' },
  { name: 'Glass Memory',    artist: 'arcrole',       diff: 'EXP 11.5', fc: true,  ap: false, cover: 'cyan' },
];

function MusicSelectionEditor() {
  return (
    <Surface>
      <TopBar
        crumbs={['BandoriSandbox', 'Music Selection']}
        right={
          <div style={{ display: 'flex', gap: 8, alignItems: 'center' }}>
            <Btn kind="ghost" size="sm" icon={I.chevL}>Start Screen</Btn>
            <Btn kind="ghost" size="sm" iconRight={I.chevR}>Settings</Btn>
            <div style={{ width: 1, height: 20, background: 'var(--border)' }}></div>
            <label style={{ display: 'flex', alignItems: 'center', gap: 6, fontSize: 11, color: 'var(--text-mid)', cursor: 'pointer' }}>
              <Toggle value={false} /> Auto-Play
            </label>
            <Btn kind="success" size="md" icon={I.play} glow>Test Game</Btn>
          </div>
        }
      />

      <div style={{ flex: 1, display: 'flex', minHeight: 0 }}>
        {/* Hierarchy / sets / songs */}
        <div style={{ width: 240, borderRight: '1px solid var(--border)', display: 'flex', flexDirection: 'column' }}>
          <div style={{ padding: 8, borderBottom: '1px solid var(--divider)' }}>
            <div style={{ display: 'flex', gap: 4 }}>
              <Btn kind="primary" size="sm" icon={I.plus}>Set</Btn>
              <Btn kind="outline" size="sm" icon={I.plus}>Song</Btn>
              <div style={{ flex: 1 }}></div>
              <IconBtn icon={I.search} size={24} />
            </div>
          </div>
          <div style={{ flex: 1, overflow: 'auto', padding: 4 }}>
            <SectionHeader label="Sets" right={<Pill color="cyan" small>4</Pill>} />
            {SETS.map((s, i) => (
              <SidebarItem
                key={s}
                icon={I.folder}
                label={s}
                active={i === 0}
                hasChildren expanded={i === 0}
                badge={i === 0 ? '6 songs' : (i+1)*3 + ' songs'}
              />
            ))}
            {SONGS.map((sg, i) => (
              <SidebarItem
                key={sg.name}
                icon={I.music}
                label={sg.name}
                indent={1}
                active={sg.selected}
                color={`var(--${sg.cover})`}
              />
            ))}

            <SectionHeader label="Page" />
            <SidebarItem icon={I.image} label="Background" indent={1} />
            <SidebarItem icon={I.badge} label="FC Badge" indent={1} />
            <SidebarItem icon={I.badge} label="AP Badge" indent={1} />
          </div>
        </div>

        {/* Preview */}
        <div style={{ flex: 1, display: 'flex', flexDirection: 'column', background: 'var(--bg-base)', minWidth: 0 }}>
          <div style={{ height: 36, padding: '0 12px', display: 'flex', alignItems: 'center', gap: 10, borderBottom: '1px solid var(--border)' }}>
            <div style={{ display: 'flex', gap: 0, background: 'var(--bg-panel)', borderRadius: 5, padding: 2 }}>
              <Btn kind="ghost" size="sm" active>16:9</Btn>
              <Btn kind="ghost" size="sm">21:9</Btn>
            </div>
            <span className="mono" style={{ fontSize: 10, color: 'var(--text-low)' }}>1920×1080</span>
            <div style={{ flex: 1 }}></div>
            <Btn kind="ghost" size="sm" icon={I.eye} active>Preview badges</Btn>
          </div>

          <div style={{ flex: 1, padding: 24, display: 'flex', alignItems: 'center', justifyContent: 'center' }}>
            <div style={{
              aspectRatio: '16/9', maxHeight: '100%', maxWidth: '100%', width: '100%',
              borderRadius: 6, overflow: 'hidden', position: 'relative',
              background: `linear-gradient(135deg, #0a1124 0%, #2a0d2e 50%, #0a0a1f 100%)`,
            }}>
              {/* Cover art bg */}
              <div style={{ position: 'absolute', inset: 0,
                background: 'radial-gradient(circle at 30% 50%, rgba(255,61,240,0.4), transparent 50%), radial-gradient(circle at 70% 30%, rgba(34,230,255,0.3), transparent 50%)',
                filter: 'blur(40px)' }}></div>
              {/* Frosted bands */}
              <div style={{ position: 'absolute', left: 0, top: 0, bottom: 0, width: '18%', background: 'rgba(0,0,0,0.7)', backdropFilter: 'blur(20px)' }}></div>
              <div style={{ position: 'absolute', right: 0, top: 0, bottom: 0, width: '18%', background: 'rgba(0,0,0,0.7)', backdropFilter: 'blur(20px)' }}></div>
              <div style={{ position: 'absolute', left: '18%', top: 0, bottom: 0, width: '64%', background: 'rgba(0,0,0,0.25)' }}></div>
              <div style={{ position: 'absolute', left: '18%', top: 0, bottom: 0, width: 1, background: 'rgba(0,0,0,0.6)' }}></div>
              <div style={{ position: 'absolute', right: '18%', top: 0, bottom: 0, width: 1, background: 'rgba(0,0,0,0.6)' }}></div>

              {/* Center: cover + difficulty */}
              <div style={{ position: 'absolute', left: '50%', top: '50%', transform: 'translate(-50%,-50%)', display: 'flex', flexDirection: 'column', alignItems: 'center', gap: 18 }}>
                <div style={{
                  width: 220, height: 220, borderRadius: 8,
                  background: 'linear-gradient(135deg, #ff3df0, #22e6ff)',
                  position: 'relative', overflow: 'hidden',
                  boxShadow: '0 0 40px rgba(255,61,240,0.5), 0 20px 60px rgba(0,0,0,0.6)',
                  border: '2px solid rgba(255,255,255,0.15)',
                }}>
                  <div style={{ position: 'absolute', inset: 0, background: 'radial-gradient(circle at 70% 30%, rgba(255,255,255,0.4), transparent 50%)' }}></div>
                  <div style={{ position: 'absolute', bottom: 14, left: 14, fontSize: 22, fontWeight: 700, color: '#fff', letterSpacing: '-0.02em' }}>Forest<br/>Place</div>
                </div>
                {/* Difficulty pills */}
                <div style={{ display: 'flex', gap: 6 }}>
                  {[
                    { l: 'EZ', n: '5.0', c: 'lime' },
                    { l: 'NM', n: '7.4', c: 'cyan' },
                    { l: 'HD', n: '9.7', c: 'magenta', active: true },
                  ].map((d, i) => (
                    <div key={i} style={{
                      padding: '6px 12px', borderRadius: 4,
                      background: d.active ? `var(--${d.c})` : `color-mix(in oklch, var(--${d.c}) 15%, var(--bg-panel-3))`,
                      color: d.active ? '#000' : `var(--${d.c})`,
                      border: `1px solid var(--${d.c})`,
                      boxShadow: d.active ? `0 0 16px var(--${d.c})` : 'none',
                      display: 'flex', flexDirection: 'column', alignItems: 'center', minWidth: 50,
                    }}>
                      <span style={{ fontSize: 10, fontWeight: 700, letterSpacing: '0.1em' }}>{d.l}</span>
                      <span className="mono" style={{ fontSize: 16, fontWeight: 700 }}>{d.n}</span>
                    </div>
                  ))}
                </div>
                <Btn kind="primary" size="lg" icon={I.play} glow>START</Btn>
              </div>

              {/* Right: song wheel cards */}
              <div style={{ position: 'absolute', right: '4%', top: '8%', bottom: '8%', width: '34%', display: 'flex', flexDirection: 'column', gap: 8, justifyContent: 'center' }}>
                {SONGS.slice(0, 5).map((s, i) => (
                  <div key={i} style={{
                    height: 64, borderRadius: 6,
                    background: s.selected ? 'rgba(0,0,0,0.85)' : 'rgba(0,0,0,0.55)',
                    border: '1px solid ' + (s.selected ? 'var(--cyan)' : 'rgba(255,255,255,0.08)'),
                    boxShadow: s.selected ? '0 0 18px rgba(34,230,255,0.5)' : 'none',
                    display: 'flex', alignItems: 'center', padding: 8, gap: 10,
                    transform: s.selected ? 'translateX(-12px) scale(1.03)' : 'none',
                    transition: 'all 0.2s',
                  }}>
                    <div style={{ width: 48, height: 48, borderRadius: 4,
                      background: `linear-gradient(135deg, var(--${s.cover}), var(--bg-panel-3))`,
                      flex: '0 0 auto', boxShadow: 'inset 0 0 0 1px rgba(255,255,255,0.1)' }}></div>
                    <div style={{ flex: 1, minWidth: 0 }}>
                      <div style={{ fontSize: 12, fontWeight: 600, color: 'var(--text-hi)', whiteSpace: 'nowrap', overflow: 'hidden', textOverflow: 'ellipsis' }}>{s.name}</div>
                      <div className="mono" style={{ fontSize: 10, color: 'var(--text-low)' }}>{s.artist} · {s.diff}</div>
                    </div>
                    {/* Diamond badges */}
                    {[s.fc, s.ap].map((unlocked, j) => (
                      <div key={j} style={{
                        width: 24, height: 24, transform: 'rotate(45deg)',
                        background: unlocked ? (j === 0 ? 'rgba(34,230,255,0.3)' : 'rgba(255,179,71,0.3)') : 'rgba(255,255,255,0.05)',
                        border: '1px solid ' + (unlocked ? (j === 0 ? 'var(--cyan)' : 'var(--amber)') : 'rgba(255,255,255,0.15)'),
                        boxShadow: unlocked ? `0 0 8px ${j===0?'var(--cyan)':'var(--amber)'}` : 'none',
                      }}></div>
                    ))}
                  </div>
                ))}
              </div>

              {/* Top bar in mock */}
              <div style={{ position: 'absolute', top: 16, left: 16, right: 16, display: 'flex', alignItems: 'center', gap: 12 }}>
                <Pill color="cyan" small>{SETS[0]}</Pill>
                <span style={{ fontSize: 11, color: 'rgba(255,255,255,0.7)' }}>6 songs · 24 charts</span>
                <div style={{ flex: 1 }}></div>
                <IconBtn icon={I.settings} size={28} color="rgba(255,255,255,0.8)" />
              </div>
            </div>
          </div>
        </div>

        {/* Properties */}
        <div style={{ width: 280, borderLeft: '1px solid var(--border)', display: 'flex', flexDirection: 'column' }}>
          <Tabs items={[{ id: 'p', label: 'Properties' }, { id: 'a', label: 'Assets' }]} value="p" onChange={()=>{}} />
          <div style={{ flex: 1, overflow: 'auto' }}>
            <SectionHeader label="Song" />
            <div style={{ padding: '0 12px 12px', display: 'flex', flexDirection: 'column', gap: 8 }}>
              <Field label="Name" value="Forest Place" />
              <Field label="Artist" value="Daira" />
              <div>
                <div style={{ fontSize: 11, color: 'var(--text-low)', marginBottom: 4 }}>Cover</div>
                <Placeholder label="cover_art.png" h={88} color="magenta" />
              </div>
              <Field label="Audio" value="forest_place.mp3" mono />
            </div>

            <SectionHeader label="Preview Clip" right={<Btn kind="ghost" size="sm" icon={I.sparkle}>Auto</Btn>} />
            <div style={{ padding: '0 12px 12px', display: 'flex', flexDirection: 'column', gap: 8 }}>
              <Slider label="Start" value={42.5} min={0} max={180} suffix="s" />
              <Slider label="Length" value={30} min={10} max={45} suffix="s" />
              <div className="mono" style={{ fontSize: 10, color: 'var(--text-low)' }}>42.5s → 72.5s (song: 3:12)</div>
            </div>

            <SectionHeader label="Charts" />
            <div style={{ padding: '0 12px 12px', display: 'flex', flexDirection: 'column', gap: 6 }}>
              {[
                { l: 'Easy',   v: '5.0',  c: 'lime',    file: 'easy.json',    score: '—',         ach: '—'  },
                { l: 'Medium', v: '7.4',  c: 'cyan',    file: 'medium.json',  score: '892,310',   ach: 'A'  },
                { l: 'Hard',   v: '9.7',  c: 'magenta', file: 'hard.json',    score: '1,001,200', ach: 'S+' },
              ].map((d, i) => (
                <div key={i} style={{
                  padding: '6px 8px', borderRadius: 4,
                  background: 'var(--bg-panel-2)',
                  border: '1px solid var(--border)',
                  display: 'flex', flexDirection: 'column', gap: 4,
                }}>
                  <div style={{ display: 'flex', alignItems: 'center', gap: 8 }}>
                    <Pill color={d.c} solid small>{d.l}</Pill>
                    <span className="mono" style={{ fontSize: 11, color: 'var(--text-hi)' }}>{d.v}</span>
                    <div style={{ flex: 1 }}></div>
                    <span className="mono" style={{ fontSize: 10, color: 'var(--text-low)' }}>{d.score}</span>
                    <Pill color={d.ach === '—' ? 'cyan' : 'amber'} small style={d.ach === '—' ? { opacity: 0.4 } : null}>{d.ach}</Pill>
                  </div>
                  <div className="mono" style={{ fontSize: 10, color: 'var(--text-low)', display: 'flex', alignItems: 'center', gap: 4 }}>
                    <span style={{ opacity: 0.5 }}>📄</span>{d.file}
                  </div>
                </div>
              ))}
            </div>

            <SectionHeader label="Game Mode" />
            <div style={{ padding: '0 12px 12px', display: 'flex', flexDirection: 'column', gap: 8 }}>
              <div>
                <div style={{ fontSize: 10, color: 'var(--text-low)', marginBottom: 3, textTransform: 'uppercase', letterSpacing: '0.06em' }}>Mode</div>
                <div style={{ display: 'flex', gap: 0, background: 'var(--bg-panel)', borderRadius: 4, padding: 2, border: '1px solid var(--border)' }}>
                  {['2D','3D','Circle','Scan'].map((m,i) => (
                    <div key={m} style={{
                      flex: 1, textAlign: 'center', padding: '4px 0', borderRadius: 3,
                      fontSize: 10, fontWeight: 600, cursor: 'pointer',
                      background: i === 1 ? 'var(--bg-elev)' : 'transparent',
                      color: i === 1 ? 'var(--magenta)' : 'var(--text-low)',
                    }}>{m}</div>
                  ))}
                </div>
              </div>
              <Slider label="Tracks" value={4} min={3} max={8} />
              <Slider label="Sky Height" value={1.4} min={0.5} max={3.0} />
              <div style={{ display: 'flex', gap: 6 }}>
                <Field label="Disk Inner" value="0.30" mono />
                <Field label="Ring Gap"   value="0.18" mono />
              </div>
              <div style={{ fontSize: 10, color: 'var(--text-low)', textTransform: 'uppercase', letterSpacing: '0.06em', marginTop: 4 }}>Judgment Windows (ms)</div>
              <div style={{ display: 'flex', gap: 4 }}>
                <Field label="Perfect" value="±33"  mono />
                <Field label="Great"   value="±80"  mono />
                <Field label="Good"    value="±140" mono />
              </div>
            </div>

            <SectionHeader label="HUD" />
            <div style={{ padding: '0 12px 12px', display: 'flex', flexDirection: 'column', gap: 8 }}>
              <div style={{ fontSize: 10, color: 'var(--text-low)', textTransform: 'uppercase', letterSpacing: '0.08em' }}>Score</div>
              <div style={{ display: 'flex', gap: 6 }}>
                <Field label="X" value="0.92" mono />
                <Field label="Y" value="0.08" mono />
              </div>
              <Slider label="Size" value={36} min={12} max={96} suffix="px" />
              <div style={{ height: 1, background: 'var(--border)', margin: '4px 0' }}></div>
              <div style={{ fontSize: 10, color: 'var(--text-low)', textTransform: 'uppercase', letterSpacing: '0.08em' }}>Combo</div>
              <div style={{ display: 'flex', gap: 6 }}>
                <Field label="X" value="0.50" mono />
                <Field label="Y" value="0.18" mono />
              </div>
              <Slider label="Size" value={48} min={12} max={120} suffix="px" />
            </div>
          </div>
        </div>
      </div>
    </Surface>
  );
}

window.MusicSelectionEditor = MusicSelectionEditor;
