/****************************************************************************
* Spore Multiplayer
*
* LAN session discovery.
*
* The server broadcasts a UDP beacon every couple of seconds; this class listens
* to those beacons and keeps the list of sessions that are available on the
* local network. That way a player can join a game without knowing any IP.
*
* The socket is non blocking and it is polled from the game thread, so no extra
* thread is needed.
****************************************************************************/
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "../../Shared/MpProtocol.h"

namespace SporeMP
{
	/// A session announced by a server on the local network.
	struct DiscoveredSession
	{
		std::string name;
		std::string address;
		uint16_t port = 0;
		int players = 0;
		int maxPlayers = 0;

		/// Milliseconds since the last beacon, used to remove dead sessions.
		uint64_t lastSeen = 0;
	};

	class Discovery
	{
	public:
		static Discovery& Get();

		/// Opens the UDP socket. The port is the one used by the servers.
		bool Start(uint16_t port);

		void Stop();

		/// Reads the pending beacons. Called every frame from the session.
		void Update();

		/// Sessions seen during the last few seconds, most recent first.
		const std::vector<DiscoveredSession>& GetSessions() const { return mSessions; }

		bool IsRunning() const { return mSocket != static_cast<uintptr_t>(~0); }

	private:
		Discovery();

		uintptr_t mSocket = static_cast<uintptr_t>(~0);
		uint16_t mPort = 0;
		std::vector<DiscoveredSession> mSessions;
	};
}
