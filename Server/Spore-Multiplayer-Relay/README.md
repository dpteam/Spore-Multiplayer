# Relay

`Spore-Multiplayer-Relay.exe` — lets players connect when a direct connection is impossible:
NAT without port forwarding, CGNAT, strict firewalls, broken NAT hairpin.

Both sides connect **out** to the relay, so nobody needs an open port:

```
Spore client  ──connect──►  Relay  ◄──connect──  Spore server
```

The relay does not parse the game protocol at all: after a small handshake it copies bytes in
both directions, so it keeps working with any future protocol version.

| File | Purpose |
|---|---|
| `main.cpp` | Command line / `relay.ini` parsing, console input |
| `MpRelay.*` | The TCP server, the rooms and the byte forwarding |
| `relay.ini` | Default configuration |

## Running

```
Spore-Multiplayer-Relay.exe --port 17402
```

| Option | Default | Description |
|---|---|---|
| `--port` | `17402` | TCP port |
| `--bind` | `0.0.0.0` | Address to bind |
| `--timeout` | `120` | Drop a waiting room after this long |
| `--log-level` | `info` | `debug`, `info`, `warning` or `error` |
| `--config` | `relay.ini` | Configuration file next to the executable |

Console commands: `list`, `status`, `stop`.

## Handshake

The first bytes of every connection are:

```
[uint16 magic "RL"][uint8 role][uint8 reserved][char ticket[16]]
```

| Role | Meaning |
|---|---|
| `1` | Host: creates a room and waits for one client |
| `2` | Client: connects to the room of that ticket |

The relay answers with a single byte: `0` ok, `2` no such room, `3` room already taken. After
that, the raw game protocol flows through unchanged.

## Using it

Server side (no port forwarding needed):

```
Spore-Multiplayer-Server.exe --relay play.dartpower.xyz:17402
```

The server logs the ticket it received, and publishes it to the master server, so the session
shows up in `mp_internet` with the relay address.

Client side:

```
mp_relay play.dartpower.xyz 17402 <ticket>
```

or simply `mp_join <n>` after `mp_internet`, which uses the relay automatically when the session
publishes one.

## Limitation

A room carries **one client at a time**. The host reconnects after each player leaves, so a
session can be joined by several players in turn, but not simultaneously through the relay. For
simultaneous players, forward the game port and let them connect directly.
