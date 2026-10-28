/****************************************************************************
* Spore Multiplayer - Dedicated server
*
* Registration on the master server.
*
* When the server is started with `--master <url>`, a background thread
* registers the session and then sends heartbeats, so the session shows up in
* the public server list. The thread uses blocking sockets on purpose: it never
* touches the game loop, so a slow or dead master cannot stall the server.
****************************************************************************/
#pragma once

#include <atomic>
#include <cstdint>
#include <string>
#include <thread>

namespace SporeMP
{
	/// Splits "http://host:port/path" into its parts.
	bool ParseMasterUrl(const std::string& url, std::string& host, uint16_t& port, std::string& path);

	/// Performs a blocking HTTP POST and returns the response body.
	bool HttpPost(const std::string& host, uint16_t port, const std::string& path,
		const std::string& body, std::string& response);

	class MasterRegistration
	{
	public:
		MasterRegistration();
		~MasterRegistration();

		/// Starts the registration thread. `playerCount` is read every heartbeat.
		/// @returns false if the URL is invalid.
		bool Start(const std::string& masterUrl, const std::string& sessionName,
			uint16_t gamePort, int maxPlayers, std::atomic<int>* playerCount,
			const std::string& relayAddress = std::string(),
			const std::string& relayTicket = std::string());

		/// Unregisters and stops the thread.
		void Stop();

		bool IsRegistered() const { return mRegistered.load(); }

	private:
		void ThreadMain(std::string host, uint16_t port, std::string path,
			std::string sessionName, uint16_t gamePort, int maxPlayers,
			std::atomic<int>* playerCount, std::string relayAddress, std::string relayTicket);

		std::string mHost;
		uint16_t mPort = 0;
		std::string mPath;
		std::string mToken;
		std::string mRelayHost;
		uint16_t mRelayPort = 0;
		std::string mRelayTicket;

		std::atomic<bool> mRegistered{ false };
		std::atomic<bool> mStopRequested{ false };
		std::thread mThread;
	};
}
