/****************************************************************************
* Spore Multiplayer - Dedicated server
*
* Hosting through the relay.
*
* When the server is started with `--relay host:port`, it opens a connection to
* the relay as the "host" side of a room and then bridges that connection to its
* own local TCP port. From the game's point of view nothing changes: the bridged
* connection becomes an ordinary client.
*
* That is what makes the server reachable without any port forwarding: the
* server only makes an outgoing connection.
****************************************************************************/
#pragma once

#include <atomic>
#include <cstdint>
#include <string>
#include <thread>

namespace SporeMP
{
	/// Generates a random hex ticket of the given length.
	std::string GenerateRelayTicket(size_t length = 16);

	/// Splits "host:port" into its parts. The port defaults to kDefaultRelayPort.
	bool ParseRelayAddress(const std::string& value, std::string& host, uint16_t& port);

	class RelayHost
	{
	public:
		RelayHost();
		~RelayHost();

		/// Connects to the relay and starts bridging. The local port is the game
		/// port, so the bridge connects to 127.0.0.1:<localPort>.
		bool Start(const std::string& relayAddress, const std::string& ticket,
			uint16_t localPort, std::string& error);

		void Stop();

		bool IsRunning() const { return mRunning.load(); }

		const std::string& GetTicket() const { return mTicket; }
		const std::string& GetRelayAddress() const { return mRelayAddress; }

	private:
		void ThreadMain(std::string host, uint16_t port, std::string ticket, uint16_t localPort);

		std::string mRelayAddress;
		std::string mTicket;

		std::atomic<bool> mRunning{ false };
		std::atomic<bool> mStopRequested{ false };
		uintptr_t mRelaySocket = static_cast<uintptr_t>(~0);

		std::thread mThread;
	};
}
