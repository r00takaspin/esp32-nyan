// Host half of the Agent Buddy bundle.
//
// It keeps a small Python bridge (bridge.py) attached to the ESP32 over USB CDC
// and does two things:
//   1. answers `approval/request` with the physical YES / NO touch buttons;
//   2. exposes `buddy_show` so the agent can draw a message on the device.
//
// The bridge speaks JSON lines on stdio; the board speaks the pipe protocol
// documented in Arduino/nyancat/nyancat.ino.

import { execFileSync, spawn } from 'node:child_process'
import http from 'node:http'
import { dirname, join } from 'node:path'
import { fileURLToPath } from 'node:url'

const HERE = dirname(fileURLToPath(import.meta.url))
const STATUS_PORT = 8788

function pickPython(explicit) {
  const candidates = [
    explicit,
    'python3',
    'python',
    join(HERE, '..', '..', '.venv-esp', 'bin', 'python'),
  ]
  const seen = new Set()
  for (const p of candidates) {
    if (!p || seen.has(p)) continue
    seen.add(p)
    try {
      execFileSync(p, ['-c', 'import serial'], { stdio: 'ignore', timeout: 4000 })
      return p
    } catch {
      /* try the next interpreter */
    }
  }
  return explicit || 'python3'
}

export const name = 'agent-buddy'
export const inject = ['tools', 'agents']

function clean(s) {
  return String(s ?? '')
    .replace(/[\r\n|]+/g, ' ')
    .slice(0, 170)
}

export function apply(ctx, config = {}) {
  const port = String(config.port || '/dev/cu.usbmodem1101')
  const python = pickPython(config.python ? String(config.python) : undefined)
  const approvalsOn = String(config.approvals ?? 'on') !== 'off'
  const timeoutMs = Number(config.timeoutMs || 15000)

  let bridge = null
  let outBuf = ''
  const pending = new Map()
  let current = 'idle'
  let manualUntil = 0        // buddy_show owns the screen for a short while

  function send(obj) {
    if (!bridge) startBridge()   // reattach after the port was released (e.g. flashing)
    if (!bridge || !bridge.stdin || !bridge.stdin.writable) return false
    try {
      bridge.stdin.write(JSON.stringify(obj) + '\n')
      return true
    } catch {
      return false
    }
  }

  function show(state, text = '', id = '') {
    const ok = send({ cmd: 'state', state, text: clean(text), id: String(id || '') })
    if (ok) current = state
    return ok
  }

  function onEvent(evt) {
    if (evt && evt.evt === 'approve' && evt.id != null) {
      const resolve = pending.get(String(evt.id))
      if (resolve) {
        pending.delete(String(evt.id))
        resolve(evt.ok ? 'allowed-once' : 'rejected')
      }
    }
  }

  function startBridge() {
    try {
      bridge = spawn(python, [join(HERE, 'bridge.py'), '--port', port], {
        stdio: ['pipe', 'pipe', 'ignore'],
      })
    } catch {
      bridge = null
      return
    }
    bridge.stdout.setEncoding('utf8')
    bridge.stdout.on('data', (chunk) => {
      outBuf += chunk
      let i
      while ((i = outBuf.indexOf('\n')) >= 0) {
        const line = outBuf.slice(0, i).trim()
        outBuf = outBuf.slice(i + 1)
        if (!line) continue
        try {
          onEvent(JSON.parse(line))
        } catch {
          /* ignore malformed bridge output */
        }
      }
    })
    bridge.on('exit', () => {
      bridge = null
    })
    bridge.on('error', () => {
      bridge = null
    })
  }

  ctx.effect(() => {
    startBridge()
    show('idle')
    const statusServer = http.createServer((req, res) => {
      res.setHeader('Access-Control-Allow-Origin', '*')
      res.setHeader('Cache-Control', 'no-store')
      if (req.url === '/status') {
        res.setHeader('Content-Type', 'application/json')
        res.end(JSON.stringify({ state: current, connected: bridge != null }))
      } else {
        res.statusCode = 404
        res.end('not found')
      }
    })
    // A busy/closed port must never surface as an unhandled error that could
    // take down the host process.
    statusServer.on('error', () => {})
    try {
      statusServer.listen(STATUS_PORT, '127.0.0.1')
    } catch {
      /* port busy: the pill simply stays offline */
    }
    // Fallback in case `agent/status` events are not delivered in this profile:
    // poll the registry cheaply and keep idle/think in sync.
    const statusPoll = setInterval(() => {
      if (current === 'approve') return
      if (Date.now() < manualUntil) return
      try {
        const list = ctx.agents?.list?.() ?? []
        const running = list.some((a) => a?.status === 'running')
        show(running ? 'think' : 'idle')
      } catch {
        /* ignore */
      }
    }, 2000)
    return () => {
      for (const resolve of pending.values()) resolve('unavailable')
      pending.clear()
      clearInterval(statusPoll)
      try {
        statusServer.close()
      } catch {
        /* ignore */
      }
      try {
        bridge?.kill()
      } catch {
        /* ignore */
      }
      bridge = null
    }
  })

  // Physical YES / NO for tool approvals. The GUI registers its answerer on the
  // agent scope, so we also register there with `prepend` to be asked first.
  const seen = new Set()

  async function approvalHandler(req, next) {
    const id = String(req?.callId || req?.id || Date.now())
    if (seen.has(id)) return next()          // another registration already handled it
    seen.add(id)
    setTimeout(() => seen.delete(id), Math.max(timeoutMs, 60000))

    const label = req?.displayReason || req?.reason || req?.tool || 'Approval needed'
    const shown = show('approve', label, id)
    if (!approvalsOn || !shown) return next()

    const outcome = await new Promise((resolve) => {
      pending.set(id, resolve)
      setTimeout(() => {
        if (pending.delete(id)) resolve(null)
      }, timeoutMs)
    })
    show('think')
    if (outcome === null) return next()      // no tap in time: let the GUI decide
    return outcome
  }

  function onAgentStatus(ev) {
    if (current === 'approve') return   // never cover a pending approval
    if (Date.now() < manualUntil) return
    const s = ev?.status ?? ev
    if (s === 'idle') show('idle')
    else if (s === 'running') show('think')
  }

  function registerForAgent(agent) {
    try {
      agent?.ctx?.on?.('approval/request', approvalHandler, { prepend: true })
    } catch {
      /* ignore */
    }
    // `agent/status` is a Cordis event emitted with { status: 'idle' | 'running' }.
    try {
      agent?.ctx?.on?.('agent/status', onAgentStatus)
    } catch {
      /* ignore */
    }
    try {
      if (agent?.status === 'running') show('think')
    } catch {
      /* ignore */
    }
  }

  try {
    for (const a of ctx.agents?.list?.() ?? []) registerForAgent(a)
  } catch {
    /* ignore */
  }
  try {
    ctx.on('agent/created', (e) => registerForAgent(e?.agent ?? e))
  } catch {
    /* ignore */
  }
  try {
    ctx.on('approval/request', approvalHandler, { prepend: true })
  } catch {
    /* ignore */
  }
  try {
    ctx.on('agent/status', onAgentStatus)
  } catch {
    /* ignore */
  }

  // Let the agent draw on the device.
  try {
    ctx.tools.register({
      name: 'buddy_show',
        description:
          'Show a short message on the desk Agent Buddy device (a 172x320 ESP32 display) and pick its mood.',
        parameters: {
          text: { type: 'string', required: true, description: 'Message to display on the device' },
          mood: {
            type: 'string',
            description: 'One of: idle, think, tool, error (default tool)',
          },
        },
        output: {
          schema: { type: 'string' },
          render: (_args, value) => [{ type: 'text', text: value }],
        },
        async execute(args) {
          manualUntil = Date.now() + 8000   // hold buddy_show's mood for a while
          const ok = show(args.mood || 'tool', args.text)
          return ok ? `shown on Agent Buddy: ${args.text}` : 'Agent Buddy is not connected'
        },
    })
  } catch {
    /* tool registration is best-effort: never fail the host */
  }
}
