// Design tokens for Music Game Engine UI
// Pure black canvas, neon accents

window.MGE_TOKENS = {
  // Surfaces — true black foundation, lifted with very subtle neutrals
  bg: {
    void:     '#000000',          // outermost canvas
    base:     '#070708',          // window
    panel:    '#0d0d10',          // panel surface
    panel2:   '#131318',          // raised panel / row hover
    panel3:   '#1a1a21',          // input fields, pressed
    border:   'rgba(255,255,255,0.06)',
    borderHi: 'rgba(255,255,255,0.10)',
    divider:  'rgba(255,255,255,0.04)',
  },
  // Accents — muted neon, oklch-derived, share chroma
  accent: {
    cyan:    '#5fb8c9',  // primary — desaturated teal
    cyanDim: '#3a7884',
    magenta: '#c87ab8',  // secondary — dusty rose
    magentaDim:'#8a4f7e',
    lime:    '#8fb87a',  // perfect / success / FC — sage
    amber:   '#d4a574',  // warning / good — muted gold
    red:     '#c97080',  // error / miss — muted coral
    violet:  '#8a7fb0',  // copilot / AI — dusty violet
  },
  // Text
  text: {
    hi:   '#f3f4f7',
    mid:  '#a8acb6',
    low:  '#5e636e',
    dim:  '#3a3e47',
  },
  // Glows — used as box-shadow for muted neon edges (low intensity)
  glow: {
    cyan:    '0 0 8px rgba(95, 184, 201, 0.22), 0 0 1px rgba(95,184,201,0.5) inset',
    cyanSoft:'0 0 16px rgba(95, 184, 201, 0.10)',
    magenta: '0 0 8px rgba(200, 122, 184, 0.22), 0 0 1px rgba(200,122,184,0.5) inset',
    magentaSoft:'0 0 16px rgba(200, 122, 184, 0.10)',
    lime:    '0 0 8px rgba(143, 184, 122, 0.22)',
  },
  // Typography
  font: {
    sans: '"Inter", -apple-system, system-ui, sans-serif',
    mono: '"JetBrains Mono", "SF Mono", Menlo, monospace',
  },
  // Radii
  r: { xs: 3, sm: 5, md: 7, lg: 10, xl: 14 },
  // Spacing scale
  s: { '0': 0, '1': 4, '2': 8, '3': 12, '4': 16, '5': 20, '6': 24, '8': 32, '10': 40, '12': 48 },
};

// Inject the global CSS once
(function injectGlobal() {
  if (document.getElementById('mge-global-css')) return;
  const style = document.createElement('style');
  style.id = 'mge-global-css';
  style.textContent = `
    @import url('https://fonts.googleapis.com/css2?family=Inter:wght@300;400;500;600;700&family=JetBrains+Mono:wght@400;500;600&display=swap');

    :root {
      --bg-void: #000;
      --bg-base: #070708;
      --bg-panel: #0d0d10;
      --bg-panel-2: #131318;
      --bg-panel-3: #1a1a21;
      --border: rgba(255,255,255,0.06);
      --border-hi: rgba(255,255,255,0.10);
      --divider: rgba(255,255,255,0.04);
      --text-hi: #f3f4f7;
      --text-mid: #a8acb6;
      --text-low: #5e636e;
      --text-dim: #3a3e47;
      --cyan: #5fb8c9;
      --cyan-dim: #3a7884;
      --magenta: #c87ab8;
      --magenta-dim: #8a4f7e;
      --lime: #8fb87a;
      --amber: #d4a574;
      --red: #c97080;
      --violet: #8a7fb0;
    }

    .mge-app, .mge-app * {
      box-sizing: border-box;
      font-family: "Inter", -apple-system, system-ui, sans-serif;
      -webkit-font-smoothing: antialiased;
      -moz-osx-font-smoothing: grayscale;
    }
    .mge-app {
      background: #000;
      color: var(--text-hi);
      font-size: 12px;
      line-height: 1.4;
      letter-spacing: 0.005em;
      user-select: none;
    }
    .mge-app .mono { font-family: "JetBrains Mono", "SF Mono", Menlo, monospace; font-feature-settings: "tnum"; }

    /* Scrollbar */
    .mge-app ::-webkit-scrollbar { width: 8px; height: 8px; }
    .mge-app ::-webkit-scrollbar-track { background: transparent; }
    .mge-app ::-webkit-scrollbar-thumb { background: rgba(255,255,255,0.08); border-radius: 4px; }
    .mge-app ::-webkit-scrollbar-thumb:hover { background: rgba(255,255,255,0.16); }
  `;
  document.head.appendChild(style);
})();
