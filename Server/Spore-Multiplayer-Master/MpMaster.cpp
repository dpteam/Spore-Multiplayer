/****************************************************************************
* Spore Multiplayer - Master server
****************************************************************************/
#include "MpMaster.h"
#include "MpLog.h"

#include <winsock2.h>
#include <ws2tcpip.h>

#pragma comment(lib, "ws2_32.lib")

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <sstream>

namespace SporeMP
{
	namespace
	{
		const uintptr_t kInvalidSocket = static_cast<uintptr_t>(INVALID_SOCKET);

		SOCKET ToSocket(uintptr_t value)
		{
			return static_cast<SOCKET>(value);
		}

		uint64_t NowMs()
		{
			return GetTickCount64();
		}

		const char* StatusText(int status)
		{
			switch (status)
			{
			case 200: return "OK";
			case 400: return "Bad Request";
			case 404: return "Not Found";
			case 405: return "Method Not Allowed";
			case 429: return "Too Many Requests";
			case 503: return "Service Unavailable";
			default: return "OK";
			}
		}

		/// Reads a header value (case insensitive name).
		bool GetHeader(const std::string& headers, const char* name, std::string& dst)
		{
			std::string lowerName(name);
			for (char& c : lowerName)
			{
				if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
			}

			std::istringstream stream(headers);
			std::string line;
			while (std::getline(stream, line))
			{
				const size_t separator = line.find(':');
				if (separator == std::string::npos) continue;

				std::string key = line.substr(0, separator);
				for (char& c : key)
				{
					if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
				}

				if (key != lowerName) continue;

				std::string value = line.substr(separator + 1);
				const size_t begin = value.find_first_not_of(" \t\r");
				if (begin == std::string::npos)
				{
					dst.clear();
					return true;
				}
				const size_t end = value.find_last_not_of(" \t\r");
				dst = value.substr(begin, end - begin + 1);
				return true;
			}

			return false;
		}
	}

	Master::Master(const MasterConfig& config)
		: mConfig(config)
	{
	}

	Master::~Master()
	{
		if (mListenSocket != kInvalidSocket)
		{
			closesocket(ToSocket(mListenSocket));
			mListenSocket = kInvalidSocket;
		}
		WSACleanup();
	}

	bool Master::Start()
	{
		WSADATA wsaData;
		if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
		{
			MP_LOG_ERROR("WSAStartup failed with error %d", WSAGetLastError());
			return false;
		}

		SOCKET listenSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
		if (listenSocket == INVALID_SOCKET)
		{
			MP_LOG_ERROR("Could not create the listening socket (error %d)", WSAGetLastError());
			return false;
		}

		BOOL reuse = TRUE;
		setsockopt(listenSocket, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&reuse), sizeof(reuse));

		sockaddr_in address;
		std::memset(&address, 0, sizeof(address));
		address.sin_family = AF_INET;
		address.sin_port = htons(mConfig.port);

		if (inet_pton(AF_INET, mConfig.bindAddress.c_str(), &address.sin_addr) != 1)
		{
			MP_LOG_WARN("Invalid bind address '%s', using 0.0.0.0", mConfig.bindAddress.c_str());
			address.sin_addr.s_addr = htonl(INADDR_ANY);
		}

		if (bind(listenSocket, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == SOCKET_ERROR)
		{
			const int error = WSAGetLastError();
			if (error == WSAEADDRINUSE)
			{
				MP_LOG_ERROR("Port %d is already in use", mConfig.port);
			}
			else
			{
				MP_LOG_ERROR("Could not bind port %d (error %d)", mConfig.port, error);
			}
			closesocket(listenSocket);
			return false;
		}

		if (listen(listenSocket, SOMAXCONN) == SOCKET_ERROR)
		{
			MP_LOG_ERROR("Could not listen on port %d (error %d)", mConfig.port, WSAGetLastError());
			closesocket(listenSocket);
			return false;
		}

		u_long nonBlocking = 1;
		ioctlsocket(listenSocket, FIONBIO, &nonBlocking);

		mListenSocket = static_cast<uintptr_t>(listenSocket);
		mStartTime = NowMs();
		mRunning = true;

		MP_LOG_INFO("Master server listening on %s:%d (HTTP)",
			mConfig.bindAddress.c_str(), mConfig.port);
		MP_LOG_INFO("Heartbeat every %d s, sessions time out after %d s",
			mConfig.heartbeatSeconds, mConfig.timeoutSeconds);
		return true;
	}

	void Master::RequestStop()
	{
		mStopRequested.store(true);
	}

	void Master::QueueCommand(const std::string& command)
	{
		std::lock_guard<std::mutex> lock(mCommandMutex);
		mCommands.push_back(command);
	}

	void Master::Run()
	{
		while (mRunning && !mStopRequested.load())
		{
			fd_set readSet;
			FD_ZERO(&readSet);
			FD_SET(ToSocket(mListenSocket), &readSet);

			timeval timeout;
			timeout.tv_sec = 0;
			timeout.tv_usec = 20000;  // 20 ms

			const int result = select(0, &readSet, nullptr, nullptr, &timeout);

			if (result > 0 && FD_ISSET(ToSocket(mListenSocket), &readSet))
			{
				AcceptConnections();
			}

			CheckTimeouts();

			// Console commands.
			std::vector<std::string> commands;
			{
				std::lock_guard<std::mutex> lock(mCommandMutex);
				commands.swap(mCommands);
			}

			for (const std::string& command : commands)
			{
				if (command == "stop" || command == "quit" || command == "exit")
				{
					MP_LOG_INFO("Stop requested from the console");
					RequestStop();
				}
				else if (command == "list" || command == "servers")
				{
					std::printf("Registered sessions (%zu):\n", mSessions.size());
					for (const Session& session : mSessions)
					{
						std::printf("  %-24s %s:%-5u %d/%d\n", session.name.c_str(),
							session.host.c_str(), session.port, session.players, session.maxPlayers);
					}
				}
				else if (command == "status")
				{
					const uint64_t uptime = (NowMs() - mStartTime) / 1000;
					std::printf("Master: %zu sessions, up %llu s, protocol v%d\n",
						mSessions.size(), static_cast<unsigned long long>(uptime), kMasterProtocolVersion);
				}
				else if (command == "help")
				{
					std::printf(
						"Commands:\n"
						"  list    - show the registered sessions\n"
						"  status  - uptime and session count\n"
						"  stop    - shut the master down\n");
				}
				else if (!command.empty())
				{
					std::printf("Unknown command '%s'\n", command.c_str());
				}
			}
		}

		if (mListenSocket != kInvalidSocket)
		{
			closesocket(ToSocket(mListenSocket));
			mListenSocket = kInvalidSocket;
		}

		mRunning = false;
		MP_LOG_INFO("Master server stopped");
	}

	void Master::AcceptConnections()
	{
		while (true)
		{
			sockaddr_in address;
			int addressLength = sizeof(address);
			SOCKET clientSocket = accept(ToSocket(mListenSocket), reinterpret_cast<sockaddr*>(&address), &addressLength);

			if (clientSocket == INVALID_SOCKET)
			{
				const int error = WSAGetLastError();
				if (error != WSAEWOULDBLOCK)
				{
					MP_LOG_WARN("accept() failed (error %d)", error);
				}
				break;
			}

			char ip[64] = { 0 };
			inet_ntop(AF_INET, &address.sin_addr, ip, sizeof(ip));

			// Accepted sockets inherit the non-blocking mode of the listener on
			// Windows, so it must be switched back to blocking for the request to
			// be read normally.
			u_long blocking = 0;
			ioctlsocket(clientSocket, FIONBIO, &blocking);

			// Requests are tiny and fast, so they are served inline. A timeout is
			// set so a stuck client cannot block the whole master.
			DWORD timeout = 3000;
			setsockopt(clientSocket, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeout), sizeof(timeout));
			setsockopt(clientSocket, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&timeout), sizeof(timeout));

			ServeRequest(static_cast<uintptr_t>(clientSocket), ip);

			// Close gracefully: shutdown the sending side first and drain whatever
			// the client still has in flight, otherwise Windows can reset the
			// connection and the response is lost.
			shutdown(clientSocket, SD_SEND);

			{
				// Short timeout so draining cannot stall the master.
				DWORD drainTimeout = 50;
				setsockopt(clientSocket, SOL_SOCKET, SO_RCVTIMEO,
					reinterpret_cast<const char*>(&drainTimeout), sizeof(drainTimeout));

				char drain[512];
				int guard = 0;
				while (guard++ < 16)
				{
					const int received = recv(clientSocket, drain, sizeof(drain), 0);
					if (received <= 0) break;
				}
			}

			closesocket(clientSocket);
		}
	}

	void Master::ServeRequest(uintptr_t socket, const std::string& address)
	{
		std::string buffer;
		char chunk[2048];

		// Read the headers.
		size_t headerEnd = std::string::npos;
		while (headerEnd == std::string::npos)
		{
			const int received = recv(ToSocket(socket), chunk, sizeof(chunk), 0);
			if (received <= 0) return;

			buffer.append(chunk, static_cast<size_t>(received));
			headerEnd = buffer.find("\r\n\r\n");

			if (buffer.size() > 16384) return;  // headers are never that big
		}

		const std::string headers = buffer.substr(0, headerEnd);

		// Parse the request line: METHOD PATH HTTP/1.1
		Request request;
		request.clientAddress = address;

		{
			std::istringstream stream(headers);
			std::string requestLine;
			std::getline(stream, requestLine);
			if (!requestLine.empty() && requestLine.back() == '\r') requestLine.pop_back();

			std::istringstream lineStream(requestLine);
			std::string version;
			lineStream >> request.method >> request.path >> version;

			// Drop the query string, if any.
			const size_t query = request.path.find('?');
			if (query != std::string::npos) request.path = request.path.substr(0, query);
		}

		if (request.method.empty() || request.path.empty())
		{
			WriteResponse(socket, 400, StatusText(400), "{\"error\":\"bad request\"}");
			return;
		}

		// Body.
		std::string contentLengthText;
		size_t contentLength = 0;
		if (GetHeader(headers, "Content-Length", contentLengthText))
		{
			contentLength = static_cast<size_t>(std::strtoul(contentLengthText.c_str(), nullptr, 10));
		}

		if (contentLength > kMasterMaxBodySize)
		{
			WriteResponse(socket, 400, StatusText(400), "{\"error\":\"body too large\"}");
			return;
		}

		// Some HTTP clients (including the one in PowerShell) wait for a
		// "100 Continue" before sending the body.
		std::string expect;
		if (GetHeader(headers, "Expect", expect) && contentLength > 0)
		{
			const char* continueResponse = "HTTP/1.1 100 Continue\r\n\r\n";
			send(ToSocket(socket), continueResponse, static_cast<int>(std::strlen(continueResponse)), 0);
		}

		request.body = buffer.substr(headerEnd + 4);
		while (request.body.size() < contentLength)
		{
			const int received = recv(ToSocket(socket), chunk, sizeof(chunk), 0);
			if (received <= 0) break;
			request.body.append(chunk, static_cast<size_t>(received));
		}

		if (request.body.size() > contentLength)
		{
			request.body.resize(contentLength);
		}

		// Behind a reverse proxy, the real address comes from the header.
		std::string forwarded;
		if (GetHeader(headers, "X-Forwarded-For", forwarded) && !forwarded.empty())
		{
			const size_t comma = forwarded.find(',');
			request.clientAddress = forwarded.substr(0, comma);
			request.forwarded = true;
		}

		MP_LOG_DEBUG("%s %s from %s", request.method.c_str(), request.path.c_str(),
			request.clientAddress.c_str());

		// Routes.
		if (request.path == "/api/v1/register" && request.method == "POST")
		{
			HandleRegister(socket, request);
		}
		else if (request.path == "/api/v1/heartbeat" && request.method == "POST")
		{
			HandleHeartbeat(socket, request);
		}
		else if (request.path == "/api/v1/unregister" && request.method == "POST")
		{
			HandleUnregister(socket, request);
		}
		else if (request.path == "/api/v1/servers" && request.method == "GET")
		{
			HandleList(socket);
		}
		else if (request.path == "/api/v1/health" && request.method == "GET")
		{
			HandleHealth(socket);
		}
		else if (request.path == "/" || request.path == "/api")
		{
			WriteResponse(socket, 200, StatusText(200),
				"{\"name\":\"Spore Multiplayer master\",\"version\":" +
				std::to_string(kMasterProtocolVersion) + ",\"endpoints\":[\"/api/v1/servers\"]}");
		}
		else
		{
			WriteResponse(socket, 404, StatusText(404), "{\"error\":\"not found\"}");
		}
	}

	void Master::HandleRegister(uintptr_t socket, const Request& request)
	{
		if (mSessions.size() >= mConfig.maxServers)
		{
			WriteResponse(socket, 503, StatusText(503), "{\"error\":\"server list is full\"}");
			return;
		}

		Session session;
		session.name = JsonGetString(request.body, "name");
		session.port = static_cast<uint16_t>(JsonGetInt(request.body, "port", 0));
		session.players = JsonGetInt(request.body, "players", 0);
		session.maxPlayers = JsonGetInt(request.body, "maxPlayers", 8);
		session.version = JsonGetInt(request.body, "version", kMasterProtocolVersion);
		session.host = request.clientAddress;
		session.lastSeen = NowMs();
		session.token = GenerateToken();

		// Optional relay information: when present, the session can be reached
		// without a direct connection.
		session.relayHost = JsonGetString(request.body, "relayHost");
		session.relayPort = static_cast<uint16_t>(JsonGetInt(request.body, "relayPort", 0));
		session.relayTicket = JsonGetString(request.body, "ticket");

		if (session.relayHost.empty() || session.relayPort == 0)
		{
			session.relayHost.clear();
			session.relayPort = 0;
			session.relayTicket.clear();
		}

		if (session.port == 0)
		{
			WriteResponse(socket, 400, StatusText(400), "{\"error\":\"port is required\"}");
			return;
		}

		if (session.name.empty())
		{
			session.name = "Spore Co-op";
		}

		// A server that registers twice (for example after a restart) replaces its
		// previous entry, detected by the same host and port.
		for (auto it = mSessions.begin(); it != mSessions.end(); ++it)
		{
			if (it->host == session.host && it->port == session.port)
			{
				mSessions.erase(it);
				break;
			}
		}

		MP_LOG_INFO("Registered '%s' at %s:%u (%d/%d)",
			session.name.c_str(), session.host.c_str(), session.port,
			session.players, session.maxPlayers);

		mSessions.push_back(session);

		const std::string body =
			"{\"ok\":true,\"token\":\"" + JsonEscape(session.token) + "\",\"interval\":" +
			std::to_string(mConfig.heartbeatSeconds) + ",\"publicPort\":" +
			std::to_string(session.port) + "}";

		WriteResponse(socket, 200, StatusText(200), body);
	}

	void Master::HandleHeartbeat(uintptr_t socket, const Request& request)
	{
		const std::string token = JsonGetString(request.body, "token");
		Session* session = FindByToken(token);

		if (session == nullptr)
		{
			WriteResponse(socket, 404, StatusText(404), "{\"error\":\"unknown token\"}");
			return;
		}

		session->players = JsonGetInt(request.body, "players", session->players);
		session->lastSeen = NowMs();

		WriteResponse(socket, 200, StatusText(200), "{\"ok\":true}");
	}

	void Master::HandleUnregister(uintptr_t socket, const Request& request)
	{
		const std::string token = JsonGetString(request.body, "token");

		for (auto it = mSessions.begin(); it != mSessions.end(); ++it)
		{
			if (it->token == token)
			{
				MP_LOG_INFO("Session '%s' unregistered", it->name.c_str());
				mSessions.erase(it);
				WriteResponse(socket, 200, StatusText(200), "{\"ok\":true}");
				return;
			}
		}

		WriteResponse(socket, 404, StatusText(404), "{\"error\":\"unknown token\"}");
	}

	void Master::HandleList(uintptr_t socket)
	{
		std::string body = "{\"count\":" + std::to_string(mSessions.size()) + ",\"servers\":[";

		bool first = true;
		for (const Session& session : mSessions)
		{
			if (!first) body += ",";
			first = false;

			body += "{\"name\":\"" + JsonEscape(session.name) + "\"";
			body += ",\"host\":\"" + JsonEscape(session.host) + "\"";
			body += ",\"port\":" + std::to_string(session.port);
			body += ",\"players\":" + std::to_string(session.players);
			body += ",\"maxPlayers\":" + std::to_string(session.maxPlayers);
			body += ",\"version\":" + std::to_string(session.version);

			// Relay information, so a client behind NAT can still join.
			if (!session.relayHost.empty())
			{
				body += ",\"relayHost\":\"" + JsonEscape(session.relayHost) + "\"";
				body += ",\"relayPort\":" + std::to_string(session.relayPort);
				body += ",\"ticket\":\"" + JsonEscape(session.relayTicket) + "\"";
			}

			body += "}";
		}

		body += "]}";

		WriteResponse(socket, 200, StatusText(200), body);
	}

	void Master::HandleHealth(uintptr_t socket)
	{
		const uint64_t uptime = (NowMs() - mStartTime) / 1000;

		const std::string body =
			"{\"ok\":true,\"servers\":" + std::to_string(mSessions.size()) +
			",\"uptime\":" + std::to_string(uptime) +
			",\"version\":" + std::to_string(kMasterProtocolVersion) + "}";

		WriteResponse(socket, 200, StatusText(200), body);
	}

	void Master::CheckTimeouts()
	{
		const uint64_t now = NowMs();
		const uint64_t limit = static_cast<uint64_t>(mConfig.timeoutSeconds) * 1000;

		for (auto it = mSessions.begin(); it != mSessions.end();)
		{
			if (now - it->lastSeen > limit)
			{
				MP_LOG_INFO("Session '%s' at %s:%u timed out", it->name.c_str(),
					it->host.c_str(), it->port);
				it = mSessions.erase(it);
			}
			else
			{
				++it;
			}
		}
	}

	Master::Session* Master::FindByToken(const std::string& token)
	{
		if (token.empty()) return nullptr;

		for (Session& session : mSessions)
		{
			if (session.token == token) return &session;
		}
		return nullptr;
	}

	std::string Master::GenerateToken()
	{
		// A random-ish token: the counter makes it unique inside this process and
		// the tick count makes it hard to guess.
		++mTokenCounter;

		char token[64];
		std::snprintf(token, sizeof(token), "%08x%08x%08x",
			static_cast<unsigned>(NowMs() & 0xFFFFFFFF),
			static_cast<unsigned>(GetCurrentProcessId()),
			static_cast<unsigned>(mTokenCounter * 2654435761u));

		return token;
	}

	void Master::WriteResponse(uintptr_t socket, int status, const char* statusText,
		const std::string& body, const char* contentType)
	{
		std::ostringstream response;
		response << "HTTP/1.1 " << status << " " << statusText << "\r\n";
		response << "Content-Type: " << contentType << "\r\n";
		response << "Content-Length: " << body.size() << "\r\n";
		response << "Access-Control-Allow-Origin: *\r\n";
		response << "Access-Control-Allow-Headers: Content-Type\r\n";
		response << "Cache-Control: no-store\r\n";
		response << "Connection: close\r\n";
		response << "\r\n";
		response << body;

		const std::string text = response.str();

		size_t offset = 0;
		while (offset < text.size())
		{
			const int sent = send(ToSocket(socket), text.c_str() + offset,
				static_cast<int>(text.size() - offset), 0);
			if (sent <= 0) break;
			offset += static_cast<size_t>(sent);
		}
	}
}
