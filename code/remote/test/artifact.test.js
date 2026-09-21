const test = require('node:test');
const assert = require('node:assert/strict');
const { readFileSync } = require('node:fs');
const { join } = require('node:path');

test('generated index is standalone and local-only', () => {
  const html = readFileSync(join(__dirname, '..', 'index.html'), 'utf8');
  assert.match(html, /<script>\s*\(function/);
  assert.doesNotMatch(html, /<script[^>]+src=/i);
  assert.doesNotMatch(html, /\bfetch\s*\(/);
  assert.doesNotMatch(html, /\bimport\s*\(/);
  assert.doesNotMatch(html, /serviceWorker|localStorage|sessionStorage/);
});

test('serial lifecycle opens at 115200 baud with DTR and no writes', () => {
  const app = readFileSync(join(__dirname, '..', 'src', 'app.js'), 'utf8');
  assert.match(app, /open\(\{\s*baudRate:\s*115200/);
  assert.match(app, /setSignals\(\{\s*dataTerminalReady:\s*true\s*\}\)/);
  assert.doesNotMatch(app, /\.writable|getWriter\(|\.write\(/);
});
