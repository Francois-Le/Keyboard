import { readFile, writeFile } from 'node:fs/promises';

const [css, traceJs, appJs] = await Promise.all([
  readFile(new URL('./src/styles.css', import.meta.url), 'utf8'),
  readFile(new URL('./src/trace.js', import.meta.url), 'utf8'),
  readFile(new URL('./src/app.js', import.meta.url), 'utf8')
]);

const html = `<!doctype html>
<html lang="en">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>Keyboard Trace Timeline</title>
  <style>
${css}
  </style>
</head>
<body>
  <header>
    <h1>Keyboard Trace Timeline</h1>
    <p class="subtitle">Local-only Web Serial viewer for KBT1 custom keyboard traces. Time flows downward; INPUT, INTERNAL decisions, and OUTPUT HID submissions are separated by labels and color.</p>
  </header>
  <section class="toolbar">
    <button id="connectBtn" type="button">Connect keyboard</button>
    <button id="disconnectBtn" type="button" disabled>Disconnect</button>
    <button id="demoBtn" type="button">Demo trace</button>
    <button id="clearBtn" type="button">Clear view</button>
    <button id="exportBtn" type="button">Export trace</button>
    <label class="fileButton">Import trace <input id="importInput" type="file" accept="application/json,.json"></label>
    <div id="status" class="status">Starting…</div>
  </section>
  <section class="filters">
    <label><input id="filterINPUT" type="checkbox" checked> INPUT</label>
    <label><input id="filterINTERNAL" type="checkbox" checked> INTERNAL</label>
    <label><input id="filterOUTPUT" type="checkbox" checked> OUTPUT</label>
    <label><input id="followToggle" type="checkbox" checked> Follow while reading</label>
    <label>Zoom <input id="zoom" type="range" min="0.4" max="4" step="0.1" value="1"></label>
    <label>Layout <select id="keyboardLayout">
      <option value="en-US">English (US QWERTY)</option>
      <option value="fr-FR">French (France AZERTY)</option>
    </select></label>
    <small id="layoutNotice" role="status"></small>
    <strong class="sensitivity">Capture is local; typed key data can be sensitive.</strong>
  </section>
  <section class="stats">
    <span id="eventCount">0 retained / 0 shown</span>
    <span id="queueState">queue unknown</span>
    <span id="streamInfo">no HELLO yet</span>
  </section>
  <main>
    <section id="timeline" class="timeline" aria-label="Vertical chronological trace timeline"></section>
    <aside class="sidePanel">
      <h2>Event details</h2>
      <pre id="details">Click an event to inspect queue after it.</pre>
    </aside>
  </main>
  <script>
${traceJs}
  </script>
  <script>
${appJs}
  </script>
</body>
</html>
`;

await writeFile(new URL('./index.html', import.meta.url), html, 'utf8');
console.log('Wrote code\\\\remote\\\\index.html');
