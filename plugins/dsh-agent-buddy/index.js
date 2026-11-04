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

// The board font is ASCII only: transliterate Cyrillic so Russian reasons
// stay readable instead of turning into glyph noise.
const RU_ASCII = {
  'а': 'a', 'б': 'b', 'в': 'v', 'г': 'g', 'д': 'd', 'е': 'e', 'ё': 'e',
  'ж': 'zh', 'з': 'z', 'и': 'i', 'й': 'y', 'к': 'k', 'л': 'l', 'м': 'm',
  'н': 'n', 'о': 'o', 'п': 'p', 'р': 'r', 'с': 's', 'т': 't', 'у': 'u',
  'ф': 'f', 'х': 'h', 'ц': 'c', 'ч': 'ch', 'ш': 'sh', 'щ': 'sch', 'ъ': '',
  'ы': 'y', 'ь': '', 'э': 'e', 'ю': 'yu', 'я': 'ya',
}

function toAsciiText(text) {
  return String(text).replace(/[\u0400-\u04FF]/g, (ch) => {
    const lower = ch.toLowerCase()
    const mapped = RU_ASCII[lower]
    if (mapped == null) return '?'
    return ch === lower ? mapped : mapped.charAt(0).toUpperCase() + mapped.slice(1)
  })
}

function clean(s) {
  return toAsciiText(String(s ?? ''))
    .replace(/[\r\n|]+/g, ' ')
    .slice(0, 170)
}

export function apply(ctx, config = {}) {
  const port = String(config.port || '/dev/cu.usbmodem1101')
  const python = pickPython(config.python ? String(config.python) : undefined)
  const approvalsOn = String(config.approvals ?? 'on') !== 'off'
  const timeoutMs = Number(config.timeoutMs || 6000)

  let bridge = null
  let deviceAlive = false    // true only after the board said "hello"
  let outBuf = ''
  const pending = new Map()
  let current = 'idle'
  let manualUntil = 0        // buddy_show owns the screen for a short while
  let reactionUntil = 0      // approved/rejected animation owns the screen

  function send(obj) {
    // Reattach when the bridge is missing or the process is already dead.
    if (!bridge || bridge.killed || bridge.exitCode != null) startBridge()
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
    if (!evt) return
    deviceAlive = true   // any line from the board proves the link is up
    if (evt.evt === 'hello') return
    if (evt.evt === 'approve' && evt.id != null) {
      const resolve = pending.get(String(evt.id))
      if (resolve) {
        pending.delete(String(evt.id))
        resolve(evt.ok ? 'allowed-once' : 'rejected')
      }
    }
  }

  function startBridge() {
    // Never leave a previous process behind: two bridges split the serial stream.
    try {
      bridge?.kill()
    } catch {
      /* ignore */
    }
    let child
    try {
      child = spawn(python, [join(HERE, 'bridge.py'), '--port', port], {
        stdio: ['pipe', 'pipe', 'ignore'],
      })
    } catch {
      bridge = null
      deviceAlive = false
      return
    }
    bridge = child
    deviceAlive = false
    child.stdout.setEncoding('utf8')
    child.stdout.on('data', (chunk) => {
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
    child.on('exit', () => {
      if (bridge === child) {
        bridge = null
        deviceAlive = false
      }
    })
    child.on('error', () => {
      if (bridge === child) {
        bridge = null
        deviceAlive = false
      }
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
        res.end(JSON.stringify({ state: current, connected: deviceAlive, build: "v4", timeout: timeoutMs }))
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
      if (Date.now() < reactionUntil) return
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

  let approveSeq = 0

  async function approvalHandler(req, next) {
    const key = String(req?.callId || req?.id || Date.now())
    if (seen.has(key)) return next()         // another registration already handled it
    seen.add(key)
    setTimeout(() => seen.delete(key), Math.max(timeoutMs, 60000))
    // The board keeps the id in a 48 byte buffer, so long call ids get
    // truncated and the tap would never match. Use a short device id.
    const id = 'a' + ++approveSeq

    // `tool` may be a descriptor object and `reason` may be structured:
    // only ever put real strings on the device.
    const asText = (v) => {
      if (typeof v === 'string') return v
      if (v && typeof v === 'object') {
        const t = v.en || v.ru || v.zh || v.message || v.text || v.description || v.label || v.reason
        return typeof t === 'string' ? t : ''
      }
      return ''
    }
    const toolName = typeof req?.tool === 'string' ? req.tool : req?.tool?.name || req?.tool?.id || ''
    const reasonText = asText(req?.displayReason) || asText(req?.reason)
    const label = [toolName, reasonText].filter(Boolean).join(': ') || 'Approval needed'
    const shown = show('approve', label, id)
    if (!approvalsOn || !shown) return next()

    const outcome = await new Promise((resolve) => {
      pending.set(id, resolve)
      setTimeout(() => {
        if (pending.delete(id)) resolve(null)
      }, timeoutMs)
    })
    // Let the device play its approved/rejected reaction before switching back.
    if (outcome !== null) reactionUntil = Date.now() + 3800
    setTimeout(() => show('think'), 3800)
    if (outcome === null) return next()      // no tap in time: let the GUI decide
    return outcome
  }

  function onAgentStatus(ev) {
    if (current === 'approve') return   // never cover a pending approval
    if (Date.now() < manualUntil) return
    if (Date.now() < reactionUntil) return
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
