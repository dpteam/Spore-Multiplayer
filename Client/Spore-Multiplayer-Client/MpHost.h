/****************************************************************************
* Spore Multiplayer
*
* Hosting a game from inside Spore.
*
* Spore cannot host a session by itself, so the mod starts the dedicated server
* executable in the background and then connects to it. That gives the usual
* "Host game / Join game" flow: the host runs `mp_host`, and everybody else on
* the local network sees the session in `mp_servers`.
****************************************************************************/
#pragma once

#include <cstdint>
#include <string>

namespace SporeMP
{
	class Host
	{
	public:
		static Host& Get();

		/// Starts the dedicated server as a child process.
		/// @param port TCP port of the session.
		/// @param sessionName Name shown to the other players.
		/// @param masterUrl Master server to register on, empty for LAN only.
		/// @param error Filled with a message when something fails.
		/// @returns true if the process was started.
		bool StartServer(uint16_t port, const std::string& sessionName,
			const std::string& masterUrl, std::string& error);

		/// Stops the server that was started by StartServer(), if any.
		void StopServer();

		/// True while the server started by the mod is running.
		bool IsHosting() const;

		/// Remembers the folder of the mod DLL, used to look for the server.
		void Initialize(const std::string& directory) { mDirectory = directory; }

		/// Path of the server executable, empty if it was not found.
		const std::string& GetServerPath() const { return mServerPath; }

		/// Looks for Spore-Multiplayer-Server.exe near the mod DLL.
		bool LocateServer();

		/// Sets an explicit path (used by the configuration file).
		void SetServerPath(const std::string& path) { mServerPath = path; }

	private:
		Host();

		std::string mServerPath;
		std::string mDirectory;
		uintptr_t mProcess = 0;
	};
}
