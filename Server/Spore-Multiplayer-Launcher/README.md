# Launcher

`Spore-Multiplayer-Launcher.exe` — a small window with the server list that starts Spore already
connected, so players never have to type an IP address.

| File | Purpose |
|---|---|
| `main.cpp` | Local HTTP server, JSON API, game launching |
| `MpLauncherUI.cpp` | The HTML/CSS/JS of the window (embedded in the exe) |
| `MpWebView.*` | WebView2 host that shows the UI in a real window |
| `webview2/` | WebView2 SDK headers and `WebView2Loader.dll` |
| `launcher.ini` | Default configuration |

## How it works

The UI is a plain HTML page. It is served by a small HTTP server on
`127.0.0.1` and shown in a **WebView2** window (the same approach as the DPTDOOM launcher), so
it looks like a native app while staying easy to change.

If the WebView2 runtime is not installed, the launcher opens the same page in the default
browser instead — one UI, two ways to show it.

## Running

```
Spore-Multiplayer-Launcher.exe
```

| Option | Default | Description |
|---|---|---|
| `--master <url>` | `play.dartpower.xyz:17401` | Master server for the list |
| `--port <number>` | pick a free port | Local HTTP port of the UI |
| `--no-browser` | off | Do not open the browser fallback |

## Configuration (`launcher.ini`)

| Key | Description |
|---|---|
| `master` | Master server shown in the list |
| `relay` | Relay used by the Host button |
| `launcher_kit` | Folder of the ModAPI Launcher Kit (detected automatically) |
| `client_ini` | `SporeMultiplayer.ini` to write (defaults to `<kit>\mLibs\SporeMultiplayer.ini`) |
| `server_exe` | `Spore-Multiplayer-Server.exe`, used by Host (looked up next to the launcher) |
| `bind`, `port` | Local address/port of the UI |

## What "Play" does

1. Writes the connection into `SporeMultiplayer.ini` (`auto_connect=1`, host, port, relay, ticket).
2. Starts `Spore ModAPI Launcher.exe -w`.
3. The mod reads the ini and connects by itself.

For **Host**, it first starts `Spore-Multiplayer-Server.exe` with `--master` and `--relay`, so the
session appears in the list and can be joined without port forwarding.

## Local API

| Method | Path | Description |
|---|---|---|
| `GET` | `/` | The launcher page |
| `GET` | `/api/servers` | Proxies `GET /api/v1/servers` of the master |
| `GET` | `/api/status` | Current configuration |
| `POST` | `/api/launch` | `{host, port, session?, relay?, ticket?, hostGame?}` |

Because the page and the API share one origin, there are no cross-origin requests and the master
server stays private.

## Requirements

- The **WebView2 runtime** is preinstalled on Windows 11 and current Windows 10; otherwise the
  launcher falls back to the browser.
- The build needs the WebView2 SDK, which is vendored in `webview2/` (no download at build time).
