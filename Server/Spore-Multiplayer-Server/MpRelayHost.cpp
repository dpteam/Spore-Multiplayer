/****************************************************************************
* Spore Multiplayer - Dedicated server
****************************************************************************/
#include "MpRelayHost.h"
#include "MpLog.h"

#include <winsock2.h>
#include <ws2tcpip.h>

#pragma comment(lib, "ws2_32.lib")

#include <chrono>
#include <cstring>
#include <random>
#include <vector>

#include "../../Shared/MpRelayProtocol.h"

namespace SporeMP
{
	namespace
	{
		const uintptr_t kInvalidSocket = static_cast<uintptr_t>(INVALID_SOCKET);

		SOCKET ToSocket(uintptr_t value)
		{
			return static_cast<SOCKET>(value);
		}

		bool EnsureWinsock()
		{
			static bool done = false;
			if (done) return true;

			WSADATA data;
			done = WSAStartup(MAKEWORD(2, 2), &data) == 0;
			return done;
		}

		/// Connects a blocking socket to the given address.
		SOCKET ConnectTo(const std::string& host, uint16_t port)
		{
			addrinfo hints;
			std::memset(&hints, 0, sizeof(hints));
			hints.ai_family = AF_INET;
			hints.ai_socktype = SOCK_STREAM;

			addrinfo* result = nullptr;
			if (getaddrinfo(host.c_str(), nullptr, &hints, &result) != 0 || result == nullptr)
			{
				return INVALID_SOCKET;
			}

			sockaddr_in address;
			std::memset(&address, 0, sizeof(address));
			address.sin_family = AF_INET;
			address.sin_port = htons(port);
			address.sin_addr = reinterpret_cast<sockaddr_in*>(result->ai_addr)->sin_addr;
			freeaddrinfo(result);

			SOCKET sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
			if (sock == INVALID_SOCKET) return INVALID_SOCKET;

			if (connect(sock, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == SOCKET_ERROR)
			{
				closesocket(sock);
				return INVALID_SOCKET;
			}

			BOOL noDelay = TRUE;
			setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&noDelay), sizeof(noDelay));

			return sock;
		}
	}

	std::string GenerateRelayTicket(size_t length)
	{
		static const char* digits = "0123456789abcdef";

		std::random_device device;
		std::mt19937 generator(device() ^ static_cast<unsigned>(GetTickCount64()));

		std::string ticket;
		ticket.reserve(length);
		for (size_t i = 0; i < length; ++i)
		{
			ticket += digits[generator() % 16];
		}

		return ticket;
	}

	bool ParseRelayAddress(const std::string& value, std::string& host, uint16_t& port)
	{
		std::string rest = value;

		// Tolerate a "tcp://" prefix.
		const size_t scheme = rest.find("://");
		if (scheme != std::string::npos) rest = rest.substr(scheme + 3);

		const size_t colon = rest.find(':');
		if (colon == std::string::npos)
		{
			host = rest;
			port = kDefaultRelayPort;
		}
		else
		{
			host = rest.substr(0, colon);
			port = static_cast<uint16_t>(std::strtoul(rest.c_str() + colon + 1, nullptr, 10));
		}

		return !host.empty() && port != 0;
	}

	RelayHost::RelayHost()
	{
	}

	RelayHost::~RelayHost()
	{
		Stop();
	}

	bool RelayHost::Start(const std::string& relayAddress, const std::string& ticket,
		uint16_t localPort, std::string& error)
	{
		if (mRunning.load())
		{
			error = "The relay is already running";
			return false;
		}

		std::string host;
		uint16_t port = 0;
		if (!ParseRelayAddress(relayAddress, host, port))
		{
			error = "Invalid relay address: " + relayAddress;
			return false;
		}

		mRelayAddress = relayAddress;
		mTicket = ticket.empty() ? GenerateRelayTicket() : ticket;
		mStopRequested.store(false);

		mThread = std::thread(&RelayHost::ThreadMain, this, host, port, mTicket, localPort);
		return true;
	}

	void RelayHost::Stop()
	{
		mStopRequested.store(true);

		if (mRelaySocket != kInvalidSocket)
		{
			shutdown(ToSocket(mRelaySocket), SD_BOTH);
		}

		if (mThread.joinable())
		{
			mThread.join();
		}

		mRunning.store(false);
	}

	void RelayHost::ThreadMain(std::string host, uint16_t port, std::string ticket, uint16_t localPort)
	{
		if (!EnsureWinsock()) return;

		// A relay room carries one client at a time: the host reconnects after each
		// client leaves, so a session can be joined by several players in turn.
		while (!mStopRequested.load())
		{
			SOCKET relaySocket = ConnectTo(host, port);
			if (relaySocket == INVALID_SOCKET)
			{
				MP_LOG_WARN("Could not connect to the relay at %s:%u, retrying in 5 s", host.c_str(), port);

				for (int i = 0; i < 50 && !mStopRequested.load(); ++i)
				{
					std::this_thread::sleep_for(std::chrono::milliseconds(100));
				}
				continue;
			}

			// Handshake: magic, role = host, reserved, ticket.
			uint8_t handshake[kRelayHandshakeSize];
			std::memset(handshake, 0, sizeof(handshake));

			uint16_t magic = kRelayMagic;
			std::memcpy(handshake, &magic, sizeof(magic));
			handshake[2] = static_cast<uint8_t>(RelayRole::Host);
			std::memcpy(handshake + 4, ticket.data(), kRelayTicketLength);

			if (send(relaySocket, reinterpret_cast<const char*>(handshake), sizeof(handshake), 0) != sizeof(handshake))
			{
				MP_LOG_WARN("Could not send the relay handshake");
				closesocket(relaySocket);
				continue;
			}

			uint8_t result = 0xFF;
			if (recv(relaySocket, reinterpret_cast<char*>(&result), 1, 0) != 1)
			{
				MP_LOG_WARN("The relay did not answer the handshake");
				closesocket(relaySocket);
				continue;
			}

			if (result != static_cast<uint8_t>(RelayResult::Ok))
			{
				MP_LOG_ERROR("The relay refused the room (code %u)", result);
				closesocket(relaySocket);
				break;
			}

			mRelaySocket = static_cast<uintptr_t>(relaySocket);
			mRunning.store(true);

			MP_LOG_INFO("Relay room ready: %s:%u, ticket %s", host.c_str(), port, ticket.c_str());
			MP_LOG_INFO("Players can join with: mp_relay %s %u %s", host.c_str(), port, ticket.c_str());

			// Serve clients of this room until the relay closes it.
			while (!mStopRequested.load())
			{
				fd_set readSet;
				FD_ZERO(&readSet);
				FD_SET(relaySocket, &readSet);

				timeval timeout = { 0, 20000 };
				const int ready = select(0, &readSet, nullptr, nullptr, &timeout);

				if (ready <= 0) continue;

				char buffer[kRelayBufferSize];
				const int received = recv(relaySocket, buffer, sizeof(buffer), 0);

				if (received <= 0)
				{
					break;
				}

				// Connect to the local game port and forward. The bytes the relay
				// sends are the beginning of the game protocol.
				SOCKET localSocket = ConnectTo("127.0.0.1", localPort);
				if (localSocket == INVALID_SOCKET)
				{
					MP_LOG_WARN("Could not connect to the local game port %u", localPort);
					continue;
				}

				MP_LOG_INFO("A player joined through the relay");

				send(localSocket, buffer, received, 0);

				// Pump until either side closes.
				bool done = false;
				while (!done && !mStopRequested.load())
				{
					fd_set both;
					FD_ZERO(&both);
					FD_SET(relaySocket, &both);
					FD_SET(localSocket, &both);

					timeval pumpTimeout = { 0, 20000 };
					if (select(0, &both, nullptr, nullptr, &pumpTimeout) <= 0) continue;

					if (FD_ISSET(localSocket, &both))
					{
						const int count = recv(localSocket, buffer, sizeof(buffer), 0);
						if (count <= 0) { done = true; break; }
						send(relaySocket, buffer, count, 0);
					}

					if (FD_ISSET(relaySocket, &both))
					{
						const int count = recv(relaySocket, buffer, sizeof(buffer), 0);
						if (count <= 0) { done = true; break; }
						send(localSocket, buffer, count, 0);
					}
				}

				closesocket(localSocket);
				MP_LOG_INFO("The relayed player disconnected");

				// The relay closes the room when one side leaves, so the outer loop
				// reconnects and the next player can join.
				break;
			}

			closesocket(relaySocket);
			mRelaySocket = kInvalidSocket;
		}

		mRunning.store(false);
		MP_LOG_INFO("Relay bridge stopped");
	}
}
