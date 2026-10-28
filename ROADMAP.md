# Roadmap

Ideas and planned work for Spore Multiplayer, roughly ordered by priority.

## Done

- Dedicated server + client mod, TCP protocol, up to 8 players
- Player state sync, remote avatars (Space / Creature / Tribe)
- Creation sharing as Model-in-Picture PNGs
- Chat, HUD overlay, `mp_*` console commands
- LAN session discovery (UDP beacons)
- GitHub Actions build + integration tests

## Short term

### In-game console instead of the overlay
`AllocConsole()` + a Win32 console window that also mirrors the log file, so the
overlay can stay off by default and the game view stays clean. Commands stay
available through the Spore console (Ctrl+Shift+C) and through the new console.

### Master server + server browser
- Master server on its own port (e.g. `play.dartpower.xyz:17401`) that keeps a
  list of registered sessions.
- Servers register themselves on start and send heartbeats; the master drops
  them after a timeout.
- Clients query the master for the public server list.
- A small launcher / WebUI (layout ideas from
  [DPTDOOM](https://github.com/dpteam/DPTDOOM)) that lists servers and launches
  the game with the right arguments.

### Relay server
Port `17402`. For players behind NAT / CGNAT / without port forwarding:
the relay forwards the TCP stream between a host and a client, so no direct
connection is needed. The server browser should prefer a direct connection and
fall back to the relay when a direct attempt fails (NAT hairpin issues).

## Medium term

### SPUI / State* integration
Replace the plain overlay with proper Spore UI:
- A `.spui` layout for the multiplayer menu (host / join / server list) loaded
  through `UILayout`, so it matches the game's look and scales with the UI.
- Use the game's `UIStateMachine` / `IStateManager` to add a real multiplayer
  state instead of drawing over whatever state is active.
- Proper buttons and text fields instead of console commands.

## Long term

### Trace / diagnostics
Structured tracing of the network layer (state snapshots, asset transfers,
latency, desync detection) written to a rotating log, plus a viewer that can
replay a session for debugging.

### Deeper co-op
- Sync NPCs / cities / empires (needs engine-level work)
- Cell and Civilization stage avatars
- Shared missions and goals
