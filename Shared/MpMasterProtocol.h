/****************************************************************************
* Spore Multiplayer
*
* Master server protocol.
*
* The master keeps the list of public sessions so players can find each other
* over the internet, instead of typing IP addresses.
*
* Unlike the game protocol, this one is plain HTTP with JSON bodies, so any
* client can talk to it: the mod, a launcher, a web page, curl...
*
*   POST /api/v1/register    {"name":"...","port":7000,"players":0,"maxPlayers":8}
*                            -> {"token":"...","interval":15,"publicPort":7000}
*   POST /api/v1/heartbeat   {"token":"...","players":3}
*                            -> {"ok":true}
*   POST /api/v1/unregister  {"token":"..."}
*                            -> {"ok":true}
*   GET  /api/v1/servers     -> {"count":1,"servers":[{"name":"...","host":"1.2.3.4",
*                               "port":7000,"players":2,"maxPlayers":8,"version":1}]}
*   GET  /api/v1/health      -> {"ok":true,"servers":1,"uptime":123}
*
* This header stays dependency free: it is compiled by the master, the dedicated
* server and the mod.
****************************************************************************/
#pragma once

#include <cstdint>
#include <cstddef>
#include <cstring>
#include <string>

namespace SporeMP
{
	/// Version of the master protocol. Bump it when the JSON changes.
	constexpr int kMasterProtocolVersion = 1;

	/// Default HTTP port of the master server.
	constexpr uint16_t kDefaultMasterPort = 17401;

	/// How often a registered server must send a heartbeat, in seconds.
	constexpr int kMasterHeartbeatSeconds = 15;

	/// A server that stops sending heartbeats is dropped after this many seconds.
	constexpr int kMasterTimeoutSeconds = 45;

	/// Maximum size of a request body accepted by the master.
	constexpr size_t kMasterMaxBodySize = 4096;

	/// Maximum number of sessions the master keeps.
	constexpr size_t kMasterMaxServers = 512;

	// ------------------------------------------------------------------------
	// Minimal JSON helpers
	//
	// Both ends are ours, so a full JSON parser is not needed: these helpers read
	// the fields the protocol defines, and nothing else.
	// ------------------------------------------------------------------------

	/// Escapes a string so it can be embedded in a JSON string literal.
	inline std::string JsonEscape(const std::string& value)
	{
		std::string result;
		result.reserve(value.size() + 8);

		for (char c : value)
		{
			switch (c)
			{
			case '"': result += "\\\""; break;
			case '\\': result += "\\\\"; break;
			case '\n': result += "\\n"; break;
			case '\r': result += "\\r"; break;
			case '\t': result += "\\t"; break;
			default:
				// Control characters are not allowed in the protocol.
				if (static_cast<unsigned char>(c) < 0x20) break;
				result += c;
				break;
			}
		}

		return result;
	}

	/// Finds the value of a field in a flat JSON object.
	/// @param json The JSON text.
	/// @param key The field name, without quotes.
	/// @param[out] dst The raw value (string contents are unescaped).
	/// @returns true if the field exists.
	inline bool JsonGetRaw(const std::string& json, const char* key, std::string& dst)
	{
		const std::string pattern = std::string("\"") + key + "\"";
		size_t position = json.find(pattern);
		if (position == std::string::npos) return false;

		position = json.find(':', position + pattern.size());
		if (position == std::string::npos) return false;

		++position;
		while (position < json.size() && (json[position] == ' ' || json[position] == '\t')) ++position;
		if (position >= json.size()) return false;

		if (json[position] == '"')
		{
			++position;
			std::string value;
			while (position < json.size() && json[position] != '"')
			{
				if (json[position] == '\\' && position + 1 < json.size())
				{
					++position;
					switch (json[position])
					{
					case 'n': value += '\n'; break;
					case 'r': value += '\r'; break;
					case 't': value += '\t'; break;
					default: value += json[position]; break;
					}
				}
				else
				{
					value += json[position];
				}
				++position;
			}

			dst = value;
			return true;
		}

		// Number, true, false or null: read until a delimiter.
		size_t end = position;
		while (end < json.size() && json[end] != ',' && json[end] != '}' && json[end] != ' ') ++end;

		dst = json.substr(position, end - position);
		return true;
	}

	/// Reads an integer field. Returns `fallback` when the field is missing or invalid.
	inline int JsonGetInt(const std::string& json, const char* key, int fallback)
	{
		std::string raw;
		if (!JsonGetRaw(json, key, raw)) return fallback;

		if (raw.empty()) return fallback;

		char* end = nullptr;
		const long value = std::strtol(raw.c_str(), &end, 10);
		if (end == raw.c_str()) return fallback;

		return static_cast<int>(value);
	}

	/// Reads a string field. Returns an empty string when the field is missing.
	inline std::string JsonGetString(const std::string& json, const char* key)
	{
		std::string raw;
		if (!JsonGetRaw(json, key, raw)) return std::string();
		return raw;
	}

	/// Reads a boolean field.
	inline bool JsonGetBool(const std::string& json, const char* key, bool fallback)
	{
		std::string raw;
		if (!JsonGetRaw(json, key, raw)) return fallback;
		return raw == "true" || raw == "1";
	}
}
