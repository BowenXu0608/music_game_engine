// Player Settings — 8 settings, scrim modal style

function PlayerSettings() {
  return (
    <Surface>
      <TopBar
        crumbs={['BandoriSandbox', 'Player Settings']}
        right={
          <div style={{ display: 'flex', gap: 8, alignItems: 'center' }}>
            <Btn kind="ghost" size="sm" icon={I.chevL}>Music Selection</Btn>
            <Btn kind="success" size="md" icon={I.play} glow>Test Game</Btn>
          </div>
        }
      />
      <div style={{ flex: 1, position: 'relative', display: 'flex', alignItems: 'center', justifyContent: 'center', overflow: 'hidden', background: 'radial-gradient(ellipse at 30% 30%, #0a0518 0%, #000 70%)' }}>
        {/* Faded preview behind scrim */}
        <div style={{ position: 'absolute', inset: 0, opacity: 0.18, filter: 'blur(2px)' }}>
          <div style={{ position: 'absolute', top: '20%', left: '50%', transform: 'translateX(-50%)', width: 500, height: 500, borderRadius: '50%', background: 'radial-gradient(circle, rgba(34,230,255,0.5), transparent 60%)', filter: 'blur(40px)' }}></div>
          <div style={{ position: 'absolute', bottom: '10%', right: '15%', width: 300, height: 300, borderRadius: '50%', background: 'radial-gradient(circle, rgba(255,61,240,0.4), transparent 60%)', filter: 'blur(30px)' }}></div>
        </div>

        {/* Card */}
        <div style={{
          width: 640, maxHeight: '90%',
          background: 'rgba(10,10,14,0.92)', backdropFilter: 'blur(20px)',
          border: '1px solid var(--border-hi)', borderRadius: 12,
          boxShadow: '0 30px 80px rgba(0,0,0,0.6), 0 0 40px rgba(34,230,255,0.1)',
          display: 'flex', flexDirection: 'column', overflow: 'hidden',
        }}>
          <div style={{ padding: '20px 24px', borderBottom: '1px solid var(--divider)', display: 'flex', alignItems: 'center', gap: 10 }}>
            <Icon d={I.settings} size={18} stroke="var(--cyan)" />
            <div style={{ fontSize: 18, fontWeight: 600 }}>Settings</div>
            <div style={{ flex: 1 }}></div>
            <IconBtn icon={I.close} size={28} />
          </div>

          <div style={{ flex: 1, overflow: 'auto', padding: 20, display: 'flex', flexDirection: 'column', gap: 18 }}>
            {/* Section: Audio */}
            <div>
              <div style={{ fontSize: 10, fontWeight: 700, letterSpacing: '0.15em', color: 'var(--cyan)', marginBottom: 12, textTransform: 'uppercase' }}>Audio</div>
              <div style={{ display: 'flex', flexDirection: 'column', gap: 14 }}>
                <Slider label="Music volume" value={0.82} min={0} max={1} suffix="" accent="cyan" />
                <Slider label="Hit-sound volume" value={0.65} min={0} max={1} accent="cyan" />
                <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center' }}>
                  <span style={{ fontSize: 12, color: 'var(--text-mid)' }}>Hit sounds enabled</span>
                  <Toggle value={true} />
                </div>
                <div>
                  <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center', marginBottom: 4 }}>
                    <span style={{ fontSize: 12, color: 'var(--text-mid)' }}>Audio offset</span>
                    <span className="mono" style={{ fontSize: 11, color: 'var(--text-low)' }}>−12 ms</span>
                  </div>
                  <Slider value={-12} min={-200} max={200} suffix=" ms" accent="magenta" />
                </div>

                {/* Calibration wizard */}
                <CalibrationWizard />
              </div>
            </div>

            <div style={{ height: 1, background: 'var(--divider)' }}></div>

            {/* Section: Gameplay */}
            <div>
              <div style={{ fontSize: 10, fontWeight: 700, letterSpacing: '0.15em', color: 'var(--magenta)', marginBottom: 12, textTransform: 'uppercase' }}>Gameplay</div>
              <div style={{ display: 'flex', flexDirection: 'column', gap: 6 }}>
                <Slider label="Note speed" value={5.0} min={1} max={10} accent="magenta" />
                <div style={{ fontSize: 10, color: 'var(--text-low)', marginTop: -2 }}>5 = default · Scan Line mode ignores this</div>
              </div>
            </div>

            <div style={{ height: 1, background: 'var(--divider)' }}></div>

            {/* Section: Visual */}
            <div>
              <div style={{ fontSize: 10, fontWeight: 700, letterSpacing: '0.15em', color: 'var(--lime)', marginBottom: 12, textTransform: 'uppercase' }}>Visual</div>
              <div style={{ display: 'flex', flexDirection: 'column', gap: 14 }}>
                <Slider label="Background dim" value={0.45} min={0} max={1} accent="lime" />
                <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center' }}>
                  <span style={{ fontSize: 12, color: 'var(--text-mid)' }}>Show FPS counter</span>
                  <Toggle value={false} />
                </div>
              </div>
            </div>

            <div style={{ height: 1, background: 'var(--divider)' }}></div>

            {/* Section: Misc */}
            <div>
              <div style={{ fontSize: 10, fontWeight: 700, letterSpacing: '0.15em', color: 'var(--amber)', marginBottom: 12, textTransform: 'uppercase' }}>Misc</div>
              <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center' }}>
                <span style={{ fontSize: 12, color: 'var(--text-mid)' }}>Language</span>
                <div style={{ display: 'flex', gap: 4 }}>
                  {['en', 'zh', 'ja', 'ko'].map((l, i) => (
                    <Btn key={l} kind="ghost" size="sm" active={i === 0}>{l}</Btn>
                  ))}
                </div>
              </div>
            </div>
          </div>

          <div style={{ padding: 14, borderTop: '1px solid var(--divider)', display: 'flex', gap: 8, justifyContent: 'flex-end' }}>
            <Btn kind="ghost" size="md">Cancel</Btn>
            <Btn kind="primary" size="md" glow icon={I.check}>Apply</Btn>
          </div>
        </div>
      </div>
    </Surface>
  );
}

// ── Calibration wizard ──────────────────────────────────────────────────────
// Mirrors drawCalibrationPanel(): idle → 4-beat play with progress + tap zone
// → average offset → Accept / Retry / Cancel. Cycles for the demo.

function CalibrationWizard() {
  const [phase, setPhase] = React.useState('idle');     // idle | run | result
  const [beat, setBeat] = React.useState(0);            // 0..4 ticked
  const [taps, setTaps] = React.useState([]);           // ms offsets
  const startRef = React.useRef(0);

  const period = 600; // ms — matches the 100bpm-ish source default
  const beatsTotal = 4;

  React.useEffect(() => {
    if (phase !== 'run') return;
    startRef.current = performance.now();
    setBeat(0);
    setTaps([]);
    let raf;
    const tick = () => {
      const t = performance.now() - startRef.current;
      const ticked = Math.min(beatsTotal, Math.floor(t / period));
      setBeat(ticked);
      if (t >= (beatsTotal + 1) * period) {
        // simulate a few taps with small offsets
        setTaps([-18, -12, -8, -15]);
        setPhase('result');
        return;
      }
      raf = requestAnimationFrame(tick);
    };
    raf = requestAnimationFrame(tick);
    return () => cancelAnimationFrame(raf);
  }, [phase]);

  const avg = taps.length ? taps.reduce((a, b) => a + b, 0) / taps.length : 0;

  if (phase === 'idle') {
    return (
      <div style={{
        padding: 12, borderRadius: 6,
        background: 'rgba(34,230,255,0.04)',
        border: '1px solid var(--border-hi)',
      }}>
        <div style={{ display: 'flex', alignItems: 'flex-start', gap: 10 }}>
          <Icon d={I.tap} size={18} stroke="var(--cyan)" />
          <div style={{ flex: 1 }}>
            <div style={{ fontSize: 12, fontWeight: 600, color: 'var(--text-hi)', marginBottom: 2 }}>Tap Calibration</div>
            <div style={{ fontSize: 11, color: 'var(--text-mid)', lineHeight: 1.4, marginBottom: 8 }}>
              You'll hear 4 beats. Tap on each one. We average your offset.
            </div>
            <Btn kind="primary" size="sm" icon={I.play} onClick={() => setPhase('run')}>Start Calibration</Btn>
          </div>
        </div>
      </div>
    );
  }

  if (phase === 'run') {
    return (
      <div style={{
        padding: 14, borderRadius: 6,
        background: 'rgba(34,230,255,0.06)',
        border: '1px solid var(--cyan)',
        boxShadow: '0 0 20px rgba(34,230,255,0.1)',
      }}>
        <div style={{ fontSize: 11, color: 'var(--cyan)', textTransform: 'uppercase', letterSpacing: '0.12em', marginBottom: 10, fontWeight: 700 }}>● Calibrating · beat {beat}/{beatsTotal}</div>
        {/* Beat dots */}
        <div style={{ display: 'flex', gap: 8, marginBottom: 12 }}>
          {[0,1,2,3].map(i => (
            <div key={i} style={{
              flex: 1, height: 8, borderRadius: 4,
              background: i < beat ? 'var(--cyan)' : 'rgba(255,255,255,0.08)',
              boxShadow: i < beat ? '0 0 8px var(--cyan)' : 'none',
              transition: 'all 0.1s',
            }}></div>
          ))}
        </div>
        {/* Tap zone */}
        <div style={{
          height: 56, borderRadius: 4,
          background: 'rgba(34,230,255,0.08)',
          border: '1px dashed var(--cyan)',
          display: 'flex', alignItems: 'center', justifyContent: 'center',
          fontSize: 12, fontWeight: 600, color: 'var(--cyan)',
          letterSpacing: '0.2em', textTransform: 'uppercase',
        }}>Tap Here on Each Beat</div>
      </div>
    );
  }

  // result
  return (
    <div style={{
      padding: 14, borderRadius: 6,
      background: 'rgba(132,235,89,0.06)',
      border: '1px solid var(--lime)',
    }}>
      <div style={{ fontSize: 11, color: 'var(--lime)', textTransform: 'uppercase', letterSpacing: '0.12em', marginBottom: 8, fontWeight: 700 }}>✓ Calibration Result</div>
      <div style={{ display: 'flex', alignItems: 'baseline', gap: 8, marginBottom: 4 }}>
        <span style={{ fontSize: 11, color: 'var(--text-mid)' }}>Average tap offset:</span>
        <span className="mono" style={{ fontSize: 18, fontWeight: 700, color: 'var(--text-hi)' }}>
          {avg > 0 ? '+' : ''}{avg.toFixed(1)} ms
        </span>
      </div>
      <div className="mono" style={{ fontSize: 10, color: 'var(--text-low)', marginBottom: 10 }}>
        samples: {taps.map(t => (t > 0 ? '+' : '') + t).join(', ')}
      </div>
      <div style={{ display: 'flex', gap: 6 }}>
        <Btn kind="success" size="sm" icon={I.check} onClick={() => setPhase('idle')}>Accept</Btn>
        <Btn kind="ghost"   size="sm" onClick={() => setPhase('run')}>Retry</Btn>
        <Btn kind="ghost"   size="sm" onClick={() => setPhase('idle')}>Cancel</Btn>
      </div>
    </div>
  );
}

window.PlayerSettings = PlayerSettings;
