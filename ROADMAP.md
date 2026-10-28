# Roadmap

Ideas and planned work for Spore Multiplayer, roughly ordered by priority.

## Done

- Dedicated server + client mod, TCP protocol, up to 8 players
- Player state sync, remote avatars (Space / Creature / Tribe)
- Creation sharing as Model-in-Picture PNGs
- Chat, HUD overlay, `mp_*` console commands
- LAN session discovery (UDP beacons)
- Hosting from the game (`mp_host`)
- Master server with an HTTP/JSON API and registration/heartbeats
- Relay server (port 17402) for NAT/CGNAT players without port forwarding
- Launcher window (WebView2, with a browser fallback) and its server list
- In-game console window that mirrors the log
- GitHub Actions build + integration tests

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
