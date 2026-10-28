<p align=center>
<img style="width:160px;" src="refs/ICON.png" alt="Spore Multiplayer">
<h1 align=center>Spore Multiplayer</h1>
<p align=center>A co-op multiplayer mod for Spore, built on the <a href="https://github.com/Spore-Community/Spore-ModAPI">Spore ModAPI SDK</a>.</p>
</p>

---

## What this is

Spore has no multiplayer. This project adds one: a **client mod** (a `.dll` loaded by the ModAPI
Launcher) and a **dedicated server** (`Spore-Multiplayer-Server.exe`). Several players can connect
to the same server, see each other in the world, and chat.

The mod is **co-op, not competitive**: every player keeps their own save and their own galaxy.
The server does not simulate Spore — it only relays the player avatars between the clients.
That keeps it compatible with mods, saves and the single player campaign.

### Current features

| Feature | Status |
|---|---|
| Dedicated server (TCP, up to 8 players) | Done |
| Handshake, protocol versioning, player list | Done |
| Player state sync (position + orientation, 15 Hz) | Done |
| Remote avatars in the **Space** stage (ships) | Done |
| Remote avatars in the **Creature** and **Tribe** stages | Done |
| Exact creations shared between players (creation PNGs) | Done |
| In-game chat relay | Done |
| On-screen overlay (players, ping, chat) | Done |
| Console commands (`mp_*`) | Done |
| Interpolation of remote movement | Done |
| Configuration files (`server.ini`, `SporeMultiplayer.ini`) | Done |
| Server console (`list`, `kick`, `say`, `status`) | Done |
| Automated server integration test | Done |
| Cell / Tribe / Civ stage sync | Planned |
| Shared galaxy / shared economy | Not possible without deeper engine work |

---

## Repository layout

```
Client/Spore-Multiplayer-Client/   The mod DLL (Spore ModAPI)
Server/Spore-Multiplayer-Server/   The dedicated server (plain Win32 console app)
Server/Spore-Multiplayer-Master/   The master server (HTTP + JSON API)
Server/Spore-Multiplayer-Relay/    The relay server (NAT traversal)
Server/Spore-Multiplayer-Launcher/ The launcher window (WebView2 + local HTTP API)
Shared/MpProtocol.h                The game wire protocol, used by both sides
Shared/MpMasterProtocol.h          The master HTTP/JSON protocol
Shared/MpRelayProtocol.h           The relay handshake
Tools/test-server.ps1              Integration test: fake clients against the real server
Tools/test-master.ps1              Integration test for the master server
Tools/test-discovery.ps1           Integration test for the LAN discovery
Tools/test-relay.ps1               Integration test for the relay forwarding
Tools/test-relay-host.ps1          End-to-end test: game protocol through the relay
Tools/fake-client.ps1              A fake client that joins and flies around
Tools/download-test-creations.py   Fetches real creations from the Sporepedia
Tools/send-console-command.ps1     Sends a command to the mod console
Tools/make-icon.ps1                Regenerates refs/ICON.ico from refs/ICON.png
refs/                              Icon and other reference assets
Spore-ModAPI/                      Submodule with the SDK
```

---

## Building

### Requirements

- **Visual Studio 2022 or newer** with *Desktop development with C++* (x86 toolset).
- The **Spore ModAPI Launcher Kit** installed in `C:\ProgramData\SPORE ModAPI Launcher Kit`
  (the client DLL is copied to its `mLibs` folder, which is where the launcher reads mods from).

If your launcher lives somewhere else, edit `SporeLauncherPath` in
`Client/Spore-Multiplayer-Client/SdkPathConfig.props`.

### Client (the mod)

1. Make sure the submodule is up to date:
   ```
   git submodule update --init --recursive
   ```
2. Open `Spore-Multiplayer.sln` and build the **Spore-Multiplayer-Client** project
   (`Release | Win32`).

The result is `Spore-Multiplayer-Client.dll` in the launcher `mLibs` folder. Start Spore through
the **Spore ModAPI Launcher** and the mod is loaded.

From the command line:

```
msbuild Client\Spore-Multiplayer-Client\Spore-Multiplayer-Client.vcxproj /p:Configuration=Release /p:Platform=Win32
```

### Server

```
msbuild Server\Spore-Multiplayer-Server\Spore-Multiplayer-Server.vcxproj /p:Configuration=Release /p:Platform=Win32
```

The result is `bin\Release\Spore-Multiplayer-Server.exe`.

### Tests

```
powershell -ExecutionPolicy Bypass -File Tools\test-server.ps1
```

The script starts the server on a random port, connects several fake clients and checks the
handshake, the player list, the state relay, the chat, the ping, the protocol mismatch rejection
and the disconnect notifications. It needs no game installation.

### Testing with two game instances on one computer

Spore refuses to start a second time, and the ModAPI Launcher stops with a `GameAlreadyRunning`
message. For local testing you can start the game twice and load the mod manually:

1. Start the first instance through the launcher (`Spore ModAPI Launcher.exe -w`).
2. Start the second instance directly, allowing multiple copies:
   ```
   SporeApp.exe -w -multipleInstances
   ```
3. Load the ModAPI into the second instance with the bundled injector:
   ```
   Tools\mp-inject\mp-inject.exe SporeApp "C:\ProgramData\SPORE ModAPI Launcher Kit\SporeModAPI-steam_patched.dll"
   ```
   (`SporeModAPI.disk.dll` for the disk version. Build the injector once with
   `Tools\mp-inject\build.cmd`.)

Both instances then share the same `SporeMultiplayer.ini`, so set `auto_connect=1` and give each
player a different name with `mp_name`.

`Tools\fake-client.ps1 -Name Bob` connects a fake client that flies in circles, which is enough to
check the avatars and the chat without a second game.

To test the creation sharing without creating anything by hand, download a few real creations
from the Sporepedia (the files are Model-in-Picture PNGs, exactly what the game uses):

```
python Tools\download-test-creations.py --count 3
```

Then make the fake client upload one of them:

```
powershell -File Tools\fake-client.ps1 -Name Bob -Asset refs\test-creations\creature_500262075795.png
```

The game should log `Imported the avatar creation of player N`, and the creation appears in the
Sporepedia of the receiving player.

> **Note:** Spore's `-devDirs` flag creates an isolated `UserData` folder, which is handy for
> testing without touching your real creations. It also breaks some of the game's SPUI layouts, so
> use it only for tests and never for a normal session.

---

## Running a session

### 1. Start the server

On the machine that will host the game:

```
Spore-Multiplayer-Server.exe --name "My Co-op" --port 7000
```

Or edit `server.ini` next to the executable and just run it. Options can also be given on the
command line; the command line always wins over the ini file.

To make the session public, register it on a master server:

```
Spore-Multiplayer-Server.exe --name "My Co-op" --port 7000 --master play.dartpower.xyz:17401
```

The server then keeps its entry alive with heartbeats and removes it on exit.

Console commands: `help`, `list`, `status`, `say <text>`, `kick <id> [reason]`, `stop`.

If the server is reachable from the internet, forward the TCP port.

### Master server

The master keeps the public list of sessions and speaks plain HTTP + JSON, so the mod, a
launcher or a web page can all read it:

```
Spore-Multiplayer-Master.exe --port 17401
```

| Endpoint | Description |
|---|---|
| `GET /api/v1/servers` | List of public sessions |
| `GET /api/v1/health` | Health check and uptime |
| `POST /api/v1/register` | Register a session, returns a token |
| `POST /api/v1/heartbeat` | Keep a session alive (the server does this automatically) |
| `POST /api/v1/unregister` | Remove a session |

```bash
curl https://play.dartpower.xyz:17401/api/v1/servers
```

The public address of a session is taken from the connection, so the game server only has to
reach the master — no extra configuration. Sessions that stop sending heartbeats are dropped
after `timeout` seconds.

### Relay (no port forwarding)

If the host cannot open a port (NAT, CGNAT, corporate network), start a relay and point the
server at it. Both sides connect out, so no forwarding is needed:

```
Spore-Multiplayer-Relay.exe --port 17402
Spore-Multiplayer-Server.exe --port 7000 --relay play.dartpower.xyz:17402
```

The server logs the ticket it got and publishes it to the master, so other players just run
`mp_internet` and `mp_join <n>`. Manually, the ticket is used like this:

```
mp_relay play.dartpower.xyz 17402 <ticket>
```

A relay room carries one player at a time; the server reconnects after each one leaves.

### 2. Configure the client

`SporeMultiplayer.ini` is created next to the mod DLL the first time the game starts:

```ini
[connection]
host=127.0.0.1
port=7000
name=
auto_connect=0

[display]
hud=1
hud_x=0.02
hud_y=0.35
hud_scale=1.0
chat_lines=6

[sync]
send_rate=15
show_avatars=1
interpolation=1
```

### 3. Play

The easiest way: type `mp_host` in the console window (or in the Spore console, Ctrl+Shift+C).
The mod starts the dedicated server, registers it on the master and joins it. Other players run
`mp_servers` for the local network or `mp_internet` for the public list, then `mp_join 1`.

To join a specific server by address:

```
mp_connect 192.168.1.10 7000
mp_say Hello!
```

You can also set `auto_connect=1` to join as soon as the game starts.

---

## Console commands

| Command | Description |
|---|---|
| `mp_connect [host] [port]` | Connect to a server (defaults come from the ini file) |
| `mp_host [port] [name]` | Start a session and join it |
| `mp_servers` | List the sessions on the local network |
| `mp_internet` | List the sessions registered on the master server |
| `mp_relay <host> <port> <ticket>` | Join a session through a relay (no port forwarding needed) |
| `mp_join [number]` | Join a session from the `mp_servers` list |
| `mp_disconnect` | Leave the session |
| `mp_name <name>` | Change the name shown to the other players |
| `mp_say <text>` | Send a chat message |
| `mp_status` | Connection state, ping, number of players and avatars |
| `mp_list` | List the players in the session |
| `mp_hud [on\|off]` | Show or hide the overlay |
| `mp_avatars [on\|off]` | Show or hide the other players in the world |
| `mp_reload` | Reload `SporeMultiplayer.ini` |
| `mp_help` | List the commands |

---

## How it works
```
Spore (client A)  ──┐
Spore (client B)  ──┼── TCP ──►  Spore-Multiplayer-Server.exe  ◄── TCP ──  Spore (client C)
Spore (client D)  ──┘
```

- The client samples the local player (ship in the Space stage, avatar in the Creature stage)
  and sends `LocalState` at the configured rate.
- The server keeps the last known state of every player and broadcasts them together at
  `tick_rate` Hz, always excluding the receiver.
- Each client interpolates the received states and teleports a visual-only copy of the other
  player's ship/creature to that position. Those copies have their AI behaviour list cleared, so
  they never simulate anything and cannot damage the local save.
- Players are only shown when they are in the same stage **and** in the same solar system /
  planet as you, so a player flying in another system does not produce a ship in your sky.
- Chat, joins and leaves are relayed by the server and printed both in the overlay and in the
  Spore console.

### Sharing the actual creations

Spore saves every creation as a **Model-in-Picture PNG**: a normal image with the creation data
hidden inside it. That makes it possible to share the *exact* creature or ship of a player:

1. When a player joins, the client looks up the PNG of its own avatar (from the game's
   key → path map, or by scanning the creations folder) and sends it to the server.
2. The server caches it and forwards it to everyone, including players that join later.
3. Each receiver writes the PNG to `%TEMP%\SporeMultiplayer\` and imports it with
   `Thumbnail_cImportExport::ImportPNG()`, which adds the creation to the Sporepedia **only if it
   is not there yet**.

Nothing that already exists is replaced: your own creations and your save are never modified.
If the transfer fails or is disabled, the mod falls back to the local ship/species, so the avatar
is always visible.

The protocol lives in `Shared/MpProtocol.h`: a 4-byte header (`uint16 type`, `uint16 size`)
followed by a packed payload. Both sides are 32-bit little-endian, so the structures are sent in
memory order. Creation files are sent in `AssetChunk` messages of 1 KB, with a maximum size of
512 KB.

---

## Limitations

- Only the **player avatars** are shared. NPCs, cities, empires and the galaxy are per-save and
  are not synchronised.
- Cell, Tribe and Civilization stages are not synced yet (the player list and chat still work).
- The remote avatars are visual only: they do not collide, shoot or interact.
- There is no authentication; run the server on a trusted network or behind a firewall.

---

## License

The mod uses the Spore ModAPI SDK, which is GPLv3. See the upstream repository for details.
