'use strict';
// ── Config ────────────────────────────────────────────────────────────────────
const API = '';   // same origin; change to 'http://localhost:3000' if proxying
const POLL_INTERVAL_MS = 2000;

// ── Element refs ─────────────────────────────────────────────────────────────
const $statusDot   = document.getElementById('status-dot');
const $statusText  = document.getElementById('status-text');
const $mOps        = document.getElementById('m-ops');
const $mTotal      = document.getElementById('m-total');
const $mKeys       = document.getElementById('m-keys');
const $mMem        = document.getElementById('m-mem');
const $mSst        = document.getElementById('m-sst');
const $mBloom      = document.getElementById('m-bloom');
const $mHitrate    = document.getElementById('m-hitrate');
const $mCacheDetail= document.getElementById('m-cache-detail');
const $consoleOut  = document.getElementById('console-output');
const $consoleIn   = document.getElementById('console-input');
const $btnSend     = document.getElementById('btn-send');
const $btnBench    = document.getElementById('btn-bench');
const $benchOut    = document.getElementById('bench-output');
const $benchWrap   = document.getElementById('bench-chart-wrap');

// ── Chart setup ───────────────────────────────────────────────────────────────
const MAX_POINTS = 30;
const CHART_OPTS = {
  responsive: true,
  animation: { duration: 250 },
  plugins: { legend: { labels: { color: '#6b7694', font: { family: 'Inter', size: 11 } } } },
  scales: {
    x: { ticks: { color: '#3d4259', maxTicksLimit: 6 }, grid: { color: 'rgba(255,255,255,0.04)' } },
    y: { ticks: { color: '#3d4259' }, grid: { color: 'rgba(255,255,255,0.04)' }, beginAtZero: true },
  },
};

function makeTimeSeries(n) {
  return Array.from({ length: n }, (_, i) => `${(n - i) * (POLL_INTERVAL_MS / 1000)}s ago`);
}

const opsChart = new Chart(document.getElementById('chart-ops'), {
  type: 'line',
  data: {
    labels: makeTimeSeries(MAX_POINTS),
    datasets: [{
      label: 'ops/sec', data: new Array(MAX_POINTS).fill(null),
      borderColor: '#ff4757', backgroundColor: 'rgba(255,71,87,0.08)',
      pointRadius: 0, tension: 0.4, fill: true,
    }],
  },
  options: CHART_OPTS,
});

const cacheChart = new Chart(document.getElementById('chart-cache'), {
  type: 'bar',
  data: {
    labels: makeTimeSeries(MAX_POINTS),
    datasets: [
      { label: 'hits',   data: new Array(MAX_POINTS).fill(null), backgroundColor: 'rgba(29,233,182,0.6)' },
      { label: 'misses', data: new Array(MAX_POINTS).fill(null), backgroundColor: 'rgba(255,71,87,0.5)' },
    ],
  },
  options: { ...CHART_OPTS, plugins: { ...CHART_OPTS.plugins } },
});

let prevHits = 0, prevMisses = 0;

function pushToChart(chart, datasetIdx, value) {
  const ds = chart.data.datasets[datasetIdx];
  ds.data.push(value);
  if (ds.data.length > MAX_POINTS) ds.data.shift();
  // Slide labels
  chart.data.labels.push('now');
  if (chart.data.labels.length > MAX_POINTS) chart.data.labels.shift();
}

// ── Metrics polling ───────────────────────────────────────────────────────────
let connected = false;

async function pollMetrics() {
  try {
    const r = await fetch(`${API}/api/metrics`);
    if (!r.ok) throw new Error('HTTP ' + r.status);
    const d = await r.json();
    if (!d.ok) throw new Error(d.error);

    // Update status
    if (!connected) {
      connected = true;
      $statusDot.className = 'status-dot connected';
      $statusText.textContent = 'Connected';
    }

    // Metric cards
    $mOps.textContent   = fmt(d.ops_per_sec);
    $mTotal.textContent = fmt(d.total_commands) + ' total';
    $mKeys.textContent  = fmt(d.db_keys);
    $mMem.textContent   = fmtBytes(d.memtable_bytes) + ' memtable';
    $mSst.textContent   = d.sstable_count;
    $mBloom.textContent = fmt(d.bloom_skips) + ' bloom skips';

    const hits   = d.cache_hits;
    const misses = d.cache_misses;
    const total  = hits + misses;
    const rate   = total > 0 ? ((hits / total) * 100).toFixed(1) + '%' : '—';
    $mHitrate.textContent     = rate;
    $mCacheDetail.textContent = fmt(hits) + ' hits / ' + fmt(misses) + ' misses';

    // Charts
    pushToChart(opsChart, 0, d.ops_per_sec);
    opsChart.update('none');

    const deltaHits   = hits   - prevHits;
    const deltaMisses = misses - prevMisses;
    prevHits   = hits;
    prevMisses = misses;
    pushToChart(cacheChart, 0, deltaHits);
    pushToChart(cacheChart, 1, deltaMisses);
    cacheChart.update('none');

  } catch (e) {
    connected = false;
    $statusDot.className = 'status-dot error';
    $statusText.textContent = 'Offline';
  }
}

pollMetrics();
setInterval(pollMetrics, POLL_INTERVAL_MS);

// ── Number formatters ─────────────────────────────────────────────────────────
function fmt(n) {
  if (n === undefined || n === null) return '—';
  if (n >= 1e6) return (n / 1e6).toFixed(1) + 'M';
  if (n >= 1e3) return (n / 1e3).toFixed(1) + 'K';
  return String(n);
}
function fmtBytes(b) {
  if (b >= 1024*1024) return (b / 1024 / 1024).toFixed(1) + ' MB';
  if (b >= 1024)      return (b / 1024).toFixed(1) + ' KB';
  return b + ' B';
}

// ── Console ───────────────────────────────────────────────────────────────────
let cmdHistory = [];
let histIdx    = -1;

function appendConsole(type, text) {
  const line = document.createElement('div');
  line.className = 'console-line';
  const ts = document.createElement('span');
  ts.className = 'ts';
  ts.textContent = new Date().toLocaleTimeString('en', { hour12: false });
  const content = document.createElement('span');
  content.className = type;
  content.textContent = text;
  line.appendChild(ts);
  line.appendChild(content);
  $consoleOut.appendChild(line);
  $consoleOut.scrollTop = $consoleOut.scrollHeight;
}

async function sendCommand() {
  const cmd = $consoleIn.value.trim();
  if (!cmd) return;
  cmdHistory.unshift(cmd);
  histIdx = -1;
  $consoleIn.value = '';

  appendConsole('cmd', '> ' + cmd);
  try {
    const r = await fetch(`${API}/api/command`, {
      method: 'POST', headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ command: cmd }),
    });
    const d = await r.json();
    if (d.ok) appendConsole('resp', d.result);
    else      appendConsole('err',  d.error);
  } catch (e) {
    appendConsole('err', 'Network error: ' + e.message);
  }
}

$btnSend.addEventListener('click', sendCommand);
$consoleIn.addEventListener('keydown', e => {
  if (e.key === 'Enter') { sendCommand(); return; }
  if (e.key === 'ArrowUp') {
    if (histIdx + 1 < cmdHistory.length) { histIdx++; $consoleIn.value = cmdHistory[histIdx]; }
    e.preventDefault();
  }
  if (e.key === 'ArrowDown') {
    if (histIdx > 0) { histIdx--; $consoleIn.value = cmdHistory[histIdx]; }
    else { histIdx = -1; $consoleIn.value = ''; }
    e.preventDefault();
  }
});

// ── Benchmark ─────────────────────────────────────────────────────────────────
let benchChart = null;

$btnBench.addEventListener('click', async () => {
  $btnBench.disabled = true;
  $btnBench.textContent = '⏳ Running…';
  $benchOut.textContent = '';
  $benchWrap.style.display = 'none';

  const r = await fetch(`${API}/api/benchmark`, { method: 'POST' });
  const reader = r.body.getReader();
  const dec = new TextDecoder();
  let buf = '';

  const benchLabels = [];
  const benchSetData = [];
  const benchGetData = [];

  while (true) {
    const { done, value } = await reader.read();
    if (done) break;
    buf += dec.decode(value, { stream: true });
    const parts = buf.split('\n\n');
    buf = parts.pop();
    for (const part of parts) {
      if (!part.startsWith('data:')) continue;
      try {
        const obj = JSON.parse(part.slice(5).trim());
        if (obj.line) {
          $benchOut.textContent += obj.line;
          $benchOut.scrollTop = $benchOut.scrollHeight;

          // Parse SET/GET lines from redis-benchmark CSV output
          const setM = obj.line.match(/"SET","([\d.]+)"/);
          const getM = obj.line.match(/"GET","([\d.]+)"/);
          if (setM) { benchSetData.push(parseFloat(setM[1])); benchLabels.push('Run ' + benchSetData.length); }
          if (getM)   benchGetData.push(parseFloat(getM[1]));
        }
        if (obj.done) {
          $btnBench.disabled = false;
          $btnBench.textContent = '▶ Run Benchmark';
          if (benchSetData.length > 0) {
            $benchWrap.style.display = 'block';
            if (benchChart) benchChart.destroy();
            benchChart = new Chart(document.getElementById('chart-bench'), {
              type: 'bar',
              data: {
                labels: benchLabels,
                datasets: [
                  { label: 'SET ops/s', data: benchSetData, backgroundColor: 'rgba(255,71,87,0.7)' },
                  { label: 'GET ops/s', data: benchGetData, backgroundColor: 'rgba(29,233,182,0.6)' },
                ],
              },
              options: CHART_OPTS,
            });
          }
        }
      } catch (_) {}
    }
  }
});
