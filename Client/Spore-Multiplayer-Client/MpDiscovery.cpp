/****************************************************************************
* Spore Multiplayer
****************************************************************************/
#include "stdafx.h"
#include "MpDiscovery.h"
#include "MpLog.h"

#include <winsock2.h>
#include <ws2tcpip.h>

#include <mutex>

#pragma comment(lib, "ws2_32.lib")

namespace SporeMP
{
	namespace
	{
		const uintptr_t kInvalidSocket = static_cast<uintptr_t>(INVALID_SOCKET);

		SOCKET ToSocket(uintptr_t value)
		{
			return static_cast<SOCKET>(value);
		}

		uint64_t NowMs()
		{
			return GetTickCount64();
		}

		/// Makes sure Winsock is initialized (the network client does the same).
		bool EnsureWinsock()
		{
			static std::once_flag flag;
			static bool result = false;

			std::call_once(flag, []()
			{
				WSADATA data;
				result = WSAStartup(MAKEWORD(2, 2), &data) == 0;
			});

			return result;
		}
	}

	Discovery::Discovery()
	{
	}

	Discovery& Discovery::Get()
	{
		static Discovery instance;
		return instance;
	}

	bool Discovery::Start(uint16_t port)
	{
		if (mSocket != kInvalidSocket) return true;
		if (!EnsureWinsock()) return false;

		SOCKET sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
		if (sock == INVALID_SOCKET)
		{
			MP_LOG_WARN("Could not create the discovery socket (error %d)", WSAGetLastError());
			return false;
		}

		// Several game instances on the same computer must be able to listen at
		// the same time, so the address is shared.
		BOOL reuse = TRUE;
		setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&reuse), sizeof(reuse));

		sockaddr_in address;
		std::memset(&address, 0, sizeof(address));
		address.sin_family = AF_INET;
		address.sin_port = htons(port);
		address.sin_addr.s_addr = htonl(INADDR_ANY);

		if (bind(sock, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == SOCKET_ERROR)
		{
			MP_LOG_WARN("Could not listen for LAN sessions on port %u (error %d)", port, WSAGetLastError());
			closesocket(sock);
			return false;
		}

		u_long nonBlocking = 1;
		ioctlsocket(sock, FIONBIO, &nonBlocking);

		mSocket = static_cast<uintptr_t>(sock);
		mPort = port;

		MP_LOG_INFO("Listening for LAN sessions on UDP port %u", port);
		return true;
	}

	void Discovery::Stop()
	{
		if (mSocket != kInvalidSocket)
		{
			closesocket(ToSocket(mSocket));
			mSocket = kInvalidSocket;
		}
		mSessions.clear();
	}

	void Discovery::Update()
	{
		if (mSocket == kInvalidSocket) return;

		// Read every pending beacon.
		while (true)
		{
			MsgDiscoveryBeacon beacon;
			sockaddr_in from;
			int fromLength = sizeof(from);

			const int received = recvfrom(ToSocket(mSocket), reinterpret_cast<char*>(&beacon), sizeof(beacon), 0,
				reinterpret_cast<sockaddr*>(&from), &fromLength);

			if (received == SOCKET_ERROR)
			{
				const int error = WSAGetLastError();
				if (error != WSAEWOULDBLOCK)
				{
					MP_LOG_DEBUG("Discovery recvfrom failed (error %d)", error);
				}
				break;
			}

			if (received < static_cast<int>(sizeof(MsgDiscoveryBeacon))) continue;
			if (beacon.magic != kDiscoveryMagic) continue;
			if (beacon.protocolVersion != kProtocolVersion) continue;

			char address[64] = { 0 };
			inet_ntop(AF_INET, &from.sin_addr, address, sizeof(address));

			// Update the existing entry or add a new one.
			bool found = false;
			for (DiscoveredSession& session : mSessions)
			{
				if (session.address == address && session.port == beacon.port)
				{
					session.name = beacon.sessionName;
					session.players = beacon.players;
					session.maxPlayers = beacon.maxPlayers;
					session.lastSeen = NowMs();
					found = true;
					break;
				}
			}

			if (!found)
			{
				DiscoveredSession session;
				session.name = beacon.sessionName;
				session.address = address;
				session.port = beacon.port;
				session.players = beacon.players;
				session.maxPlayers = beacon.maxPlayers;
				session.lastSeen = NowMs();
				mSessions.push_back(session);

				MP_LOG_INFO("Found the session '%s' at %s:%u (%d/%d players)",
					session.name.c_str(), session.address.c_str(), session.port,
					session.players, session.maxPlayers);
			}
		}

		// Drop the sessions that stopped sending beacons.
		const uint64_t now = NowMs();
		for (auto it = mSessions.begin(); it != mSessions.end();)
		{
			if (now - it->lastSeen > static_cast<uint64_t>(kDiscoveryTimeoutMs))
			{
				MP_LOG_INFO("The session '%s' is gone", it->name.c_str());
				it = mSessions.erase(it);
			}
			else
			{
				++it;
			}
		}
	}
}
