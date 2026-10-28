# Master

`Spore-Multiplayer-Master.exe` — keeps the list of public sessions so players can find each
other over the internet.

It is a small HTTP server with a JSON API, which means the list can be read by the mod, by a
launcher, by a web page or by `curl`.

| File | Purpose |
|---|---|
| `main.cpp` | Command line / `master.ini` parsing, console input |
| `MpMaster.*` | The HTTP server, the session registry and the timeout logic |
| `master.ini` | Default configuration |

## Running

```
Spore-Multiplayer-Master.exe --port 17401
```

| Option | Default | Description |
|---|---|---|
| `--port` | `17401` | HTTP port |
| `--bind` | `0.0.0.0` | Address to bind |
| `--heartbeat` | `15` | Expected heartbeat interval, in seconds |
| `--timeout` | `45` | Drop a session after this long without heartbeats |
| `--log-level` | `info` | `debug`, `info`, `warning` or `error` |
| `--config` | `master.ini` | Configuration file next to the executable |

Console commands: `list`, `status`, `stop`.

## API

| Method | Path | Body / response |
|---|---|---|
| `GET` | `/api/v1/servers` | `{"count":1,"servers":[{"name","host","port","players","maxPlayers","version"}]}` |
| `GET` | `/api/v1/health` | `{"ok":true,"servers":1,"uptime":123,"version":1}` |
| `POST` | `/api/v1/register` | `{"name","port","players","maxPlayers","version"}` → `{"ok","token","interval","publicPort"}` |
| `POST` | `/api/v1/heartbeat` | `{"token","players"}` → `{"ok":true}` |
| `POST` | `/api/v1/unregister` | `{"token"}` → `{"ok":true}` |

The public address of a session is taken from the connection (or from `X-Forwarded-For` when
running behind a reverse proxy), so a game server only has to reach the master; no extra
configuration is needed.

```bash
curl http://127.0.0.1:17401/api/v1/servers
```

The build output goes to `bin\Release\Spore-Multiplayer-Master.exe` at the repository root.
