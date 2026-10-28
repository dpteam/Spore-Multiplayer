/****************************************************************************
* Spore Multiplayer
*
* Client configuration, read from `SporeMultiplayer.ini` next to the DLL.
*
* [connection]
* host=127.0.0.1
* port=7000
* name=Darter
* auto_connect=0
*
* [display]
* hud=1
* hud_x=0.02
* hud_y=0.35
* hud_scale=1.0
* chat_lines=6
*
* [sync]
* send_rate=15
* show_avatars=1
* interpolation=1
****************************************************************************/
#pragma once

#include <string>

namespace SporeMP
{
	struct ClientConfig
	{
		std::string host = "127.0.0.1";
		uint16_t port = 7000;
		std::string name;
		bool autoConnect = false;

		/// Session name used when hosting from the game.
		std::string sessionName = "Spore Co-op";

		/// Optional explicit path of Spore-Multiplayer-Server.exe.
		std::string serverExe;

		/// Master server used by mp_internet and by the servers started with mp_host.
		std::string masterUrl = "play.dartpower.xyz:17401";

		/// Relay used by mp_host, so players can join without port forwarding.
		/// Empty disables it.
		std::string relayUrl = "play.dartpower.xyz:17402";

		bool hud = false;
		float hudX = 0.02f;
		float hudY = 0.35f;
		float hudScale = 1.0f;
		int chatLines = 6;

		/// Open the separate console window with the log and the commands.
		bool console = true;

		/// Log verbosity: "debug", "info", "warning" or "error".
		/// Set "debug" when collecting logs for a bug report.
		std::string logLevel = "info";

		int sendRate = 15;
		bool showAvatars = true;
		bool interpolation = true;
		bool nameTags = true;
	};

	/// Loads the configuration from the given file. Missing values keep their defaults.
	/// @returns false if the file could not be opened.
	bool LoadClientConfig(const std::string& path, ClientConfig& config);

	/// Writes the configuration, creating the file with the defaults if it does not exist.
	bool SaveClientConfig(const std::string& path, const ClientConfig& config);

	/// Returns the directory where `SporeMultiplayer.ini` and `SporeMultiplayer.log`
	/// are stored, with a trailing backslash.
	///
	/// The preferred directory (the folder of the mod DLL) is used when it is
	/// writable; otherwise the function falls back to `%APPDATA%\SporeMultiplayer\`,
	/// which is always writable for the current user.
	std::string ResolveDataDirectory(const std::string& preferred);
}
