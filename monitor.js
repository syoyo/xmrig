'use strict';

const os = require('os');
const { spawn } = require('child_process');
const path = require('path');

// ─── Arg Parser ───────────────────────────────────────────────────────────────

const defaults = {
  idleThreshold: 20,
  busyThreshold: 50,
  window: 180,
  sampleInterval: 10,
  maxLogLines: 1000,
  gcInterval: 1800,
  minerPath: 'build/xmrig',
  minerArgs: '--bench=1M',
  dryRun: false,
};

function parseArgs(argv) {
  const config = { ...defaults };
  for (const arg of argv.slice(2)) {
    if (!arg.startsWith('--')) continue;
    const [key, ...rest] = arg.slice(2).split('=');
    const val = rest.join('=');
    switch (key) {
      case 'idle-threshold':  config.idleThreshold = Number(val); break;
      case 'busy-threshold':  config.busyThreshold = Number(val); break;
      case 'window':          config.window = Number(val); break;
      case 'sample-interval': config.sampleInterval = Number(val); break;
      case 'max-log-lines':   config.maxLogLines = Number(val); break;
      case 'gc-interval':     config.gcInterval = Number(val); break;
      case 'miner-path':      config.minerPath = val; break;
      case 'miner-args':      config.minerArgs = val; break;
      case 'dry-run':         config.dryRun = true; break;
    }
  }
  return config;
}

const config = parseArgs(process.argv);
const IS_WIN = process.platform === 'win32';
const SAMPLES_IN_WINDOW = Math.max(1, Math.floor(config.window / config.sampleInterval));

// ─── Ring Buffer ──────────────────────────────────────────────────────────────

class RingBuffer {
  constructor(capacity) {
    this._buf = new Array(capacity);
    this._cap = capacity;
    this._head = 0;
    this._size = 0;
  }

  push(item) {
    this._buf[this._head] = item;
    this._head = (this._head + 1) % this._cap;
    if (this._size < this._cap) this._size++;
  }

  getAll() {
    if (this._size === 0) return [];
    if (this._size < this._cap) return this._buf.slice(0, this._size);
    return this._buf.slice(this._head).concat(this._buf.slice(0, this._head));
  }

  average() {
    if (this._size === 0) return 0;
    let sum = 0;
    const all = this.getAll();
    for (let i = 0; i < all.length; i++) sum += all[i];
    return sum / all.length;
  }

  get size() { return this._size; }
}

// ─── Logging ──────────────────────────────────────────────────────────────────

const logBuffer = new RingBuffer(config.maxLogLines);
const originalLog = console.log.bind(console);
const originalError = console.error.bind(console);

console.log = function (...args) {
  const line = `[${new Date().toISOString()}] ${args.join(' ')}`;
  logBuffer.push(line);
  originalLog(line);
};

console.error = function (...args) {
  const line = `[${new Date().toISOString()}] ERROR: ${args.join(' ')}`;
  logBuffer.push(line);
  originalError(line);
};

// ─── CPU Measurement ─────────────────────────────────────────────────────────

function cpuSnapshot() {
  const cpus = os.cpus();
  let totalIdle = 0, totalTick = 0;
  for (const cpu of cpus) {
    for (const type of Object.keys(cpu.times)) {
      totalTick += cpu.times[type];
    }
    totalIdle += cpu.times.idle;
  }
  return { idle: totalIdle, total: totalTick };
}

function cpuUsagePercent(prev, curr) {
  const idleDelta = curr.idle - prev.idle;
  const totalDelta = curr.total - prev.total;
  if (totalDelta === 0) return 0;
  return ((1 - idleDelta / totalDelta) * 100);
}

// ─── Miner Manager ───────────────────────────────────────────────────────────

class MinerManager {
  constructor(minerPath, minerArgs, dryRun) {
    this._path = path.resolve(minerPath);
    this._args = minerArgs ? minerArgs.split(',') : [];
    this._dryRun = dryRun;
    this._child = null;
    this._state = 'idle'; // idle | running | paused
    this._startCount = 0;
  }

  get state() { return this._state; }

  start() {
    if (this._state === 'running') return;

    if (this._state === 'paused' && !IS_WIN && this._child) {
      console.log(`Resuming miner (SIGCONT) pid=${this._child.pid}`);
      if (!this._dryRun) {
        this._child.kill('SIGCONT');
      }
      this._state = 'running';
      return;
    }

    // spawn new process
    this._startCount++;
    console.log(`Starting miner: ${this._path} ${this._args.join(' ')} (start #${this._startCount})`);
    if (this._dryRun) {
      this._state = 'running';
      return;
    }

    this._child = spawn(this._path, this._args, {
      stdio: ['ignore', 'pipe', 'pipe'],
    });

    this._child.stdout.on('data', (chunk) => {
      const lines = chunk.toString().split('\n');
      for (const line of lines) {
        if (line.trim()) console.log(`[miner] ${line}`);
      }
    });

    this._child.stderr.on('data', (chunk) => {
      const lines = chunk.toString().split('\n');
      for (const line of lines) {
        if (line.trim()) console.error(`[miner] ${line}`);
      }
    });

    this._child.on('error', (err) => {
      console.error(`Miner process error: ${err.message}`);
      this._child = null;
      this._state = 'idle';
    });

    this._child.on('exit', (code, signal) => {
      console.log(`Miner exited: code=${code} signal=${signal}`);
      this._child = null;
      // Only go to idle if we didn't intentionally kill/stop it
      if (this._state === 'running') {
        this._state = 'idle';
      }
    });

    this._state = 'running';
  }

  pause() {
    if (this._state !== 'running') return;

    if (IS_WIN) {
      console.log('Pausing miner (kill on Windows)');
      if (!this._dryRun && this._child) {
        this._child.kill();
        this._child = null;
      }
      this._state = 'paused';
    } else {
      console.log(`Pausing miner (SIGSTOP) pid=${this._child ? this._child.pid : 'N/A'}`);
      if (!this._dryRun && this._child) {
        this._child.kill('SIGSTOP');
      }
      this._state = 'paused';
    }
  }

  kill() {
    if (!this._child) {
      this._state = 'idle';
      return;
    }
    console.log(`Killing miner pid=${this._child.pid}`);
    // If paused on Unix, resume first so it can receive SIGTERM
    if (!IS_WIN && this._state === 'paused') {
      try { this._child.kill('SIGCONT'); } catch (_) {}
    }
    try { this._child.kill('SIGTERM'); } catch (_) {}
    this._child = null;
    this._state = 'idle';
  }
}

// ─── Main ─────────────────────────────────────────────────────────────────────

const cpuSamples = new RingBuffer(SAMPLES_IN_WINDOW);
const miner = new MinerManager(config.minerPath, config.minerArgs, config.dryRun);
let prevSnap = cpuSnapshot();
const startTime = Date.now();

console.log('=== XMRig CPU Idle Monitor ===');
console.log(`Platform: ${process.platform} (${IS_WIN ? 'Windows strategy' : 'Unix strategy'})`);
console.log(`Config: idle<${config.idleThreshold}% busy>${config.busyThreshold}% window=${config.window}s sample=${config.sampleInterval}s`);
console.log(`Miner: ${config.minerPath} ${config.minerArgs}`);
console.log(`Samples in window: ${SAMPLES_IN_WINDOW}`);
if (config.dryRun) console.log('*** DRY RUN MODE — miner will not actually be spawned ***');

function formatUptime(ms) {
  const s = Math.floor(ms / 1000);
  const d = Math.floor(s / 86400);
  const h = Math.floor((s % 86400) / 3600);
  const m = Math.floor((s % 3600) / 60);
  return `${d}d ${h}h ${m}m`;
}

const sampleTimer = setInterval(() => {
  const currSnap = cpuSnapshot();
  const usage = cpuUsagePercent(prevSnap, currSnap);
  prevSnap = currSnap;
  cpuSamples.push(usage);

  const avg = cpuSamples.average();
  const uptime = formatUptime(Date.now() - startTime);
  const state = miner.state;

  console.log(`CPU: ${usage.toFixed(1)}% (avg ${avg.toFixed(1)}%) | State: ${state} | Uptime: ${uptime} | Samples: ${cpuSamples.size}/${SAMPLES_IN_WINDOW}`);

  // Need at least a full window before making decisions
  if (cpuSamples.size < SAMPLES_IN_WINDOW) {
    console.log(`Waiting for full window (${cpuSamples.size}/${SAMPLES_IN_WINDOW})...`);
    return;
  }

  // State machine
  if (state === 'idle' && avg < config.idleThreshold) {
    console.log(`CPU idle (avg ${avg.toFixed(1)}% < ${config.idleThreshold}%) — starting miner`);
    miner.start();
  } else if (state === 'running' && avg > config.busyThreshold) {
    console.log(`CPU busy (avg ${avg.toFixed(1)}% > ${config.busyThreshold}%) — pausing miner`);
    miner.pause();
  } else if (state === 'paused' && avg < config.idleThreshold) {
    console.log(`CPU idle again (avg ${avg.toFixed(1)}% < ${config.idleThreshold}%) — resuming miner`);
    miner.start();
  }
}, config.sampleInterval * 1000);

// ─── Periodic GC ──────────────────────────────────────────────────────────────

const gcTimer = setInterval(() => {
  if (typeof global.gc === 'function') {
    global.gc();
    console.log('Manual GC triggered');
  }
}, config.gcInterval * 1000);

// ─── Graceful Shutdown ────────────────────────────────────────────────────────

let shuttingDown = false;

function shutdown(signal) {
  if (shuttingDown) return;
  shuttingDown = true;
  console.log(`\nShutdown signal received (${signal})`);
  clearInterval(sampleTimer);
  clearInterval(gcTimer);
  miner.kill();
  const uptime = formatUptime(Date.now() - startTime);
  console.log(`Final uptime: ${uptime}`);
  console.log('Goodbye.');
  process.exit(0);
}

process.on('SIGINT', () => shutdown('SIGINT'));
process.on('SIGTERM', () => shutdown('SIGTERM'));
if (IS_WIN) {
  process.on('SIGBREAK', () => shutdown('SIGBREAK'));
}
