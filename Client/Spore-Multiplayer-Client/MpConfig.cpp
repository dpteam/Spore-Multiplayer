/****************************************************************************
* Spore Multiplayer
****************************************************************************/
#include "stdafx.h"
#include "MpConfig.h"

#include <cstdio>
#include <fstream>
#include <sstream>

namespace SporeMP
{
	namespace
	{
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

		bool ToBool(const std::string& value)
		{
			const std::string lower = ToLower(value);
			return lower == "1" || lower == "true" || lower == "yes" || lower == "on";
		}

		bool ToInt(const std::string& value, int& dst)
		{
			if (value.empty()) return false;

			char* end = nullptr;
			const long result = std::strtol(value.c_str(), &end, 10);
			if (end == nullptr || *end != '\0') return false;

			dst = static_cast<int>(result);
			return true;
		}

		bool ToFloat(const std::string& value, float& dst)
		{
			if (value.empty()) return false;

			char* end = nullptr;
			const double result = std::strtod(value.c_str(), &end);
			if (end == nullptr || *end != '\0') return false;

			dst = static_cast<float>(result);
			return true;
		}
	}

	bool LoadClientConfig(const std::string& path, ClientConfig& config)
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
			float decimal = 0.0f;

			if (key == "host") config.host = value;
			else if (key == "port" && ToInt(value, number) && number > 0 && number < 65536) config.port = static_cast<uint16_t>(number);
			else if (key == "name") config.name = value;
			else if (key == "auto_connect") config.autoConnect = ToBool(value);
			else if (key == "hud") config.hud = ToBool(value);
			else if (key == "hud_x" && ToFloat(value, decimal)) config.hudX = decimal;
			else if (key == "hud_y" && ToFloat(value, decimal)) config.hudY = decimal;
			else if (key == "hud_scale" && ToFloat(value, decimal) && decimal > 0.1f && decimal < 4.0f) config.hudScale = decimal;
			else if (key == "chat_lines" && ToInt(value, number) && number >= 0 && number <= 20) config.chatLines = number;
			else if (key == "send_rate" && ToInt(value, number) && number >= 1 && number <= 60) config.sendRate = number;
			else if (key == "show_avatars") config.showAvatars = ToBool(value);
			else if (key == "interpolation") config.interpolation = ToBool(value);
			else if (key == "name_tags") config.nameTags = ToBool(value);
		}

		return true;
	}

	bool SaveClientConfig(const std::string& path, const ClientConfig& config)	{
		std::ofstream file(path.c_str(), std::ios::trunc);
		if (!file.is_open()) return false;

		file << "; Spore Multiplayer client configuration\n"
			"\n"
			"[connection]\n"
			"; Address of the Spore-Multiplayer-Server.exe instance.\n"
			"host=" << config.host << "\n"
			"port=" << config.port << "\n"
			"; Name shown to the other players. Empty means \"use the Spore profile name\".\n"
			"name=" << config.name << "\n"
			"; Connect automatically when the game starts.\n"
			"auto_connect=" << (config.autoConnect ? 1 : 0) << "\n"
			"\n"
			"[display]\n"
			"hud=" << (config.hud ? 1 : 0) << "\n"
			"hud_x=" << config.hudX << "\n"
			"hud_y=" << config.hudY << "\n"
			"hud_scale=" << config.hudScale << "\n"
			"chat_lines=" << config.chatLines << "\n"
			"\n"
			"[sync]\n"
			"; How many times per second the local player state is sent (1-60).\n"
			"send_rate=" << config.sendRate << "\n"
			"; Show the other players as ships/creatures in the world.\n"
			"show_avatars=" << (config.showAvatars ? 1 : 0) << "\n"
			"; Smooth the movement of the other players.\n"
			"interpolation=" << (config.interpolation ? 1 : 0) << "\n"
			"; Draw the player names above the remote avatars.\n"
			"name_tags=" << (config.nameTags ? 1 : 0) << "\n";

		return true;
	}

	std::string ResolveDataDirectory(const std::string& preferred)
	{
		// Probe the preferred directory by creating a small file in it.
		if (!preferred.empty())
		{
			const std::string probePath = preferred + "SporeMultiplayer.probe";
			std::ofstream probe(probePath.c_str(), std::ios::trunc);
			if (probe.is_open())
			{
				probe.close();
				::DeleteFileA(probePath.c_str());
				return preferred;
			}
		}

		// Fall back to the per-user application data folder.
		char buffer[MAX_PATH] = { 0 };
		const DWORD length = GetEnvironmentVariableA("APPDATA", buffer, MAX_PATH);
		if (length > 0 && length < MAX_PATH)
		{
			const std::string directory = std::string(buffer) + "\\SporeMultiplayer\\";
			CreateDirectoryA(directory.c_str(), nullptr);
			return directory;
		}

		return preferred;
	}
}
