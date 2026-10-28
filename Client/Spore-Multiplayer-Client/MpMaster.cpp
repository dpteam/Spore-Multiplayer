/****************************************************************************
* Spore Multiplayer
****************************************************************************/
#include "stdafx.h"
#include "MpMaster.h"
#include "MpLog.h"

#include <winsock2.h>
#include <ws2tcpip.h>

#pragma comment(lib, "ws2_32.lib")

#include <cstdlib>
#include <cstring>
#include <mutex>

namespace SporeMP
{
	namespace
	{
		bool EnsureWinsock()
		{
			static std::once_flag flag;
			static bool result = false;

			std::call_once(flag, []()
			{
				WSADATA data;
				result = WSAStartup(MAKEWORD(2, 2), &data) == 0;
			});

			return result;
		}

		/// Splits "http://host:port/path" into its parts.
		bool ParseUrl(const std::string& url, std::string& host, uint16_t& port, std::string& path)
		{
			std::string rest = url;

			const size_t scheme = rest.find("://");
			if (scheme != std::string::npos)
			{
				rest = rest.substr(scheme + 3);
			}

			const size_t slash = rest.find('/');
			if (slash != std::string::npos)
			{
				path = rest.substr(slash);
				rest = rest.substr(0, slash);
			}
			else
			{
				path.clear();
			}

			const size_t colon = rest.find(':');
			if (colon != std::string::npos)
			{
				host = rest.substr(0, colon);
				port = static_cast<uint16_t>(std::strtoul(rest.c_str() + colon + 1, nullptr, 10));
			}
			else
			{
				host = rest;
				port = kDefaultMasterPort;
			}

			return !host.empty() && port != 0;
		}
	}

	bool MasterQuery::HttpGet(const std::string& host, uint16_t port, const std::string& path,
		std::string& response, std::string& error)
	{
		if (!EnsureWinsock())
		{
			error = "Could not initialize Winsock";
			return false;
		}

		addrinfo hints;
		std::memset(&hints, 0, sizeof(hints));
		hints.ai_family = AF_INET;
		hints.ai_socktype = SOCK_STREAM;

		addrinfo* result = nullptr;
		if (getaddrinfo(host.c_str(), nullptr, &hints, &result) != 0 || result == nullptr)
		{
			error = "Could not resolve " + host;
			return false;
		}

		sockaddr_in address;
		std::memset(&address, 0, sizeof(address));
		address.sin_family = AF_INET;
		address.sin_port = htons(port);
		address.sin_addr = reinterpret_cast<sockaddr_in*>(result->ai_addr)->sin_addr;
		freeaddrinfo(result);

		SOCKET sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
		if (sock == INVALID_SOCKET)
		{
			error = "Could not create the socket";
			return false;
		}

		// Short timeouts: this runs on the game thread.
		DWORD timeout = 4000;
		setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeout), sizeof(timeout));
		setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&timeout), sizeof(timeout));

		if (connect(sock, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == SOCKET_ERROR)
		{
			closesocket(sock);
			error = "Could not connect to the master";
			return false;
		}

		const std::string request =
			"GET " + (path.empty() ? std::string("/") : path) + " HTTP/1.1\r\n"
			"Host: " + host + "\r\n"
			"Connection: close\r\n"
			"\r\n";

		size_t offset = 0;
		while (offset < request.size())
		{
			const int sent = send(sock, request.c_str() + offset, static_cast<int>(request.size() - offset), 0);
			if (sent <= 0)
			{
				closesocket(sock);
				error = "Could not send the request";
				return false;
			}
			offset += static_cast<size_t>(sent);
		}

		std::string raw;
		char buffer[2048];
		while (true)
		{
			const int received = recv(sock, buffer, sizeof(buffer), 0);
			if (received <= 0) break;
			raw.append(buffer, static_cast<size_t>(received));
			if (raw.size() > 262144) break;
		}

		closesocket(sock);

		if (raw.compare(0, 9, "HTTP/1.1 ") != 0 && raw.compare(0, 9, "HTTP/1.0 ") != 0)
		{
			error = "The master sent an invalid response";
			return false;
		}

		const int status = std::atoi(raw.c_str() + 9);
		if (status != 200)
		{
			error = "The master answered with status " + std::to_string(status);
			return false;
		}

		const size_t bodyStart = raw.find("\r\n\r\n");
		if (bodyStart == std::string::npos)
		{
			error = "The master sent an empty response";
			return false;
		}

		response = raw.substr(bodyStart + 4);
		return true;
	}

	bool MasterQuery::Fetch(const std::string& masterUrl, std::vector<PublicSession>& dst, std::string& error)
	{
		std::string host;
		uint16_t port = 0;
		std::string path;

		if (!ParseUrl(masterUrl, host, port, path))
		{
			error = "Invalid master address: " + masterUrl;
			return false;
		}

		if (path.empty() || path == "/")
		{
			path = "/api/v1/servers";
		}

		std::string response;
		if (!HttpGet(host, port, path, response, error))
		{
			return false;
		}

		return ParseServerList(response, dst);
	}

	bool MasterQuery::ParseServerList(const std::string& json, std::vector<PublicSession>& dst)
	{
		dst.clear();

		// The servers are objects inside the "servers" array; each one is parsed
		// with the flat helpers from the shared header.
		const size_t arrayStart = json.find("\"servers\"");
		if (arrayStart == std::string::npos) return false;

		const size_t open = json.find('[', arrayStart);
		if (open == std::string::npos) return false;

		size_t position = open + 1;
		while (true)
		{
			const size_t objectStart = json.find('{', position);
			if (objectStart == std::string::npos) break;

			const size_t objectEnd = json.find('}', objectStart);
			if (objectEnd == std::string::npos) break;

			const std::string object = json.substr(objectStart, objectEnd - objectStart + 1);

			PublicSession session;
			session.name = JsonGetString(object, "name");
			session.host = JsonGetString(object, "host");
			session.port = static_cast<uint16_t>(JsonGetInt(object, "port", 0));
			session.players = JsonGetInt(object, "players", 0);
			session.maxPlayers = JsonGetInt(object, "maxPlayers", 0);
			session.version = JsonGetInt(object, "version", 0);

			if (!session.host.empty() && session.port != 0)
			{
				dst.push_back(session);
			}

			position = objectEnd + 1;

			// Stop at the end of the array.
			const size_t nextComma = json.find(',', position);
			const size_t close = json.find(']', position);
			if (close != std::string::npos && (nextComma == std::string::npos || close < nextComma))
			{
				break;
			}
		}

		return true;
	}
}
