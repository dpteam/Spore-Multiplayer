/****************************************************************************
* Spore Multiplayer - Dedicated server
*
* Entry point: parses the configuration (server.ini + command line), starts the
* server and forwards console input to it as commands.
*
* Usage:
*     Spore-Multiplayer-Server.exe [--port 7000] [--name "Spore Co-op"]
*                                  [--max-players 8] [--tick-rate 20]
*                                  [--timeout 15] [--bind 0.0.0.0]
*                                  [--log-level info] [--config server.ini]
****************************************************************************/
#include "MpServer.h"
#include "MpLog.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>

#include <windows.h>

namespace
{
	SporeMP::Server* g_server = nullptr;

	BOOL WINAPI ConsoleHandler(DWORD signal)
	{
		switch (signal)
		{
		case CTRL_C_EVENT:
		case CTRL_BREAK_EVENT:
		case CTRL_CLOSE_EVENT:
		case CTRL_SHUTDOWN_EVENT:
			if (g_server != nullptr)
			{
				g_server->RequestStop();
			}
			return TRUE;
		default:
			return FALSE;
		}
	}

	std::string Trim(const std::string& value)
	{
		size_t begin = 0;
		size_t end = value.size();

		while (begin < end && (value[begin] == ' ' || value[begin] == '\t' || value[begin] == '\r' || value[begin] == '\n'))
		{
			++begin;
		}
		while (end > begin && (value[end - 1] == ' ' || value[end - 1] == '\t' || value[end - 1] == '\r' || value[end - 1] == '\n'))
		{
			--end;
		}
		return value.substr(begin, end - begin);
	}

	std::string ToLower(const std::string& value)
	{
		std::string result = value;
		for (char& c : result)
		{
			if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
		}
		return result;
	}

	bool ParseInt(const std::string& text, int& dst)
	{
		if (text.empty()) return false;

		char* end = nullptr;
		const long value = std::strtol(text.c_str(), &end, 10);
		if (end == nullptr || *end != '\0') return false;

		dst = static_cast<int>(value);
		return true;
	}

	std::string GetExecutableDirectory()
	{
		char path[MAX_PATH] = { 0 };
		const DWORD length = GetModuleFileNameA(nullptr, path, MAX_PATH);
		if (length == 0) return std::string();

		std::string fullPath(path, length);
		const size_t separator = fullPath.find_last_of("\\/");
		if (separator == std::string::npos) return std::string();
		return fullPath.substr(0, separator + 1);
	}

	void PrintUsage()
	{
		std::printf(
			"%s - dedicated server v%u\n"
			"\n"
			"Usage: Spore-Multiplayer-Server.exe [options]\n"
			"\n"
			"Options:\n"
			"  --port <number>       TCP port to listen on (default: 7000)\n"
			"  --bind <address>      Address to bind (default: 0.0.0.0)\n"
			"  --name <text>         Session name shown to the clients (default: Spore Co-op)\n"
			"  --max-players <n>     Maximum number of players, 1-%d (default: %d)\n"
			"  --tick-rate <hz>      State broadcast rate, 1-60 (default: 20)\n"
			"  --timeout <seconds>   Drop a player after this time without data (default: 15)\n"
			"  --log-level <level>   debug, info, warning or error (default: info)\n"
			"  --config <file>       Read the options from an ini file\n"
			"  --allow-duplicate-names\n"
			"  --help, --version\n",
			SporeMP::kProtocolName, SporeMP::kProtocolVersion, SporeMP::kMaxPlayers, SporeMP::kMaxPlayers);
	}

	bool LoadIniFile(const std::string& path, SporeMP::ServerConfig& config)
	{
		std::ifstream file(path.c_str());
		if (!file.is_open()) return false;

		std::string line;
		while (std::getline(file, line))
		{
			line = Trim(line);
			if (line.empty() || line[0] == '#' || line[0] == ';' || line[0] == '[') continue;

			const size_t separator = line.find('=');
			if (separator == std::string::npos) continue;

			const std::string key = ToLower(Trim(line.substr(0, separator)));
			const std::string value = Trim(line.substr(separator + 1));
			int number = 0;

			if (key == "port" && ParseInt(value, number)) config.port = static_cast<uint16_t>(number);
			else if (key == "bind" || key == "bind_address") config.bindAddress = value;
			else if (key == "session_name" || key == "name") config.sessionName = value;
			else if (key == "max_players" && ParseInt(value, number)) config.maxPlayers = number;
			else if (key == "tick_rate" && ParseInt(value, number)) config.tickRate = number;
			else if (key == "timeout" && ParseInt(value, number)) config.timeoutSeconds = number;
			else if (key == "handshake_timeout" && ParseInt(value, number)) config.handshakeTimeoutSeconds = number;
			else if (key == "allow_duplicate_names") config.allowDuplicateNames = (value == "1" || ToLower(value) == "true");
			else if (key == "log_level")
			{
				SporeMP::LogLevel level;
				if (SporeMP::ParseLogLevel(value.c_str(), level)) SporeMP::SetLogLevel(level);
			}
		}

		return true;
	}
}

int main(int argc, char** argv)
{
	SporeMP::ServerConfig config;
	SporeMP::SetLogLevel(SporeMP::LogLevel::Info);

	// 1) The ini file is read first, so the command line always wins.
	std::string configPath = GetExecutableDirectory() + "server.ini";
	bool configSpecified = false;

	for (int i = 1; i < argc; ++i)
	{
		if (std::strcmp(argv[i], "--config") == 0 && i + 1 < argc)
		{
			configPath = argv[++i];
			configSpecified = true;
		}
	}

	if (LoadIniFile(configPath, config))
	{
		MP_LOG_INFO("Loaded configuration from %s", configPath.c_str());
	}
	else if (configSpecified)
	{
		MP_LOG_ERROR("Could not open the configuration file %s", configPath.c_str());
		return 1;
	}

	// 2) Command line options.
	for (int i = 1; i < argc; ++i)
	{
		const std::string argument = argv[i];

		auto nextValue = [&](const char* name, std::string& dst) -> bool
		{
			if (i + 1 >= argc)
			{
				MP_LOG_ERROR("Missing value for %s", name);
				return false;
			}
			dst = argv[++i];
			return true;
		};

		std::string value;
		int number = 0;

		if (argument == "--help" || argument == "-h" || argument == "/?")
		{
			PrintUsage();
			return 0;
		}
		else if (argument == "--version")
		{
			std::printf("%s server, protocol v%u\n", SporeMP::kProtocolName, SporeMP::kProtocolVersion);
			return 0;
		}
		else if (argument == "--config")
		{
			// Already handled above.
			++i;
		}
		else if (argument == "--port")
		{
			if (!nextValue("--port", value) || !ParseInt(value, number)) return 1;
			config.port = static_cast<uint16_t>(number);
		}
		else if (argument == "--bind")
		{
			if (!nextValue("--bind", value)) return 1;
			config.bindAddress = value;
		}
		else if (argument == "--name")
		{
			if (!nextValue("--name", value)) return 1;
			config.sessionName = value;
		}
		else if (argument == "--max-players")
		{
			if (!nextValue("--max-players", value) || !ParseInt(value, number)) return 1;
			config.maxPlayers = number;
		}
		else if (argument == "--tick-rate")
		{
			if (!nextValue("--tick-rate", value) || !ParseInt(value, number)) return 1;
			config.tickRate = number;
		}
		else if (argument == "--timeout")
		{
			if (!nextValue("--timeout", value) || !ParseInt(value, number)) return 1;
			config.timeoutSeconds = number;
		}
		else if (argument == "--log-level")
		{
			if (!nextValue("--log-level", value)) return 1;
			SporeMP::LogLevel level;
			if (!SporeMP::ParseLogLevel(value.c_str(), level))
			{
				MP_LOG_ERROR("Unknown log level '%s'", value.c_str());
				return 1;
			}
			SporeMP::SetLogLevel(level);
		}
		else if (argument == "--allow-duplicate-names")
		{
			config.allowDuplicateNames = true;
		}
		else
		{
			MP_LOG_ERROR("Unknown option '%s' (use --help)", argument.c_str());
			return 1;
		}
	}

	// 3) Validation.
	if (config.port == 0)
	{
		MP_LOG_ERROR("The port must be between 1 and 65535");
		return 1;
	}
	if (config.maxPlayers < 1 || config.maxPlayers > SporeMP::kMaxPlayers)
	{
		MP_LOG_ERROR("--max-players must be between 1 and %d", SporeMP::kMaxPlayers);
		return 1;
	}
	if (config.tickRate < 1 || config.tickRate > 60)
	{
		MP_LOG_ERROR("--tick-rate must be between 1 and 60");
		return 1;
	}
	if (config.timeoutSeconds < 5)
	{
		MP_LOG_ERROR("--timeout must be at least 5 seconds");
		return 1;
	}

	std::printf("%s server, protocol v%u\n", SporeMP::kProtocolName, SporeMP::kProtocolVersion);
	std::printf("Type 'help' and press Enter to see the console commands.\n\n");

	SporeMP::Server server(config);
	if (!server.Start())
	{
		return 1;
	}

	g_server = &server;
	SetConsoleCtrlHandler(ConsoleHandler, TRUE);

	// Console commands are queued and executed by the server loop, so the
	// client list is never accessed from two threads at the same time.
	std::thread inputThread([&server]()
	{
		std::string line;
		while (std::getline(std::cin, line))
		{
			line = Trim(line);
			if (!line.empty())
			{
				server.QueueCommand(line);
			}
		}
	});
	inputThread.detach();

	server.Run();

	g_server = nullptr;
	return 0;
}
