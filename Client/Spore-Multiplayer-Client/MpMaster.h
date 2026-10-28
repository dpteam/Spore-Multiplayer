/****************************************************************************
* Spore Multiplayer
*
* Querying the master server for the public session list.
*
* The query is a plain HTTP GET with a JSON response, so the same endpoint can
* be used by the mod, by a launcher or by a web page. It runs on the game
* thread but with short socket timeouts, and it is only triggered by a command,
* so it never blocks the game loop for long.
****************************************************************************/
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "../../Shared/MpMasterProtocol.h"

namespace SporeMP
{
	/// A public session reported by the master server.
	struct PublicSession
	{
		std::string name;
		std::string host;
		uint16_t port = 0;
		int players = 0;
		int maxPlayers = 0;
		int version = 0;

		/// Relay of the session, empty when it must be reached directly.
		std::string relayHost;
		uint16_t relayPort = 0;
		std::string ticket;
	};

	class MasterQuery
	{
	public:
		/// Fetches the session list from a master URL such as
		/// "http://play.example.com:17401".
		/// @param error Filled with a message when the query fails.
		static bool Fetch(const std::string& masterUrl, std::vector<PublicSession>& dst, std::string& error);

		/// Parses the JSON returned by GET /api/v1/servers.
		static bool ParseServerList(const std::string& json, std::vector<PublicSession>& dst);

	private:
		/// Performs a blocking HTTP GET. Used only by Fetch().
		static bool HttpGet(const std::string& host, uint16_t port, const std::string& path,
			std::string& response, std::string& error);
	};
}
