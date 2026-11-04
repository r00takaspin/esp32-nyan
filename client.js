// Client half: a small status pill in the composer dock.
// It polls the Host's local status endpoint (127.0.0.1:8788) so the page shows
// whether the ESP32 Agent Buddy is connected and what it is displaying.

window.__ModuleLoader__.load({
  id: '@local/dsh-agent-buddy',
  factory(require) {
    const React = require('react')
    const h = React.createElement

    function BuddyPill() {
      const [s, setS] = React.useState({ state: 'idle', connected: false })

      React.useEffect(() => {
        let alive = true
        const tick = () => {
          fetch('http://127.0.0.1:8788/status', { cache: 'no-store' })
            .then((r) => r.json())
            .then((j) => {
              if (alive) setS(j)
            })
            .catch(() => {
              if (alive) setS({ state: 'idle', connected: false })
            })
        }
        tick()
        const id = setInterval(tick, 2000)
        return () => {
          alive = false
          clearInterval(id)
        }
      }, [])

      const color = !s.connected
        ? '#8a8f99'
        : s.state === 'approve'
          ? '#ffcc33'
          : s.state === 'error'
            ? '#ff5555'
            : s.state === 'think'
              ? '#4cc2ff'
              : '#39d98a'
      const label = s.connected ? s.state : 'offline'

      return h(
        'div',
        {
          title: 'Agent Buddy (ESP32): ' + label,
          style: {
            display: 'flex',
            alignItems: 'center',
            gap: 6,
            padding: '2px 8px',
            fontSize: 12,
            opacity: 0.85,
            userSelect: 'none',
          },
        },
        h('span', {
          style: {
            width: 8,
            height: 8,
            borderRadius: 4,
            background: color,
            display: 'inline-block',
          },
        }),
        'Agent Buddy · ' + label,
      )
    }

    return {
      inject: ['slots'],
      apply(ctx) {
        ctx.slots.inject('conversation.composer.dock', () =>
          ctx.slots.register(
            { name: 'conversation.composer.dock', id: 'agent-buddy-pill', order: 7 },
            BuddyPill,
          ),
        )
      },
    }
  },
})
