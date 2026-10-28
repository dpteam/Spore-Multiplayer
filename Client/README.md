# Client

The Spore mod (`Spore-Multiplayer-Client.dll`), loaded by the Spore ModAPI Launcher.

| File | Purpose |
|---|---|
| `dllmain.cpp` | Entry point: logging, session, overlay, console commands, per-frame update |
| `MpNet.*` | Non-blocking TCP client with its own worker thread |
| `MpSession.*` | Session state machine: handshake, player table, state sync, chat |
| `MpAvatars.*` | Spawns/moves the visual copies of the other players (ships, creatures) |
| `MpAssets.*` | Shares the exact creations as Model-in-Picture PNGs and imports them |
| `MpOverlay.*` | On-screen overlay with the players, the ping and the chat |
| `MpMenu.*` | In-game menu built with the game's own UI toolkit (UTFWin) |
| `MpCheats.*` | The `mp_*` console commands |
| `MpConfig.*` | Reads/writes `SporeMultiplayer.ini` |
| `MpLog.*` | Log file (`SporeMultiplayer.log`) |
| `MpConsole.*` | Console window that mirrors the log and accepts commands |
| `MpDiscovery.*` | Finds sessions on the local network (UDP beacons) |
| `MpHost.*` | Starts the dedicated server from inside the game |
| `MpMaster.*` | Queries the master server for the public session list |

Build it with the `Spore-Multiplayer-Client` project in the solution. The DLL is copied to the
`mLibs` folder of the ModAPI Launcher Kit, which is where the launcher picks mods from.

The shared wire protocol is in `../../Shared/MpProtocol.h`.

See the repository `README.md` for the build and usage instructions.
