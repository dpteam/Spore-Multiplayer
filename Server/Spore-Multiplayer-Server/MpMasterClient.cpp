/****************************************************************************
* Spore Multiplayer - Dedicated server
****************************************************************************/
#include "MpMasterClient.h"
#include "MpLog.h"

#include <winsock2.h>
#include <ws2tcpip.h>

#pragma comment(lib, "ws2_32.lib")

#include <chrono>
#include <cstring>

#include "../../Shared/MpMasterProtocol.h"

namespace SporeMP
{
	namespace
	{
		/// Ensures Winsock is initialized (the server does it too, this is just safe).
		bool EnsureWinsock()
		{
			static bool done = false;
			if (done) return true;

			WSADATA data;
			done = WSAStartup(MAKEWORD(2, 2), &data) == 0;
			return done;
		}
	}

	bool ParseMasterUrl(const std::string& url, std::string& host, uint16_t& port, std::string& path)
	{
		std::string rest = url;

		// Strip the scheme.
		const size_t scheme = rest.find("://");
		if (scheme != std::string::npos)
		{
			rest = rest.substr(scheme + 3);
		}

		// Split the path.
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

		// Split host and port.
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

	bool HttpPost(const std::string& host, uint16_t port, const std::string& path,
		const std::string& body, std::string& response)
	{
		if (!EnsureWinsock()) return false;

		addrinfo hints;
		std::memset(&hints, 0, sizeof(hints));
		hints.ai_family = AF_INET;
		hints.ai_socktype = SOCK_STREAM;

		addrinfo* result = nullptr;
		if (getaddrinfo(host.c_str(), nullptr, &hints, &result) != 0 || result == nullptr)
		{
			return false;
		}

		sockaddr_in address;
		std::memset(&address, 0, sizeof(address));
		address.sin_family = AF_INET;
		address.sin_port = htons(port);
		address.sin_addr = reinterpret_cast<sockaddr_in*>(result->ai_addr)->sin_addr;
		freeaddrinfo(result);

		SOCKET sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
		if (sock == INVALID_SOCKET) return false;

		DWORD timeout = 5000;
		setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeout), sizeof(timeout));
		setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&timeout), sizeof(timeout));

		if (connect(sock, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == SOCKET_ERROR)
		{
			closesocket(sock);
			return false;
		}

		const std::string request =
			"POST " + (path.empty() ? std::string("/") : path) + " HTTP/1.1\r\n"
			"Host: " + host + "\r\n"
			"Content-Type: application/json\r\n"
			"Content-Length: " + std::to_string(body.size()) + "\r\n"
			"Connection: close\r\n"
			"\r\n" + body;

		size_t offset = 0;
		while (offset < request.size())
		{
			const int sent = send(sock, request.c_str() + offset, static_cast<int>(request.size() - offset), 0);
			if (sent <= 0)
			{
				closesocket(sock);
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
			if (raw.size() > 65536) break;
		}

		closesocket(sock);

		// Check the status line and keep the body.
		if (raw.compare(0, 9, "HTTP/1.1 ") != 0 && raw.compare(0, 9, "HTTP/1.0 ") != 0)
		{
			return false;
		}

		const size_t statusStart = 9;
		const int status = std::atoi(raw.c_str() + statusStart);
		if (status < 200 || status >= 300) return false;

		const size_t bodyStart = raw.find("\r\n\r\n");
		if (bodyStart == std::string::npos) return false;

		response = raw.substr(bodyStart + 4);
		return true;
	}

	MasterRegistration::MasterRegistration()
	{
	}

	MasterRegistration::~MasterRegistration()
	{
		Stop();
	}

	bool MasterRegistration::Start(const std::string& masterUrl, const std::string& sessionName,
		uint16_t gamePort, int maxPlayers, std::atomic<int>* playerCount)
	{
		std::string host;
		uint16_t port = 0;
		std::string path;

		if (!ParseMasterUrl(masterUrl, host, port, path))
		{
			MP_LOG_ERROR("Invalid master URL: %s", masterUrl.c_str());
			return false;
		}

		// Default to the register endpoint when only a host was given.
		if (path.empty() || path == "/")
		{
			path = "/api/v1/register";
		}

		// /api/v1/register -> the heartbeat endpoint is a sibling.
		const size_t lastSlash = path.find_last_of('/');
		mPath = (lastSlash == std::string::npos) ? path : path.substr(0, lastSlash);

		mHost = host;
		mPort = port;
		mStopRequested.store(false);

		MP_LOG_INFO("Registering on the master at %s:%u", host.c_str(), port);

		mThread = std::thread(&MasterRegistration::ThreadMain, this, host, port, mPath,
			sessionName, gamePort, maxPlayers, playerCount);

		return true;
	}

	void MasterRegistration::Stop()
	{
		mStopRequested.store(true);

		if (mThread.joinable())
		{
			mThread.join();
		}

		// Best effort: remove the session so it disappears from the list at once.
		if (!mToken.empty() && !mHost.empty())
		{
			const std::string body = "{\"token\":\"" + JsonEscape(mToken) + "\"}";
			std::string response;
			HttpPost(mHost, mPort, mPath + "/unregister", body, response);
			MP_LOG_INFO("Unregistered from the master");
		}

		mToken.clear();
		mRegistered.store(false);
	}

	void MasterRegistration::ThreadMain(std::string host, uint16_t port, std::string basePath,
		std::string sessionName, uint16_t gamePort, int maxPlayers,
		std::atomic<int>* playerCount)
	{
		int interval = kMasterHeartbeatSeconds;

		// Register, retrying a few times in case the master is still starting.
		for (int attempt = 0; attempt < 3 && !mStopRequested.load(); ++attempt)
		{
			const std::string body =
				"{\"name\":\"" + JsonEscape(sessionName) + "\""
				",\"port\":" + std::to_string(gamePort) +
				",\"players\":0"
				",\"maxPlayers\":" + std::to_string(maxPlayers) +
				",\"version\":" + std::to_string(kMasterProtocolVersion) + "}";

			std::string response;
			if (HttpPost(host, port, basePath + "/register", body, response))
			{
				mToken = JsonGetString(response, "token");
				interval = JsonGetInt(response, "interval", kMasterHeartbeatSeconds);

				if (!mToken.empty())
				{
					mRegistered.store(true);
					MP_LOG_INFO("Registered on the master (heartbeat every %d s)", interval);
					break;
				}

				MP_LOG_WARN("The master did not return a token");
			}
			else
			{
				MP_LOG_WARN("Could not reach the master (attempt %d)", attempt + 1);
			}

			std::this_thread::sleep_for(std::chrono::seconds(5));
		}

		if (!mRegistered.load())
		{
			MP_LOG_WARN("Giving up on the master registration, the session stays LAN only");
			return;
		}

		// Heartbeats.
		while (!mStopRequested.load())
		{
			for (int second = 0; second < interval && !mStopRequested.load(); ++second)
			{
				std::this_thread::sleep_for(std::chrono::seconds(1));
			}

			if (mStopRequested.load()) break;

			const int players = playerCount != nullptr ? playerCount->load() : 0;

			const std::string body =
				"{\"token\":\"" + JsonEscape(mToken) + "\""
				",\"players\":" + std::to_string(players) + "}";

			std::string response;
			if (!HttpPost(host, port, basePath + "/heartbeat", body, response))
			{
				MP_LOG_WARN("Heartbeat to the master failed, re-registering");
				mRegistered.store(false);

				// Try to register again; the master may have restarted.
				const std::string registerBody =
					"{\"name\":\"" + JsonEscape(sessionName) + "\""
					",\"port\":" + std::to_string(gamePort) +
					",\"players\":" + std::to_string(players) +
					",\"maxPlayers\":" + std::to_string(maxPlayers) +
					",\"version\":" + std::to_string(kMasterProtocolVersion) + "}";

				if (HttpPost(host, port, basePath + "/register", registerBody, response))
				{
					const std::string token = JsonGetString(response, "token");
					if (!token.empty())
					{
						mToken = token;
						mRegistered.store(true);
						MP_LOG_INFO("Re-registered on the master");
					}
				}
			}
		}
	}
}
