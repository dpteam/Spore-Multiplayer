/****************************************************************************
* Spore Multiplayer
*
* Relay protocol.
*
* The relay lets players connect when a direct connection is impossible: NAT
* without port forwarding, CGNAT, corporate firewalls, broken NAT hairpin...
*
* Both sides connect *out* to the relay, so neither needs an open port:
*
*     Spore client  ──connect──►  Relay  ◄──connect──  Spore server
*
* The relay does not understand the game protocol at all: it just forwards
* bytes. Everything is authenticated by a short "ticket" so a stranger cannot
* join somebody else's room.
*
* Handshake, always as the first bytes of a connection:
*
*     [uint16 magic][uint8 role][uint8 reserved][char ticket[16]]
*
*   role 1 = host   : waits for a client, then forwards both directions
*   role 2 = client : connects to the host of that ticket
*
* After the handshake the relay answers with a single byte:
*
*     kRelayOk (0)          - connected, raw game traffic follows
*     kRelayWaiting (1)     - host: waiting for a client (client only gets this
*                             if it connects first, which is an error)
*     kRelayNoRoom (2)      - unknown or already used ticket
*     kRelayFull (3)        - the room already has its two sides
*
* The game protocol is unchanged: once the handshake is done, the client talks
* to the server exactly as if it had connected directly.
****************************************************************************/
#pragma once

#include <cstdint>
#include <cstddef>
#include <cstring>

namespace SporeMP
{
	/// Magic at the start of a relay handshake ("RL" in ASCII).
	constexpr uint16_t kRelayMagic = 0x4C52;

	/// Default TCP port of the relay.
	constexpr uint16_t kDefaultRelayPort = 17402;

	/// Length of a relay ticket, in bytes.
	constexpr size_t kRelayTicketLength = 16;

	/// Bytes of the handshake: magic + role + reserved + ticket.
	constexpr size_t kRelayHandshakeSize = 2 + 1 + 1 + kRelayTicketLength;

	/// Result byte sent by the relay after the handshake.
	enum class RelayResult : uint8_t
	{
		Ok = 0,
		Waiting = 1,
		NoRoom = 2,
		Full = 3,
	};

	/// Role byte of the handshake.
	enum class RelayRole : uint8_t
	{
		Host = 1,
		Client = 2,
	};

	/// A room is dropped if nobody connects within this time, in seconds.
	constexpr int kRelayRoomTimeoutSeconds = 120;

	/// Bytes forwarded per read.
	constexpr size_t kRelayBufferSize = 8192;
}
