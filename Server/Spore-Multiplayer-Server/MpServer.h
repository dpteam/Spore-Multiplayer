/****************************************************************************
* Spore Multiplayer - Dedicated server
*
* A single threaded, select() based TCP server that relays player states and
* chat messages between the Spore clients connected to it.
*
* The server is authoritative for:
*   - player ids, names and stages
*   - the list of players in the session
*   - the tick at which states are broadcast
*
* It does not simulate anything: Spore has no server side game simulation, so
* each client simulates its own world and only the player avatars are shared.
****************************************************************************/
#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "../../Shared/MpProtocol.h"

namespace SporeMP
{
	struct ServerConfig
	{
		uint16_t port = 7000;
		std::string bindAddress = "0.0.0.0";
		int maxPlayers = kMaxPlayers;
		std::string sessionName = "Spore Co-op";
		int tickRate = 20;
		int timeoutSeconds = 15;
		int handshakeTimeoutSeconds = 10;
		bool allowDuplicateNames = false;
	};

	class Server
	{
	public:
		explicit Server(const ServerConfig& config);
		~Server();

		Server(const Server&) = delete;
		Server& operator=(const Server&) = delete;

		/// Initializes Winsock, binds and starts listening.
		/// @returns false if the server could not be started (the error is logged).
		bool Start();

		/// Blocking loop: accepts clients, processes messages and broadcasts states.
		/// It returns when RequestStop() is called (or when the console receives Ctrl+C).
		void Run();

		/// Asks the loop to finish as soon as possible.
		void RequestStop();

		/// Thread safe: the command is executed by the Run() loop.
		void QueueCommand(const std::string& command);

		const ServerConfig& GetConfig() const { return mConfig; }

	private:
		struct Client
		{
			uintptr_t socket = static_cast<uintptr_t>(~0);
			bool handshakeDone = false;
			/// The client is being disconnected; the socket is closed once all pending
			/// data has been sent.
			bool closing = false;
			uint32_t id = 0;
			std::string address;
			std::vector<uint8_t> inBuffer;
			std::vector<uint8_t> outBuffer;
			size_t inOffset = 0;
			uint64_t connectedAt = 0;
			uint64_t lastRecvAt = 0;
			uint64_t closingSince = 0;
			PlayerInfo info;
			StateEntry state;
			bool hasState = false;
			bool stateChanged = false;
		};

		// --- networking ------------------------------------------------------
		void AcceptNewClients();
		void ReadClient(Client& client);
		void FlushClient(Client& client);
		void CloseClient(Client& client, RejectReason reason, const char* message, bool notify = true);
		void RemoveClosedClients();

		void HandleFrame(Client& client, uint16_t type, const uint8_t* payload, size_t size);
		void HandleHello(Client& client, const uint8_t* payload, size_t size);
		void HandleLocalState(Client& client, const uint8_t* payload, size_t size);
		void HandleChat(Client& client, const uint8_t* payload, size_t size);
		void HandleSetName(Client& client, const uint8_t* payload, size_t size);
		void HandlePing(Client& client, const uint8_t* payload, size_t size);

		// --- session ---------------------------------------------------------
		void ProcessTicks();
		void BroadcastStates();
		void CheckTimeouts();
		void ProcessCommands();

		void Send(Client& client, MsgType type, const void* payload, size_t size);
		void Broadcast(MsgType type, const void* payload, size_t size, uint32_t exceptId = 0);
		void BroadcastChat(uint32_t senderId, const char* text);

		void SendPlayerList(Client& client);
		void SendSystemChat(Client& client, const char* text);

		/// Generates a name that is not used by any other client yet.
		/// `exceptId` is the id of the client that is being named, so it does not collide with itself.
		void MakeUniqueName(const char* requested, char* dst, size_t dstSize, uint32_t exceptId = 0);
		bool IsNameInUse(const char* name, uint32_t exceptId) const;
		bool IsNameValid(const char* name) const;

		uint32_t NextPlayerId();
		size_t ReadyClientCount() const;

		ServerConfig mConfig;
		uintptr_t mListenSocket = static_cast<uintptr_t>(~0);
		std::vector<std::unique_ptr<Client>> mClients;
		uint32_t mNextPlayerId = 1;
		uint64_t mStartTime = 0;
		bool mRunning = false;
		std::atomic<bool> mStopRequested{ false };

		std::mutex mCommandMutex;
		std::vector<std::string> mCommands;
	};
}
