// Shared UI primitives for Music Game Engine editor mockups

const Icon = ({ d, size = 14, stroke = 'currentColor', fill = 'none', sw = 1.5, style }) => (
  <svg width={size} height={size} viewBox="0 0 24 24" fill={fill} stroke={stroke} strokeWidth={sw}
    strokeLinecap="round" strokeLinejoin="round" style={{ flex: '0 0 auto', ...style }}>
    {typeof d === 'string' ? <path d={d} /> : d}
  </svg>
);

// Icon paths — outline, monoline. Built inline.
const I = {
  folder: 'M3 7a2 2 0 0 1 2-2h4l2 2h8a2 2 0 0 1 2 2v9a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2V7z',
  music:  'M9 18V5l12-2v13 M9 18a3 3 0 1 1-6 0 3 3 0 0 1 6 0z M21 16a3 3 0 1 1-6 0 3 3 0 0 1 6 0z',
  layers: 'M12 3 2 8l10 5 10-5-10-5z M2 17l10 5 10-5 M2 12.5l10 5 10-5',
  image:  'M3 5h18v14H3z M3 16l5-5 5 5 4-4 4 4 M9 9.5a1.5 1.5 0 1 1-3 0 1.5 1.5 0 0 1 3 0z',
  film:   'M3 4h18v16H3z M7 4v16 M17 4v16 M3 8h4 M3 12h4 M3 16h4 M17 8h4 M17 12h4 M17 16h4',
  audio:  'M11 5 6 9H2v6h4l5 4V5z M16 9a3 3 0 0 1 0 6 M19 6a7 7 0 0 1 0 12',
  play:   'M8 5v14l11-7z',
  pause:  'M6 4h4v16H6z M14 4h4v16h-4z',
  stop:   'M5 5h14v14H5z',
  prev:   'M19 20 9 12l10-8v16z M5 4v16',
  next:   'M5 4l10 8-10 8V4z M19 4v16',
  plus:   'M12 5v14 M5 12h14',
  search: 'M11 19a8 8 0 1 1 0-16 8 8 0 0 1 0 16zm6.5-2.5L22 21',
  settings:'M12 8a4 4 0 1 0 0 8 4 4 0 0 0 0-8z M19.4 15a1.7 1.7 0 0 0 .3 1.8l.1.1a2 2 0 1 1-2.8 2.8l-.1-.1a1.7 1.7 0 0 0-1.8-.3 1.7 1.7 0 0 0-1 1.5V21a2 2 0 1 1-4 0v-.1a1.7 1.7 0 0 0-1.1-1.5 1.7 1.7 0 0 0-1.8.3l-.1.1a2 2 0 1 1-2.8-2.8l.1-.1a1.7 1.7 0 0 0 .3-1.8 1.7 1.7 0 0 0-1.5-1H3a2 2 0 1 1 0-4h.1a1.7 1.7 0 0 0 1.5-1.1 1.7 1.7 0 0 0-.3-1.8l-.1-.1a2 2 0 1 1 2.8-2.8l.1.1a1.7 1.7 0 0 0 1.8.3h.1a1.7 1.7 0 0 0 1-1.5V3a2 2 0 1 1 4 0v.1a1.7 1.7 0 0 0 1 1.5 1.7 1.7 0 0 0 1.8-.3l.1-.1a2 2 0 1 1 2.8 2.8l-.1.1a1.7 1.7 0 0 0-.3 1.8v.1a1.7 1.7 0 0 0 1.5 1H21a2 2 0 1 1 0 4h-.1a1.7 1.7 0 0 0-1.5 1z',
  chevR:  'M9 6l6 6-6 6',
  chevL:  'M15 6l-6 6 6 6',
  chevD:  'M6 9l6 6 6-6',
  chevU:  'M18 15l-6-6-6 6',
  close:  'M6 6l12 12 M18 6 6 18',
  check:  'M5 12l5 5L20 6',
  bolt:   'M13 2 3 14h7l-1 8 10-12h-7l1-8z',
  sparkle:'M12 3v6 M12 15v6 M3 12h6 M15 12h6 M5.6 5.6l4.2 4.2 M14.2 14.2l4.2 4.2 M5.6 18.4l4.2-4.2 M14.2 9.8l4.2-4.2',
  wave:   'M2 12h2l2-7 4 14 4-10 4 6 2-3 2 0',
  eye:    'M2 12s4-7 10-7 10 7 10 7-4 7-10 7S2 12 2 12z M12 9a3 3 0 1 1 0 6 3 3 0 0 1 0-6z',
  lock:   'M5 11h14v10H5z M8 11V7a4 4 0 1 1 8 0v4',
  trash:  'M4 6h16 M9 6V4h6v2 M6 6l1 14h10l1-14',
  download:'M12 3v12 M7 10l5 5 5-5 M5 21h14',
  upload: 'M12 21V9 M7 14l5-5 5 5 M5 3h14',
  refresh:'M21 12a9 9 0 1 1-3-6.7L21 8 M21 3v5h-5',
  star:   'M12 3l3 6 6 1-4.5 4.5L18 21l-6-3-6 3 1.5-6.5L3 10l6-1z',
  diamond:'M12 3 22 12 12 21 2 12z',
  arrow:  'M5 12h14 M13 6l6 6-6 6',
  drag:   'M9 6h0 M15 6h0 M9 12h0 M15 12h0 M9 18h0 M15 18h0',
  copy:   'M9 9h12v12H9z M5 15H3V3h12v2',
  scissor:'M6 9l12 6 M6 15l12-6 M6 9a3 3 0 1 1 0-6 3 3 0 0 1 0 6z M6 21a3 3 0 1 0 0-6 3 3 0 0 0 0 6z',
  zoom:   'M11 19a8 8 0 1 1 0-16 8 8 0 0 1 0 16zm6.5-2.5L22 21 M8 11h6 M11 8v6',
  zoomout:'M11 19a8 8 0 1 1 0-16 8 8 0 0 1 0 16zm6.5-2.5L22 21 M8 11h6',
  arc:    'M4 18C4 9 11 4 20 4',
  flick:  'M5 19l14-14 M19 5h-7 M19 5v7',
  hold:   'M4 12h16 M4 8h16 M4 16h16',
  tap:    'M12 5v8 M12 13l-4 4 M12 13l4 4 M9 5h6',
  slide:  'M4 12h16 M16 8l4 4-4 4 M4 8l-2 4 2 4',
  mic:    'M12 2a3 3 0 0 0-3 3v6a3 3 0 0 0 6 0V5a3 3 0 0 0-3-3z M19 11a7 7 0 0 1-14 0 M12 18v4',
  globe:  'M12 3a9 9 0 1 0 0 18 9 9 0 0 0 0-18z M3 12h18 M12 3a14 14 0 0 1 0 18 14 14 0 0 1 0-18z',
  filter: 'M3 5h18l-7 9v6l-4-2v-4z',
  android:'M7 9V6a5 5 0 0 1 10 0v3 M5 11h14v8H5z M9 11V8 M15 11V8 M9 19v3 M15 19v3',
  send:   'M3 11l18-8-8 18-2-7-8-3z',
  brain:  'M9 5a3 3 0 0 0-3 3 3 3 0 0 0-3 3 3 3 0 0 0 3 3 3 3 0 0 0 3 3 3 3 0 0 0 3-3V8a3 3 0 0 0-3-3z M15 5a3 3 0 0 1 3 3 3 3 0 0 1 3 3 3 3 0 0 1-3 3 3 3 0 0 1-3 3 3 3 0 0 1-3-3V8a3 3 0 0 1 3-3z',
  badge:  'M12 2 4 6v6c0 5 3 9 8 10 5-1 8-5 8-10V6l-8-4z',
  flag:   'M4 21V4 M4 4h12l-2 4 2 4H4',
};

// ───────────── Window chrome ─────────────
const WindowChrome = ({ title, right, children, height }) => (
  <div style={{
    background: 'var(--bg-base)',
    border: '1px solid var(--border)',
    borderRadius: 10,
    overflow: 'hidden',
    display: 'flex',
    flexDirection: 'column',
    height: height || 'auto',
    boxShadow: '0 24px 60px rgba(0,0,0,0.5), 0 0 0 1px rgba(255,255,255,0.02)',
  }}>
    <div style={{
      height: 36, display: 'flex', alignItems: 'center',
      padding: '0 12px',
      background: 'linear-gradient(180deg, #111114 0%, #0a0a0d 100%)',
      borderBottom: '1px solid var(--border)',
      gap: 12,
    }}>
      <div style={{ display: 'flex', gap: 6 }}>
        <div style={{ width: 11, height: 11, borderRadius: 6, background: '#ff5f57' }}></div>
        <div style={{ width: 11, height: 11, borderRadius: 6, background: '#febc2e' }}></div>
        <div style={{ width: 11, height: 11, borderRadius: 6, background: '#28c840' }}></div>
      </div>
      <div style={{ color: 'var(--text-mid)', fontSize: 11, fontWeight: 500, letterSpacing: '0.04em' }}>{title}</div>
      <div style={{ flex: 1 }}></div>
      {right}
    </div>
    <div style={{ flex: 1, minHeight: 0, display: 'flex', flexDirection: 'column' }}>{children}</div>
  </div>
);

// ───────────── Top bar (app-level) ─────────────
const TopBar = ({ crumbs, right, mode, project }) => (
  <div style={{
    height: 44,
    display: 'flex', alignItems: 'center',
    padding: '0 14px',
    background: 'var(--bg-panel)',
    borderBottom: '1px solid var(--border)',
    gap: 14,
  }}>
    <div style={{
      width: 24, height: 24, borderRadius: 6,
      background: 'linear-gradient(135deg, var(--cyan), var(--magenta))',
      display: 'flex', alignItems: 'center', justifyContent: 'center',
      boxShadow: '0 0 12px rgba(34,230,255,0.4)',
    }}>
      <span style={{ color: '#000', fontSize: 11, fontWeight: 800 }}>M</span>
    </div>
    <div style={{ display: 'flex', alignItems: 'center', gap: 8, color: 'var(--text-mid)', fontSize: 12 }}>
      {crumbs && crumbs.map((c, i) => (
        <React.Fragment key={i}>
          {i > 0 && <Icon d={I.chevR} size={12} stroke="var(--text-low)" />}
          <span style={{ color: i === crumbs.length - 1 ? 'var(--text-hi)' : 'var(--text-mid)', fontWeight: i === crumbs.length - 1 ? 500 : 400 }}>{c}</span>
        </React.Fragment>
      ))}
    </div>
    <div style={{ flex: 1 }}></div>
    {right}
  </div>
);

// ───────────── Buttons ─────────────
const Btn = ({ children, kind = 'ghost', size = 'md', icon, iconRight, glow, onClick, active, disabled, style, title }) => {
  const sizes = {
    sm: { h: 22, px: 8, fs: 11, gap: 5 },
    md: { h: 28, px: 12, fs: 12, gap: 6 },
    lg: { h: 36, px: 16, fs: 13, gap: 8 },
  }[size];
  const kinds = {
    primary: {
      background: 'linear-gradient(180deg, #1cc0d6, #0a98b3)',
      color: '#001417', border: '1px solid rgba(34,230,255,0.7)',
      boxShadow: glow ? '0 0 18px rgba(34,230,255,0.45), inset 0 1px 0 rgba(255,255,255,0.2)' : 'inset 0 1px 0 rgba(255,255,255,0.15)',
      fontWeight: 600,
    },
    secondary: {
      background: 'linear-gradient(180deg, #ff3df0, #c61eb4)',
      color: '#1d0119', border: '1px solid rgba(255,61,240,0.7)',
      boxShadow: glow ? '0 0 18px rgba(255,61,240,0.45)' : 'none',
      fontWeight: 600,
    },
    success: {
      background: 'linear-gradient(180deg, #65e34c, #45b332)',
      color: '#031a01', border: '1px solid rgba(125,255,90,0.6)',
      boxShadow: glow ? '0 0 18px rgba(125,255,90,0.4)' : 'none',
      fontWeight: 600,
    },
    ghost: {
      background: active ? 'var(--bg-panel-3)' : 'transparent',
      color: active ? 'var(--text-hi)' : 'var(--text-mid)',
      border: '1px solid ' + (active ? 'var(--border-hi)' : 'transparent'),
    },
    outline: {
      background: 'var(--bg-panel-2)',
      color: 'var(--text-hi)',
      border: '1px solid var(--border-hi)',
    },
  }[kind];
  return (
    <button onClick={onClick} disabled={disabled} title={title} style={{
      height: sizes.h, padding: `0 ${sizes.px}px`, fontSize: sizes.fs,
      borderRadius: 6, display: 'inline-flex', alignItems: 'center', gap: sizes.gap,
      cursor: disabled ? 'not-allowed' : 'pointer', opacity: disabled ? 0.45 : 1,
      whiteSpace: 'nowrap',
      transition: 'all 0.12s',
      ...kinds, ...style,
    }}>
      {icon && <Icon d={icon} size={sizes.fs + 2} />}
      {children}
      {iconRight && <Icon d={iconRight} size={sizes.fs + 2} />}
    </button>
  );
};

const IconBtn = ({ icon, size = 28, active, glow, color, onClick, title }) => (
  <button onClick={onClick} title={title} style={{
    width: size, height: size, borderRadius: 6,
    background: active ? 'var(--bg-panel-3)' : 'transparent',
    border: '1px solid ' + (active ? 'var(--border-hi)' : 'transparent'),
    color: color || (active ? 'var(--text-hi)' : 'var(--text-mid)'),
    display: 'inline-flex', alignItems: 'center', justifyContent: 'center',
    cursor: 'pointer',
    boxShadow: glow ? '0 0 12px ' + (color || 'var(--cyan)') + '66' : 'none',
    transition: 'all 0.12s',
  }}>
    <Icon d={icon} size={size * 0.5} />
  </button>
);

// ───────────── Panel ─────────────
const Panel = ({ title, right, children, style, scroll, padding = true }) => (
  <div style={{
    background: 'var(--bg-panel)',
    border: '1px solid var(--border)',
    borderRadius: 8,
    display: 'flex', flexDirection: 'column',
    minHeight: 0,
    ...style,
  }}>
    {title && (
      <div style={{
        height: 32, padding: '0 12px',
        display: 'flex', alignItems: 'center', gap: 8,
        borderBottom: '1px solid var(--divider)',
        flexShrink: 0,
      }}>
        <div style={{
          fontSize: 10, fontWeight: 600, letterSpacing: '0.1em',
          textTransform: 'uppercase', color: 'var(--text-mid)',
        }}>{title}</div>
        <div style={{ flex: 1 }}></div>
        {right}
      </div>
    )}
    <div style={{ flex: 1, minHeight: 0, overflow: scroll ? 'auto' : 'visible', padding: padding ? 0 : 0 }}>
      {children}
    </div>
  </div>
);

// ───────────── Tabs ─────────────
const Tabs = ({ items, value, onChange, accent = 'cyan' }) => (
  <div style={{ display: 'flex', borderBottom: '1px solid var(--divider)', gap: 0 }}>
    {items.map(t => {
      const active = t === value || t.id === value;
      const id = t.id || t;
      const label = t.label || t;
      return (
        <div key={id} onClick={() => onChange && onChange(id)} style={{
          padding: '8px 14px', fontSize: 11, fontWeight: 500,
          color: active ? 'var(--text-hi)' : 'var(--text-mid)',
          cursor: 'pointer',
          borderBottom: '2px solid ' + (active ? `var(--${accent})` : 'transparent'),
          marginBottom: -1,
          letterSpacing: '0.02em',
          display: 'flex', alignItems: 'center', gap: 6,
        }}>
          {t.icon && <Icon d={t.icon} size={12} />}
          {label}
          {t.count != null && <span style={{
            fontSize: 10, padding: '1px 5px', borderRadius: 4,
            background: active ? `var(--${accent})` : 'var(--bg-panel-3)',
            color: active ? '#000' : 'var(--text-mid)',
            fontWeight: 600,
          }}>{t.count}</span>}
        </div>
      );
    })}
  </div>
);

// ───────────── Sidebar items ─────────────
const SidebarItem = ({ icon, label, active, color, indent = 0, hasChildren, expanded, locked, hidden, onClick, badge }) => (
  <div onClick={onClick} style={{
    display: 'flex', alignItems: 'center', gap: 8,
    padding: '0 8px',
    height: 26,
    borderRadius: 4,
    background: active ? 'rgba(34,230,255,0.08)' : 'transparent',
    color: active ? 'var(--text-hi)' : 'var(--text-mid)',
    cursor: 'pointer',
    fontSize: 12,
    paddingLeft: 8 + indent * 14,
    borderLeft: active ? '2px solid var(--cyan)' : '2px solid transparent',
    position: 'relative',
  }}>
    {hasChildren ? (
      <Icon d={expanded ? I.chevD : I.chevR} size={10} stroke="var(--text-low)" />
    ) : (
      <span style={{ width: 10 }}></span>
    )}
    {icon && <Icon d={icon} size={13} stroke={color || (active ? 'var(--cyan)' : 'var(--text-mid)')} />}
    <span style={{ flex: 1, overflow: 'hidden', textOverflow: 'ellipsis', whiteSpace: 'nowrap' }}>{label}</span>
    {badge && <span style={{ fontSize: 10, color: 'var(--text-low)', fontFamily: 'JetBrains Mono, monospace' }}>{badge}</span>}
    {hidden && <Icon d={I.eye} size={11} stroke="var(--text-low)" />}
    {locked && <Icon d={I.lock} size={11} stroke="var(--text-low)" />}
  </div>
);

// ───────────── Input field ─────────────
const Field = ({ label, value, suffix, mono, onChange, width, color }) => (
  <div style={{ display: 'flex', alignItems: 'center', gap: 6, flex: width ? 'none' : 1 }}>
    {label && <div style={{ fontSize: 11, color: 'var(--text-low)', minWidth: 30 }}>{label}</div>}
    <div style={{
      flex: 1, width: width || 'auto',
      height: 24, borderRadius: 4,
      background: 'var(--bg-panel-3)',
      border: '1px solid var(--border)',
      display: 'flex', alignItems: 'center',
      padding: '0 8px',
      fontSize: 11,
      color: color || 'var(--text-hi)',
      fontFamily: mono ? 'JetBrains Mono, monospace' : 'inherit',
    }}>
      <span style={{ flex: 1 }}>{value}</span>
      {suffix && <span style={{ color: 'var(--text-low)', fontSize: 10 }}>{suffix}</span>}
    </div>
  </div>
);

// ───────────── Slider ─────────────
const Slider = ({ label, value, min = 0, max = 1, suffix = '', accent = 'cyan' }) => {
  const pct = ((value - min) / (max - min)) * 100;
  return (
    <div>
      {label && (
        <div style={{ display: 'flex', justifyContent: 'space-between', marginBottom: 4 }}>
          <span style={{ fontSize: 11, color: 'var(--text-mid)' }}>{label}</span>
          <span style={{ fontSize: 11, color: 'var(--text-hi)', fontFamily: 'JetBrains Mono, monospace' }}>
            {typeof value === 'number' ? (value % 1 === 0 ? value : value.toFixed(2)) : value}{suffix}
          </span>
        </div>
      )}
      <div style={{
        height: 4, borderRadius: 2,
        background: 'var(--bg-panel-3)',
        position: 'relative',
      }}>
        <div style={{
          position: 'absolute', left: 0, top: 0, bottom: 0,
          width: pct + '%',
          background: `linear-gradient(90deg, var(--${accent}-dim), var(--${accent}))`,
          borderRadius: 2,
          boxShadow: `0 0 8px var(--${accent})`,
        }}></div>
        <div style={{
          position: 'absolute', left: `calc(${pct}% - 6px)`, top: -4,
          width: 12, height: 12, borderRadius: 6,
          background: '#fff',
          boxShadow: `0 0 8px var(--${accent})`,
        }}></div>
      </div>
    </div>
  );
};

// ───────────── Toggle ─────────────
const Toggle = ({ value, accent = 'cyan' }) => (
  <div style={{
    width: 26, height: 14, borderRadius: 8,
    background: value ? `var(--${accent}-dim)` : 'var(--bg-panel-3)',
    border: '1px solid ' + (value ? `var(--${accent})` : 'var(--border)'),
    position: 'relative',
    transition: 'all 0.2s',
    boxShadow: value ? `0 0 8px var(--${accent})` : 'none',
  }}>
    <div style={{
      position: 'absolute', top: 1, left: value ? 13 : 1,
      width: 10, height: 10, borderRadius: 5,
      background: '#fff',
      transition: 'left 0.2s',
    }}></div>
  </div>
);

// ───────────── Checkbox ─────────────
const Checkbox = ({ value, accent = 'cyan' }) => (
  <div style={{
    width: 13, height: 13, borderRadius: 3,
    background: value ? `var(--${accent})` : 'var(--bg-panel-3)',
    border: '1px solid ' + (value ? `var(--${accent})` : 'var(--border-hi)'),
    display: 'flex', alignItems: 'center', justifyContent: 'center',
    boxShadow: value ? `0 0 6px var(--${accent})` : 'none',
  }}>
    {value && <Icon d={I.check} size={9} stroke="#000" sw={3} />}
  </div>
);

// ───────────── Pill / chip ─────────────
const Pill = ({ children, color = 'cyan', solid, glow, small, style }) => (
  <span style={{
    display: 'inline-flex', alignItems: 'center', gap: 4,
    padding: small ? '1px 6px' : '2px 8px',
    borderRadius: 999,
    fontSize: small ? 9 : 10, fontWeight: 600, letterSpacing: '0.04em',
    background: solid ? `var(--${color})` : `color-mix(in oklch, var(--${color}) 16%, transparent)`,
    color: solid ? '#000' : `var(--${color})`,
    border: solid ? 'none' : `1px solid color-mix(in oklch, var(--${color}) 40%, transparent)`,
    boxShadow: glow ? `0 0 10px var(--${color})66` : 'none',
    textTransform: 'uppercase',
    ...style,
  }}>{children}</span>
);

// ───────────── Section header (sidebar) ─────────────
const SectionHeader = ({ label, expanded = true, right, color }) => (
  <div style={{
    display: 'flex', alignItems: 'center', gap: 6,
    padding: '8px 10px 6px',
    fontSize: 10, fontWeight: 600, letterSpacing: '0.1em',
    textTransform: 'uppercase',
    color: color || 'var(--text-low)',
  }}>
    <Icon d={expanded ? I.chevD : I.chevR} size={9} stroke="currentColor" />
    <span style={{ flex: 1 }}>{label}</span>
    {right}
  </div>
);

// ───────────── Surface wrap (full-bleed background) ─────────────
const Surface = ({ children, style }) => (
  <div className="mge-app" style={{
    width: '100%', height: '100%',
    background: '#000',
    overflow: 'hidden',
    display: 'flex', flexDirection: 'column',
    ...style,
  }}>{children}</div>
);

// Striped placeholder (for images, covers, etc.)
const Placeholder = ({ label, w, h, color = 'cyan', radius = 6 }) => (
  <div style={{
    width: w || '100%', height: h || '100%',
    borderRadius: radius,
    background: `repeating-linear-gradient(135deg,
      color-mix(in oklch, var(--${color}) 8%, #0a0a0d) 0 6px,
      color-mix(in oklch, var(--${color}) 14%, #0a0a0d) 6px 12px)`,
    border: `1px dashed color-mix(in oklch, var(--${color}) 30%, transparent)`,
    display: 'flex', alignItems: 'center', justifyContent: 'center',
    color: `color-mix(in oklch, var(--${color}) 70%, white)`,
    fontFamily: 'JetBrains Mono, monospace',
    fontSize: 9, letterSpacing: '0.1em', textTransform: 'uppercase',
    flex: '0 0 auto',
  }}>{label}</div>
);

Object.assign(window, {
  Icon, I, WindowChrome, TopBar, Btn, IconBtn, Panel, Tabs, SidebarItem,
  Field, Slider, Toggle, Checkbox, Pill, SectionHeader, Surface, Placeholder,
});
