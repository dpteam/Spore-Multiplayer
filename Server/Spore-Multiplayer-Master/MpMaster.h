/****************************************************************************
* Spore Multiplayer - Master server
*
* Keeps the list of public sessions. Servers register themselves, send
* heartbeats, and clients ask for the list.
*
* The API is plain HTTP + JSON (see Shared/MpMasterProtocol.h), so the list can
* be consumed by the mod, by a launcher or by a web page.
*
* The public address of a session is taken from the socket of the registration,
* so a server behind NAT only has to reach the master; no configuration on the
* server side is needed beyond forwarding its game port.
****************************************************************************/
#pragma once

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

#include "../../Shared/MpMasterProtocol.h"

namespace SporeMP
{
	struct MasterConfig
	{
		uint16_t port = kDefaultMasterPort;
		std::string bindAddress = "0.0.0.0";
		int heartbeatSeconds = kMasterHeartbeatSeconds;
		int timeoutSeconds = kMasterTimeoutSeconds;
		size_t maxServers = kMasterMaxServers;
		std::string motd;
	};

	class Master
	{
	public:
		explicit Master(const MasterConfig& config);
		~Master();

		Master(const Master&) = delete;
		Master& operator=(const Master&) = delete;

		bool Start();
		void Run();
		void RequestStop();

		/// Thread safe: the command is executed by the Run() loop.
		void QueueCommand(const std::string& command);

	private:
		struct Session
		{
			std::string token;
			std::string name;
			std::string host;
			uint16_t port = 0;
			int players = 0;
			int maxPlayers = 0;
			int version = 0;
			uint64_t lastSeen = 0;

			/// Relay used by the session, if any: clients behind NAT can join
			/// through it instead of connecting directly.
			std::string relayHost;
			uint16_t relayPort = 0;
			std::string relayTicket;
		};

		struct Request
		{
			std::string method;
			std::string path;
			std::string body;
			std::string clientAddress;
			bool forwarded = false;
		};

		void AcceptConnections();
		void HandleConnection(uintptr_t socket, const std::string& address);

		/// Parses the HTTP request and writes the response.
		void ServeRequest(uintptr_t socket, const std::string& address);

		void HandleRegister(uintptr_t socket, const Request& request);
		void HandleHeartbeat(uintptr_t socket, const Request& request);
		void HandleUnregister(uintptr_t socket, const Request& request);
		void HandleList(uintptr_t socket);
		void HandleHealth(uintptr_t socket);

		void CheckTimeouts();

		Session* FindByToken(const std::string& token);
		std::string GenerateToken();

		void WriteResponse(uintptr_t socket, int status, const char* statusText,
			const std::string& body, const char* contentType = "application/json");

		MasterConfig mConfig;
		uintptr_t mListenSocket = static_cast<uintptr_t>(~0);
		std::vector<Session> mSessions;
		uint64_t mStartTime = 0;
		uint32_t mTokenCounter = 0;

		bool mRunning = false;
		std::atomic<bool> mStopRequested{ false };

		std::mutex mCommandMutex;
		std::vector<std::string> mCommands;
	};
}
