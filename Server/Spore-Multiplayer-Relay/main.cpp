/****************************************************************************
* Spore Multiplayer - Relay server
*
* Entry point. Usage:
*     Spore-Multiplayer-Relay.exe [--port 17402] [--bind 0.0.0.0]
*                                 [--timeout 120] [--log-level info]
*                                 [--config relay.ini]
****************************************************************************/
#include "MpRelay.h"
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
	SporeMP::Relay* g_relay = nullptr;

	BOOL WINAPI ConsoleHandler(DWORD signal)
	{
		switch (signal)
		{
		case CTRL_C_EVENT:
		case CTRL_BREAK_EVENT:
		case CTRL_CLOSE_EVENT:
		case CTRL_SHUTDOWN_EVENT:
			if (g_relay != nullptr) g_relay->RequestStop();
			return TRUE;
		default:
			return FALSE;
		}
	}

	std::string Trim(const std::string& value)
	{
		size_t begin = 0;
		size_t end = value.size();

		while (begin < end && (value[begin] == ' ' || value[begin] == '\t' || value[begin] == '\r' || value[begin] == '\n')) ++begin;
		while (end > begin && (value[end - 1] == ' ' || value[end - 1] == '\t' || value[end - 1] == '\r' || value[end - 1] == '\n')) --end;

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
			"Spore Multiplayer - relay server\n"
			"\n"
			"Usage: Spore-Multiplayer-Relay.exe [options]\n"
			"\n"
			"Options:\n"
			"  --port <number>       TCP port (default: %u)\n"
			"  --bind <address>      Address to bind (default: 0.0.0.0)\n"
			"  --timeout <seconds>   Drop a waiting room after this long (default: %d)\n"
			"  --log-level <level>   debug, info, warning or error (default: info)\n"
			"  --config <file>       Read the options from an ini file\n"
			"  --help, --version\n"
			"\n"
			"The relay forwards the game stream between a host and a client, so\n"
			"neither needs an open port. It solves NAT without port forwarding,\n"
			"CGNAT and broken NAT hairpin.\n",
			SporeMP::kDefaultRelayPort, SporeMP::kRelayRoomTimeoutSeconds);
	}

	bool LoadIniFile(const std::string& path, SporeMP::RelayConfig& config)
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
			else if (key == "timeout" && ParseInt(value, number)) config.roomTimeoutSeconds = number;
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
	SporeMP::RelayConfig config;
	SporeMP::SetLogLevel(SporeMP::LogLevel::Info);

	std::string configPath = GetExecutableDirectory() + "relay.ini";
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
			std::printf("Spore Multiplayer relay\n");
			return 0;
		}
		else if (argument == "--config")
		{
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
		else if (argument == "--timeout")
		{
			if (!nextValue("--timeout", value) || !ParseInt(value, number)) return 1;
			config.roomTimeoutSeconds = number;
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
		else
		{
			MP_LOG_ERROR("Unknown option '%s' (use --help)", argument.c_str());
			return 1;
		}
	}

	if (config.port == 0)
	{
		MP_LOG_ERROR("The port must be between 1 and 65535");
		return 1;
	}
	if (config.roomTimeoutSeconds < 10)
	{
		MP_LOG_ERROR("--timeout must be at least 10 seconds");
		return 1;
	}

	std::printf("Spore Multiplayer relay\n");
	std::printf("Type 'help' and press Enter to see the console commands.\n\n");

	SporeMP::Relay relay(config);
	if (!relay.Start())
	{
		return 1;
	}

	g_relay = &relay;
	SetConsoleCtrlHandler(ConsoleHandler, TRUE);

	std::thread inputThread([&relay]()
	{
		std::string line;
		while (std::getline(std::cin, line))
		{
			line = Trim(line);
			if (!line.empty()) relay.QueueCommand(line);
		}
	});
	inputThread.detach();

	relay.Run();

	g_relay = nullptr;
	return 0;
}
