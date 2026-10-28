/****************************************************************************
* Spore Multiplayer - Relay server
*
* Forwards the game stream between a host and a client, so neither side needs an
* open port. This solves NAT without port forwarding, CGNAT, strict firewalls
* and broken NAT hairpin.
*
* The relay never parses the game protocol: after the handshake it copies bytes
* in both directions. That keeps it small and makes it work with any future
* protocol version.
****************************************************************************/
#pragma once

#include <atomic>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "../../Shared/MpRelayProtocol.h"

namespace SporeMP
{
	struct RelayConfig
	{
		uint16_t port = kDefaultRelayPort;
		std::string bindAddress = "0.0.0.0";
		int roomTimeoutSeconds = kRelayRoomTimeoutSeconds;
		size_t maxRooms = 512;
		/// Optional shared secret: when set, hosts must pass it as part of the
		/// ticket prefix, so a public relay cannot be abused by strangers.
		std::string secret;
	};

	class Relay
	{
	public:
		explicit Relay(const RelayConfig& config);
		~Relay();

		Relay(const Relay&) = delete;
		Relay& operator=(const Relay&) = delete;

		bool Start();
		void Run();
		void RequestStop();

		void QueueCommand(const std::string& command);

	private:
		struct Endpoint
		{
			uintptr_t socket = static_cast<uintptr_t>(~0);
			std::string address;
			std::vector<uint8_t> inBuffer;
			std::vector<uint8_t> outBuffer;
			bool handshakeDone = false;
			bool closing = false;
		};

		struct Room
		{
			std::string ticket;
			uint64_t createdAt = 0;
			std::unique_ptr<Endpoint> host;
			std::unique_ptr<Endpoint> client;
			/// Set once both sides are connected and the room is relaying.
			bool connected = false;
		};

		void AcceptConnections();
		void ReadEndpoint(Room& room, Endpoint& endpoint);
		void FlushEndpoint(Endpoint& endpoint);
		void CloseEndpoint(Endpoint& endpoint);

		/// Reads the handshake of a pending connection; once complete, the endpoint
		/// is moved into a room.
		void ReadPendingHandshake(std::unique_ptr<Endpoint>& endpoint);

		/// Validates a complete handshake and moves the endpoint into a room.
		void HandleHandshake(std::unique_ptr<Endpoint> endpoint);

		void RelayTraffic(Room& room);
		void CleanupRooms();

		void WriteByte(Endpoint& endpoint, uint8_t value);

		RelayConfig mConfig;
		uintptr_t mListenSocket = static_cast<uintptr_t>(~0);
		std::map<std::string, std::unique_ptr<Room>> mRooms;

		/// Connections that have not finished the handshake yet.
		std::vector<std::unique_ptr<Endpoint>> mPending;

		bool mRunning = false;
		std::atomic<bool> mStopRequested{ false };

		std::mutex mCommandMutex;
		std::vector<std::string> mCommands;
	};
}
