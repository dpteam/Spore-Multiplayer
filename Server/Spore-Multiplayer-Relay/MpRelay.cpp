/****************************************************************************
* Spore Multiplayer - Relay server
****************************************************************************/
#include "MpRelay.h"
#include "MpLog.h"

#include <winsock2.h>
#include <ws2tcpip.h>

#pragma comment(lib, "ws2_32.lib")

#include <algorithm>
#include <cstdio>
#include <cstring>

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

		/// Ticket characters are hex, so they are safe in logs and URLs.
		bool IsValidTicket(const char* ticket, size_t length)
		{
			for (size_t i = 0; i < length; ++i)
			{
				const char c = ticket[i];
				const bool hex = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
				if (!hex) return false;
			}
			return true;
		}
	}

	Relay::Relay(const RelayConfig& config)
		: mConfig(config)
	{
	}

	Relay::~Relay()
	{
		if (mListenSocket != kInvalidSocket)
		{
			closesocket(ToSocket(mListenSocket));
			mListenSocket = kInvalidSocket;
		}
		WSACleanup();
	}

	bool Relay::Start()
	{
		WSADATA wsaData;
		if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
		{
			MP_LOG_ERROR("WSAStartup failed with error %d", WSAGetLastError());
			return false;
		}

		SOCKET listenSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
		if (listenSocket == INVALID_SOCKET)
		{
			MP_LOG_ERROR("Could not create the listening socket (error %d)", WSAGetLastError());
			return false;
		}

		BOOL reuse = TRUE;
		setsockopt(listenSocket, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&reuse), sizeof(reuse));

		sockaddr_in address;
		std::memset(&address, 0, sizeof(address));
		address.sin_family = AF_INET;
		address.sin_port = htons(mConfig.port);

		if (inet_pton(AF_INET, mConfig.bindAddress.c_str(), &address.sin_addr) != 1)
		{
			MP_LOG_WARN("Invalid bind address '%s', using 0.0.0.0", mConfig.bindAddress.c_str());
			address.sin_addr.s_addr = htonl(INADDR_ANY);
		}

		if (bind(listenSocket, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == SOCKET_ERROR)
		{
			const int error = WSAGetLastError();
			if (error == WSAEADDRINUSE)
			{
				MP_LOG_ERROR("Port %d is already in use", mConfig.port);
			}
			else
			{
				MP_LOG_ERROR("Could not bind port %d (error %d)", mConfig.port, error);
			}
			closesocket(listenSocket);
			return false;
		}

		if (listen(listenSocket, SOMAXCONN) == SOCKET_ERROR)
		{
			MP_LOG_ERROR("Could not listen on port %d (error %d)", mConfig.port, WSAGetLastError());
			closesocket(listenSocket);
			return false;
		}

		u_long nonBlocking = 1;
		ioctlsocket(listenSocket, FIONBIO, &nonBlocking);

		mListenSocket = static_cast<uintptr_t>(listenSocket);
		mRunning = true;

		MP_LOG_INFO("Relay server listening on %s:%d", mConfig.bindAddress.c_str(), mConfig.port);
		MP_LOG_INFO("Rooms time out after %d s, up to %zu rooms",
			mConfig.roomTimeoutSeconds, mConfig.maxRooms);
		return true;
	}

	void Relay::RequestStop()
	{
		mStopRequested.store(true);
	}

	void Relay::QueueCommand(const std::string& command)
	{
		std::lock_guard<std::mutex> lock(mCommandMutex);
		mCommands.push_back(command);
	}

	void Relay::Run()
	{
		while (mRunning && !mStopRequested.load())
		{
			fd_set readSet;
			FD_ZERO(&readSet);
			FD_SET(ToSocket(mListenSocket), &readSet);

			for (const auto& endpoint : mPending)
			{
				if (endpoint->socket != kInvalidSocket)
				{
					FD_SET(ToSocket(endpoint->socket), &readSet);
				}
			}

			for (auto& pair : mRooms)
			{
				Room& room = *pair.second;
				if (room.host && room.host->socket != kInvalidSocket)
				{
					FD_SET(ToSocket(room.host->socket), &readSet);
				}
				if (room.client && room.client->socket != kInvalidSocket)
				{
					FD_SET(ToSocket(room.client->socket), &readSet);
				}
			}

			timeval timeout;
			timeout.tv_sec = 0;
			timeout.tv_usec = 5000;  // 5 ms

			const int result = select(0, &readSet, nullptr, nullptr, &timeout);

			if (result == SOCKET_ERROR)
			{
				MP_LOG_ERROR("select() failed (error %d)", WSAGetLastError());
				break;
			}

			if (result > 0)
			{
				if (FD_ISSET(ToSocket(mListenSocket), &readSet))
				{
					AcceptConnections();
				}

				// Pending handshakes.
				for (size_t i = 0; i < mPending.size();)
				{
					std::unique_ptr<Endpoint>& endpoint = mPending[i];
					if (endpoint->socket == kInvalidSocket)
					{
						mPending.erase(mPending.begin() + i);
						continue;
					}

					if (FD_ISSET(ToSocket(endpoint->socket), &readSet))
					{
						ReadPendingHandshake(endpoint);

						// ReadPendingHandshake moves the endpoint into a room (and
						// clears the slot) once the handshake is complete.
						if (endpoint == nullptr)
						{
							mPending.erase(mPending.begin() + i);
							continue;
						}
					}

					++i;
				}

				// Iterate over a copy of the room keys: reading can close a room.
				std::vector<std::string> tickets;
				tickets.reserve(mRooms.size());
				for (const auto& pair : mRooms) tickets.push_back(pair.first);

				for (const std::string& ticket : tickets)
				{
					auto it = mRooms.find(ticket);
					if (it == mRooms.end()) continue;

					Room& room = *it->second;

					if (room.host && room.host->socket != kInvalidSocket &&
						FD_ISSET(ToSocket(room.host->socket), &readSet))
					{
						ReadEndpoint(room, *room.host);
					}

					it = mRooms.find(ticket);
					if (it == mRooms.end()) continue;

					Room& room2 = *it->second;

					if (room2.client && room2.client->socket != kInvalidSocket &&
						FD_ISSET(ToSocket(room2.client->socket), &readSet))
					{
						ReadEndpoint(room2, *room2.client);
					}
				}
			}

			// Flush the pending output of every room.
			for (auto& pair : mRooms)
			{
				Room& room = *pair.second;
				if (room.host && room.host->socket != kInvalidSocket) FlushEndpoint(*room.host);
				if (room.client && room.client->socket != kInvalidSocket) FlushEndpoint(*room.client);
			}

			CleanupRooms();

			// Console commands.
			std::vector<std::string> commands;
			{
				std::lock_guard<std::mutex> lock(mCommandMutex);
				commands.swap(mCommands);
			}

			for (const std::string& command : commands)
			{
				if (command == "stop" || command == "quit" || command == "exit")
				{
					MP_LOG_INFO("Stop requested from the console");
					RequestStop();
				}
				else if (command == "list" || command == "rooms")
				{
					std::printf("Rooms (%zu):\n", mRooms.size());
					for (const auto& pair : mRooms)
					{
						const Room& room = *pair.second;
						std::printf("  %s  host=%s  client=%s  %s\n",
							room.ticket.c_str(),
							room.host ? room.host->address.c_str() : "-",
							room.client ? room.client->address.c_str() : "-",
							room.connected ? "relaying" : "waiting");
					}
				}
				else if (command == "status")
				{
					size_t active = 0;
					for (const auto& pair : mRooms)
					{
						if (pair.second->connected) ++active;
					}
					std::printf("Relay: %zu rooms (%zu active)\n", mRooms.size(), active);
				}
				else if (command == "help")
				{
					std::printf(
						"Commands:\n"
						"  list    - show the rooms\n"
						"  status  - room counts\n"
						"  stop    - shut the relay down\n");
				}
				else if (!command.empty())
				{
					std::printf("Unknown command '%s'\n", command.c_str());
				}
			}
		}

		// Close everything.
		for (auto& endpoint : mPending)
		{
			if (endpoint != nullptr) CloseEndpoint(*endpoint);
		}
		mPending.clear();

		for (auto& pair : mRooms)
		{
			Room& room = *pair.second;
			if (room.host) CloseEndpoint(*room.host);
			if (room.client) CloseEndpoint(*room.client);
		}
		mRooms.clear();

		if (mListenSocket != kInvalidSocket)
		{
			closesocket(ToSocket(mListenSocket));
			mListenSocket = kInvalidSocket;
		}

		mRunning = false;
		MP_LOG_INFO("Relay server stopped");
	}

	void Relay::AcceptConnections()
	{
		while (true)
		{
			sockaddr_in address;
			int addressLength = sizeof(address);
			SOCKET clientSocket = accept(ToSocket(mListenSocket), reinterpret_cast<sockaddr*>(&address), &addressLength);

			if (clientSocket == INVALID_SOCKET)
			{
				const int error = WSAGetLastError();
				if (error != WSAEWOULDBLOCK)
				{
					MP_LOG_WARN("accept() failed (error %d)", error);
				}
				break;
			}

			// Accepted sockets inherit the non-blocking mode of the listener on
			// Windows, which is exactly what the relay wants.
			u_long nonBlocking = 1;
			ioctlsocket(clientSocket, FIONBIO, &nonBlocking);

			BOOL noDelay = TRUE;
			setsockopt(clientSocket, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&noDelay), sizeof(noDelay));

			char ip[64] = { 0 };
			inet_ntop(AF_INET, &address.sin_addr, ip, sizeof(ip));

			// The connection waits in a list until the handshake arrives; a room is
			// created only when it is valid.
			auto endpoint = std::make_unique<Endpoint>();
			endpoint->socket = static_cast<uintptr_t>(clientSocket);
			endpoint->address = ip;

			mPending.push_back(std::move(endpoint));
		}
	}

	void Relay::ReadPendingHandshake(std::unique_ptr<Endpoint>& endpoint)
	{
		char buffer[256];
		const int received = recv(ToSocket(endpoint->socket), buffer, sizeof(buffer), 0);

		if (received <= 0)
		{
			const int error = WSAGetLastError();
			if (received == SOCKET_ERROR && error == WSAEWOULDBLOCK) return;

			CloseEndpoint(*endpoint);
			endpoint.reset();
			return;
		}

		endpoint->inBuffer.insert(endpoint->inBuffer.end(), buffer, buffer + received);

		if (endpoint->inBuffer.size() < kRelayHandshakeSize)
		{
			// Wait for the rest: the handshake can be split across packets.
			return;
		}

		// The handshake is complete: hand the endpoint over to the room logic.
		std::unique_ptr<Endpoint> complete = std::move(endpoint);
		HandleHandshake(std::move(complete));
	}

	void Relay::HandleHandshake(std::unique_ptr<Endpoint> endpoint)
	{
		uint16_t magic = 0;
		std::memcpy(&magic, endpoint->inBuffer.data(), sizeof(magic));

		if (magic != kRelayMagic)
		{
			MP_LOG_DEBUG("Bad relay magic from %s", endpoint->address.c_str());
			CloseEndpoint(*endpoint);
			return;
		}

		const uint8_t role = endpoint->inBuffer[2];

		char ticket[kRelayTicketLength + 1] = { 0 };
		std::memcpy(ticket, endpoint->inBuffer.data() + 4, kRelayTicketLength);

		if (!IsValidTicket(ticket, kRelayTicketLength))
		{
			MP_LOG_DEBUG("Invalid relay ticket from %s", endpoint->address.c_str());
			WriteByte(*endpoint, static_cast<uint8_t>(RelayResult::NoRoom));
			FlushEndpoint(*endpoint);
			CloseEndpoint(*endpoint);
			return;
		}

		const std::string ticketString(ticket, kRelayTicketLength);

		// Anything after the handshake is the start of the game stream.
		endpoint->inBuffer.erase(endpoint->inBuffer.begin(),
			endpoint->inBuffer.begin() + kRelayHandshakeSize);
		endpoint->handshakeDone = true;

		if (role == static_cast<uint8_t>(RelayRole::Host))
		{
			if (mRooms.size() >= mConfig.maxRooms)
			{
				WriteByte(*endpoint, static_cast<uint8_t>(RelayResult::Full));
				FlushEndpoint(*endpoint);
				CloseEndpoint(*endpoint);
				return;
			}

			// Replace a stale room with the same ticket (the host restarted).
			auto existing = mRooms.find(ticketString);
			if (existing != mRooms.end())
			{
				Room& old = *existing->second;
				if (old.host) CloseEndpoint(*old.host);
				if (old.client) CloseEndpoint(*old.client);
				mRooms.erase(existing);
			}

			auto room = std::make_unique<Room>();
			room->ticket = ticketString;
			room->createdAt = NowMs();
			room->host = std::move(endpoint);

			Room* roomPtr = room.get();
			mRooms[ticketString] = std::move(room);

			WriteByte(*roomPtr->host, static_cast<uint8_t>(RelayResult::Ok));
			FlushEndpoint(*roomPtr->host);

			MP_LOG_INFO("Room %s created by host %s", ticketString.c_str(), roomPtr->host->address.c_str());
			return;
		}

		if (role == static_cast<uint8_t>(RelayRole::Client))
		{
			auto it = mRooms.find(ticketString);
			if (it == mRooms.end())
			{
				WriteByte(*endpoint, static_cast<uint8_t>(RelayResult::NoRoom));
				FlushEndpoint(*endpoint);
				CloseEndpoint(*endpoint);
				return;
			}

			Room& room = *it->second;

			if (room.client != nullptr && room.client->socket != kInvalidSocket)
			{
				WriteByte(*endpoint, static_cast<uint8_t>(RelayResult::Full));
				FlushEndpoint(*endpoint);
				CloseEndpoint(*endpoint);
				return;
			}

			room.client = std::move(endpoint);
			room.connected = true;

			WriteByte(*room.client, static_cast<uint8_t>(RelayResult::Ok));
			FlushEndpoint(*room.client);

			MP_LOG_INFO("Room %s: client %s connected to host %s",
				ticketString.c_str(), room.client->address.c_str(), room.host->address.c_str());
			return;
		}

		MP_LOG_DEBUG("Unknown relay role %u from %s", role, endpoint->address.c_str());
		CloseEndpoint(*endpoint);
	}

	void Relay::ReadEndpoint(Room& room, Endpoint& endpoint)
	{
		char buffer[kRelayBufferSize];
		const int received = recv(ToSocket(endpoint.socket), buffer, sizeof(buffer), 0);

		if (received == 0)
		{
			MP_LOG_INFO("Room %s: %s disconnected", room.ticket.c_str(), endpoint.address.c_str());

			// Tell the other side and close the whole room: a half room is useless.
			if (&endpoint == room.host.get() && room.client)
			{
				CloseEndpoint(*room.client);
			}
			else if (&endpoint == room.client.get() && room.host)
			{
				CloseEndpoint(*room.host);
			}

			CloseEndpoint(endpoint);
			return;
		}

		if (received == SOCKET_ERROR)
		{
			const int error = WSAGetLastError();
			if (error != WSAEWOULDBLOCK)
			{
				MP_LOG_INFO("Room %s: recv failed for %s (error %d)",
					room.ticket.c_str(), endpoint.address.c_str(), error);
				CloseEndpoint(endpoint);
			}
			return;
		}

		if (endpoint.inBuffer.empty())
		{
			endpoint.inBuffer.assign(buffer, buffer + received);
		}
		else
		{
			endpoint.inBuffer.insert(endpoint.inBuffer.end(), buffer, buffer + received);
		}

		// Forward to the other side.
		Endpoint* other = (&endpoint == room.host.get()) ? room.client.get() : room.host.get();
		if (other == nullptr || other->socket == kInvalidSocket) return;

		other->outBuffer.insert(other->outBuffer.end(), endpoint.inBuffer.begin(), endpoint.inBuffer.end());
		endpoint.inBuffer.clear();
	}

	void Relay::FlushEndpoint(Endpoint& endpoint)
	{
		if (endpoint.socket == kInvalidSocket) return;

		while (!endpoint.outBuffer.empty())
		{
			const int sent = send(ToSocket(endpoint.socket),
				reinterpret_cast<const char*>(endpoint.outBuffer.data()),
				static_cast<int>(endpoint.outBuffer.size()), 0);

			if (sent == SOCKET_ERROR)
			{
				const int error = WSAGetLastError();
				if (error == WSAEWOULDBLOCK) return;

				CloseEndpoint(endpoint);
				return;
			}

			endpoint.outBuffer.erase(endpoint.outBuffer.begin(), endpoint.outBuffer.begin() + sent);
		}
	}

	void Relay::CloseEndpoint(Endpoint& endpoint)
	{
		if (endpoint.socket != kInvalidSocket)
		{
			closesocket(ToSocket(endpoint.socket));
			endpoint.socket = kInvalidSocket;
		}
		endpoint.inBuffer.clear();
		endpoint.outBuffer.clear();
		endpoint.closing = true;
	}

	void Relay::WriteByte(Endpoint& endpoint, uint8_t value)
	{
		if (endpoint.socket == kInvalidSocket) return;
		endpoint.outBuffer.push_back(value);
	}

	void Relay::CleanupRooms()
	{
		const uint64_t now = NowMs();
		const uint64_t limit = static_cast<uint64_t>(mConfig.roomTimeoutSeconds) * 1000;

		for (auto it = mRooms.begin(); it != mRooms.end();)
		{
			Room& room = *it->second;

			const bool hostDead = room.host == nullptr || room.host->socket == kInvalidSocket;
			const bool clientDead = room.client == nullptr || room.client->socket == kInvalidSocket;

			bool remove = false;

			if (room.connected)
			{
				// A connected room ends as soon as one side is gone.
				remove = hostDead || clientDead;
			}
			else
			{
				// A waiting room ends when the host leaves or the timeout expires.
				remove = hostDead || (now - room.createdAt > limit);
			}

			if (remove)
			{
				if (room.host) CloseEndpoint(*room.host);
				if (room.client) CloseEndpoint(*room.client);

				MP_LOG_INFO("Room %s closed", room.ticket.c_str());
				it = mRooms.erase(it);
			}
			else
			{
				++it;
			}
		}
	}
}
