# Server

`Spore-Multiplayer-Server.exe` — a standalone Win32 console application. It does **not** need
Spore installed and it does not simulate anything: it relays the player states and the chat
between the connected clients.

| File | Purpose |
|---|---|
| `main.cpp` | Command line / `server.ini` parsing, console input thread |
| `MpServer.*` | The `select()` based TCP server, the session and the relay logic |
| `MpMasterClient.*` | Registers the session on the master server |
| `MpLog.*` | Timestamped logging to stdout |
| `server.ini` | Default configuration |
| `Resource.rc` | Icon (from `refs/ICON.ico`) and version information |

## Running

```
Spore-Multiplayer-Server.exe --port 7000 --name "Spore Co-op" --max-players 8 --tick-rate 20
```

| Option | Default | Description |
|---|---|---|
| `--port` | `7000` | TCP port to listen on |
| `--bind` | `0.0.0.0` | Address to bind (`127.0.0.1` for local tests) |
| `--name` | `Spore Co-op` | Session name shown to the clients |
| `--max-players` | `8` | Maximum number of players (1–8) |
| `--tick-rate` | `20` | State broadcasts per second (1–60) |
| `--timeout` | `15` | Seconds without data before a player is dropped |
| `--log-level` | `info` | `debug`, `info`, `warning` or `error` |
| `--master` | *(empty)* | Master server URL, e.g. `http://host:17401` |
| `--config` | `server.ini` | Configuration file next to the executable |
| `--allow-duplicate-names` | off | Do not make player names unique |

Console commands: `help`, `list`, `status`, `say <text>`, `kick <id> [reason]`, `stop`.

The build output goes to `bin\Release\Spore-Multiplayer-Server.exe` at the repository root.
