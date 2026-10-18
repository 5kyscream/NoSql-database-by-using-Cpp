'use strict';
const express = require('express');
const cors    = require('cors');
const Redis   = require('ioredis');
const path    = require('path');
const { exec } = require('child_process');

const app  = express();
const PORT = process.env.DASHBOARD_PORT || 3000;
const REDIS_HOST = process.env.MINIRED_HOST || '127.0.0.1';
const REDIS_PORT = parseInt(process.env.MINIRED_PORT || '6379', 10);

app.use(cors());
app.use(express.json());
app.use(express.static(path.join(__dirname, 'public')));

// ── ioredis client ────────────────────────────────────────────────────────────
const redis = new Redis({
    host: REDIS_HOST,
    port: REDIS_PORT,
    retryStrategy: (times) => Math.min(times * 200, 3000),
    lazyConnect: true,
});

redis.connect().catch(() => {
    console.warn(`Warning: could not connect to MiniRed at ${REDIS_HOST}:${REDIS_PORT}`);
});

// ── Parse INFO string into an object ─────────────────────────────────────────
function parseInfo(raw) {
    const result = {};
    for (const line of raw.split('\r\n')) {
        if (line.startsWith('#') || !line.includes(':')) continue;
        const [k, v] = line.split(':');
        result[k.trim()] = v ? v.trim() : '';
    }
    return result;
}

// ── GET /api/info  ────────────────────────────────────────────────────────────
app.get('/api/info', async (req, res) => {
    try {
        const raw = await redis.info();
        res.json({ ok: true, info: parseInfo(raw) });
    } catch (e) {
        res.status(503).json({ ok: false, error: e.message });
    }
});

// ── GET /api/metrics  (subset of INFO for live panel) ────────────────────────
app.get('/api/metrics', async (req, res) => {
    try {
        const raw = await redis.info();
        const info = parseInfo(raw);
        res.json({
            ok: true,
            ops_per_sec:        parseFloat(info.instantaneous_ops_per_sec || '0'),
            total_commands:     parseInt(info.total_commands_processed    || '0', 10),
            db_keys:            parseInt((info['db0'] || '').replace(/.*keys=(\d+).*/, '$1') || '0', 10),
            memtable_bytes:     parseInt(info.memtable_size_bytes || '0', 10),
            sstable_count:      parseInt(info.sstable_count       || '0', 10),
            bloom_skips:        parseInt(info.bloom_filter_skips  || '0', 10),
            cache_hits:         parseInt(info.cache_hits          || '0', 10),
            cache_misses:       parseInt(info.cache_misses        || '0', 10),
        });
    } catch (e) {
        res.status(503).json({ ok: false, error: e.message });
    }
});

// ── POST /api/command  ───────────────────────────────────────────────────────
// Body: { command: "SET foo bar" }
app.post('/api/command', async (req, res) => {
    const { command } = req.body || {};
    if (!command) return res.status(400).json({ ok: false, error: 'missing command' });

    const parts = command.trim().split(/\s+/);
    const cmd   = parts[0].toLowerCase();
    const args  = parts.slice(1);

    try {
        const result = await redis.call(cmd, ...args);
        res.json({ ok: true, result: String(result) });
    } catch (e) {
        res.json({ ok: false, error: e.message });
    }
});

// ── POST /api/benchmark  ─────────────────────────────────────────────────────
// Runs the benchmark script and streams output via newline-delimited JSON.
app.post('/api/benchmark', (req, res) => {
    res.setHeader('Content-Type', 'text/event-stream');
    res.setHeader('Cache-Control', 'no-cache');
    res.flushHeaders();

    const scriptPath = path.join(__dirname, '..', 'benchmarks', 'run_benchmarks.sh');
    const child = exec(`bash "${scriptPath}"`, { env: process.env });

    child.stdout.on('data', d => res.write(`data: ${JSON.stringify({ line: d })}\n\n`));
    child.stderr.on('data', d => res.write(`data: ${JSON.stringify({ line: d })}\n\n`));
    child.on('close', code => {
        res.write(`data: ${JSON.stringify({ done: true, code })}\n\n`);
        res.end();
    });
});

app.listen(PORT, () => {
    console.log(`MiniRed Dashboard running at http://localhost:${PORT}`);
    console.log(`Connecting to MiniRed at ${REDIS_HOST}:${REDIS_PORT}`);
});
