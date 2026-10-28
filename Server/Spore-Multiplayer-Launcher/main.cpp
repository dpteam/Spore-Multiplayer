/****************************************************************************
* Spore Multiplayer - Launcher
*
* A small local web server that shows the server list and starts the game.
*
* Why a local web server instead of a native window:
*   - no WebView2 runtime to install, no embedded browser to maintain
*   - the page is plain HTML/CSS/JS, so it is easy to change
*   - the same page can be served on a web site later
*
* The launcher binds to 127.0.0.1 on a free port, opens the default browser and
* exposes a tiny JSON API. When the game is launched, the launcher writes the
* connection into SporeMultiplayer.ini and starts Spore through the ModAPI
* Launcher, so the mod connects automatically.
****************************************************************************/
#include "MpLauncherUI.h"
#include "MpLog.h"
#include "MpWebView.h"

#include <winsock2.h>
#include <ws2tcpip.h>
#include <shellapi.h>

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "shell32.lib")

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include <windows.h>

#include "../../Shared/MpMasterProtocol.h"

namespace
{
	struct LauncherConfig
	{
		std::string masterUrl = "play.dartpower.xyz:17401";
		std::string relayUrl = "play.dartpower.xyz:17402";
		/// Path of the ModAPI Launcher Kit (the folder with Spore ModAPI Launcher.exe).
		std::string launcherKitPath;
		/// Path of the client configuration written before launching.
		std::string clientIniPath;
		/// Path of the dedicated server, used when hosting from the launcher.
		std::string serverExe;
		std::string bindAddress = "127.0.0.1";
		uint16_t port = 0;  // 0 = pick a free port
	};

	LauncherConfig g_config;

	// ------------------------------------------------------------------------
	// Configuration
	// ------------------------------------------------------------------------

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

	std::string ReadWholeFile(const std::string& path)
	{
		std::ifstream file(path.c_str());
		if (!file.is_open()) return std::string();

		std::ostringstream stream;
		stream << file.rdbuf();
		return stream.str();
	}

	bool FileExists(const std::string& path)
	{
		const DWORD attributes = GetFileAttributesA(path.c_str());
		return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0;
	}

	/// Finds the ModAPI Launcher Kit through the file the launcher writes.
	std::string FindLauncherKit()
	{
		char buffer[MAX_PATH] = { 0 };
		const DWORD length = GetEnvironmentVariableA("APPDATA", buffer, MAX_PATH);
		if (length == 0) return std::string();

		const std::string pathInfo = std::string(buffer) + "\\Spore ModAPI Launcher\\path.info";
		const std::string path = Trim(ReadWholeFile(pathInfo));

		if (!path.empty() && FileExists(path + "\\Spore ModAPI Launcher.exe"))
		{
			return path;
		}

		return std::string();
	}

	void LoadConfig(const std::string& path)
	{
		std::ifstream file(path.c_str());
		if (!file.is_open()) return;

		std::string line;
		while (std::getline(file, line))
		{
			line = Trim(line);
			if (line.empty() || line[0] == '#' || line[0] == ';' || line[0] == '[') continue;

			const size_t separator = line.find('=');
			if (separator == std::string::npos) continue;

			const std::string key = ToLower(Trim(line.substr(0, separator)));
			const std::string value = Trim(line.substr(separator + 1));

			if (key == "master") g_config.masterUrl = value;
			else if (key == "relay") g_config.relayUrl = value;
			else if (key == "launcher_kit") g_config.launcherKitPath = value;
			else if (key == "client_ini") g_config.clientIniPath = value;
			else if (key == "server_exe") g_config.serverExe = value;
			else if (key == "bind") g_config.bindAddress = value;
			else if (key == "port") g_config.port = static_cast<uint16_t>(std::atoi(value.c_str()));
		}
	}

	// ------------------------------------------------------------------------
	// HTTP helpers
	// ------------------------------------------------------------------------

	/// Splits "http://host:port/path" into its parts.
	bool ParseUrl(const std::string& url, std::string& host, uint16_t& port, std::string& path)
	{
		std::string rest = url;

		const size_t scheme = rest.find("://");
		if (scheme != std::string::npos) rest = rest.substr(scheme + 3);

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
			port = SporeMP::kDefaultMasterPort;
		}

		return !host.empty() && port != 0;
	}

	/// Blocking HTTP GET. Used only for the master proxy, with a short timeout.
	bool HttpGet(const std::string& host, uint16_t port, const std::string& path,
		std::string& body, std::string& error)
	{
		WSADATA wsaData;
		if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
		{
			error = "WSAStartup failed";
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
			error = "Could not create a socket";
			return false;
		}

		DWORD timeout = 5000;
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
			"Connection: close\r\n\r\n";

		send(sock, request.c_str(), static_cast<int>(request.size()), 0);

		std::string raw;
		char buffer[4096];
		while (true)
		{
			const int received = recv(sock, buffer, sizeof(buffer), 0);
			if (received <= 0) break;
			raw.append(buffer, static_cast<size_t>(received));
			if (raw.size() > 262144) break;
		}

		closesocket(sock);

		const size_t bodyStart = raw.find("\r\n\r\n");
		if (bodyStart == std::string::npos)
		{
			error = "Invalid response from the master";
			return false;
		}

		body = raw.substr(bodyStart + 4);
		return true;
	}

	void WriteResponse(uintptr_t socket, int status, const char* statusText,
		const std::string& body, const char* contentType)
	{
		std::ostringstream response;
		response << "HTTP/1.1 " << status << " " << statusText << "\r\n";
		response << "Content-Type: " << contentType << "\r\n";
		response << "Content-Length: " << body.size() << "\r\n";
		response << "Cache-Control: no-store\r\n";
		response << "Connection: close\r\n\r\n";
		response << body;

		const std::string text = response.str();
		send(static_cast<SOCKET>(socket), text.c_str(), static_cast<int>(text.size()), 0);
	}

	// ------------------------------------------------------------------------
	// Game launching
	// ------------------------------------------------------------------------

	/// Writes the connection into SporeMultiplayer.ini so the mod auto-connects.
	bool WriteClientConfig(const std::string& host, uint16_t port,
		const std::string& relay, const std::string& ticket, std::string& error)
	{
		if (g_config.clientIniPath.empty())
		{
			error = "client_ini is not configured in launcher.ini";
			return false;
		}

		// Keep the existing settings and only touch the connection block.
		const std::string existing = ReadWholeFile(g_config.clientIniPath);

		std::ostringstream output;
		output << "; Written by Spore Multiplayer Launcher\n\n";

		if (existing.empty())
		{
			output <<
				"[display]\n"
				"hud=0\n"
				"console=1\n"
				"chat_lines=6\n\n"
				"[sync]\n"
				"send_rate=15\n"
				"show_avatars=1\n"
				"interpolation=1\n"
				"name_tags=1\n\n";
		}
		else
		{
			// Reuse everything except our own [connection] block.
			std::istringstream stream(existing);
			std::string line;
			bool skipping = false;

			while (std::getline(stream, line))
			{
				const std::string trimmed = Trim(line);

				if (!trimmed.empty() && trimmed[0] == '[')
				{
					skipping = (ToLower(trimmed) == "[connection]");
					if (skipping) continue;
				}

				if (skipping) continue;
				if (trimmed.compare(0, 15, "; Written by Spo") == 0) continue;

				output << line << "\n";
			}

			output << "\n";
		}

		output <<
			"[connection]\n"
			"host=" << host << "\n"
			"port=" << port << "\n"
			"auto_connect=1\n";

		if (!relay.empty())
		{
			output << "relay=" << relay << "\n";
			output << "relay_ticket=" << ticket << "\n";
		}

		if (!g_config.masterUrl.empty())
		{
			output << "master=" << g_config.masterUrl << "\n";
		}

		std::ofstream file(g_config.clientIniPath.c_str(), std::ios::trunc);
		if (!file.is_open())
		{
			error = "Could not write " + g_config.clientIniPath;
			return false;
		}

		file << output.str();
		return true;
	}

	bool StartProcess(const std::string& executable, const std::string& arguments,
		const std::string& workingDirectory, std::string& error)
	{
		std::string commandLine = "\"" + executable + "\"";
		if (!arguments.empty()) commandLine += " " + arguments;

		STARTUPINFOA startupInfo;
		std::memset(&startupInfo, 0, sizeof(startupInfo));
		startupInfo.cb = sizeof(startupInfo);

		PROCESS_INFORMATION processInfo;
		std::memset(&processInfo, 0, sizeof(processInfo));

		std::vector<char> buffer(commandLine.begin(), commandLine.end());
		buffer.push_back('\0');

		const BOOL started = CreateProcessA(nullptr, buffer.data(), nullptr, nullptr, FALSE,
			0, nullptr, workingDirectory.empty() ? nullptr : workingDirectory.c_str(),
			&startupInfo, &processInfo);

		if (!started)
		{
			error = "Could not start " + executable + " (error " + std::to_string(GetLastError()) + ")";
			return false;
		}

		CloseHandle(processInfo.hThread);
		CloseHandle(processInfo.hProcess);
		return true;
	}

	bool LaunchGame(const std::string& host, uint16_t port, const std::string& session,
		const std::string& relay, const std::string& ticket, bool hostGame,
		std::string& message, std::string& error)
	{
		if (g_config.launcherKitPath.empty())
		{
			error = "ModAPI Launcher Kit not found. Set launcher_kit in launcher.ini";
			return false;
		}

		// When hosting, the dedicated server runs in the background.
		if (hostGame)
		{
			if (g_config.serverExe.empty() || !FileExists(g_config.serverExe))
			{
				error = "Spore-Multiplayer-Server.exe not found. Set server_exe in launcher.ini";
				return false;
			}

			std::string arguments = "--port " + std::to_string(port) + " --name \"" + session + "\"";

			if (!g_config.masterUrl.empty()) arguments += " --master \"" + g_config.masterUrl + "\"";
			if (!g_config.relayUrl.empty()) arguments += " --relay \"" + g_config.relayUrl + "\"";

			const std::string directory = g_config.serverExe.substr(0, g_config.serverExe.find_last_of("\\/"));

			if (!StartProcess(g_config.serverExe, arguments, directory, error))
			{
				return false;
			}

			// Give the server a moment to bind its ports.
			Sleep(1200);

			message = "Сервер запущен, открываю Spore...";
		}

		if (!WriteClientConfig(host, port, relay, ticket, error))
		{
			return false;
		}

		const std::string launcher = g_config.launcherKitPath + "\\Spore ModAPI Launcher.exe";
		if (!StartProcess(launcher, "-w", g_config.launcherKitPath, error))
		{
			return false;
		}

		if (message.empty()) message = "Spore запускается, подключаюсь к " + host + ":" + std::to_string(port);
		return true;
	}

	// ------------------------------------------------------------------------
	// Request handling
	// ------------------------------------------------------------------------

	/// Minimal JSON field readers (the bodies are small and written by our page).
	std::string JsonString(const std::string& json, const char* key)
	{
		std::string raw;
		if (!SporeMP::JsonGetRaw(json, key, raw)) return std::string();
		return raw;
	}

	int JsonInt(const std::string& json, const char* key, int fallback)
	{
		return SporeMP::JsonGetInt(json, key, fallback);
	}

	void ServeRequest(SOCKET socket)
	{
		std::string buffer;
		char chunk[2048];

		size_t headerEnd = std::string::npos;
		while (headerEnd == std::string::npos)
		{
			const int received = recv(socket, chunk, sizeof(chunk), 0);
			if (received <= 0) return;

			buffer.append(chunk, static_cast<size_t>(received));
			headerEnd = buffer.find("\r\n\r\n");
			if (buffer.size() > 16384) return;
		}

		const std::string headers = buffer.substr(0, headerEnd);

		std::string method;
		std::string path;

		{
			std::istringstream stream(headers);
			std::string requestLine;
			std::getline(stream, requestLine);
			if (!requestLine.empty() && requestLine.back() == '\r') requestLine.pop_back();

			std::istringstream lineStream(requestLine);
			std::string version;
			lineStream >> method >> path >> version;

			const size_t query = path.find('?');
			if (query != std::string::npos) path = path.substr(0, query);
		}

		// Content-Length for POST bodies.
		size_t contentLength = 0;
		{
			const std::string lower = ToLower(headers);
			const size_t position = lower.find("content-length:");
			if (position != std::string::npos)
			{
				contentLength = static_cast<size_t>(std::strtoul(headers.c_str() + position + 15, nullptr, 10));
			}
		}

		std::string body = buffer.substr(headerEnd + 4);
		while (body.size() < contentLength)
		{
			const int received = recv(socket, chunk, sizeof(chunk), 0);
			if (received <= 0) break;
			body.append(chunk, static_cast<size_t>(received));
		}

		if (body.size() > contentLength) body.resize(contentLength);

		// Routes.
		if (path == "/" || path == "/index.html")
		{
			WriteResponse(socket, 200, "OK", SporeMP::GetLauncherHtml(), "text/html; charset=utf-8");
			return;
		}

		if (path == "/api/servers" && method == "GET")
		{
			std::string host;
			uint16_t port = 0;
			std::string masterPath;

			if (!ParseUrl(g_config.masterUrl, host, port, masterPath))
			{
				WriteResponse(socket, 500, "Internal Server Error",
					"{\"error\":\"invalid master url\"}", "application/json");
				return;
			}

			if (masterPath.empty() || masterPath == "/") masterPath = "/api/v1/servers";

			std::string response;
			std::string error;
			if (!HttpGet(host, port, masterPath, response, error))
			{
				WriteResponse(socket, 502, "Bad Gateway",
					"{\"error\":\"" + SporeMP::JsonEscape(error) + "\"}", "application/json");
				return;
			}

			WriteResponse(socket, 200, "OK", response, "application/json");
			return;
		}

		if (path == "/api/status" && method == "GET")
		{
			std::ostringstream response;
			response << "{\"ok\":true"
				<< ",\"master\":\"" << SporeMP::JsonEscape(g_config.masterUrl) << "\""
				<< ",\"relay\":\"" << SporeMP::JsonEscape(g_config.relayUrl) << "\""
				<< ",\"launcherKit\":\"" << SporeMP::JsonEscape(g_config.launcherKitPath) << "\""
				<< "}";

			WriteResponse(socket, 200, "OK", response.str(), "application/json");
			return;
		}

		if (path == "/api/launch" && method == "POST")
		{
			const std::string host = JsonString(body, "host");
			const int port = JsonInt(body, "port", 0);
			const std::string session = JsonString(body, "session");
			const std::string relay = JsonString(body, "relay");
			const std::string ticket = JsonString(body, "ticket");
			const bool hostGame = JsonInt(body, "hostGame", 0) != 0;

			if (host.empty() || port <= 0 || port > 65535)
			{
				WriteResponse(socket, 400, "Bad Request",
					"{\"error\":\"host and port are required\"}", "application/json");
				return;
			}

			std::string message;
			std::string error;

			if (!LaunchGame(host, static_cast<uint16_t>(port), session, relay, ticket, hostGame, message, error))
			{
				WriteResponse(socket, 500, "Internal Server Error",
					"{\"error\":\"" + SporeMP::JsonEscape(error) + "\"}", "application/json");
				return;
			}

			MP_LOG_INFO("Launch: host=%s port=%d hostGame=%d", host.c_str(), port, hostGame ? 1 : 0);

			WriteResponse(socket, 200, "OK",
				"{\"ok\":true,\"message\":\"" + SporeMP::JsonEscape(message) + "\"}", "application/json");
			return;
		}

		WriteResponse(socket, 404, "Not Found", "{\"error\":\"not found\"}", "application/json");
	}
}

int main(int argc, char** argv)
{
	SporeMP::SetLogLevel(SporeMP::LogLevel::Info);

	const std::string directory = GetExecutableDirectory();
	LoadConfig(directory + "launcher.ini");

	// Fill in the defaults that can be discovered.
	bool openBrowser = true;

	if (g_config.launcherKitPath.empty())
	{
		g_config.launcherKitPath = FindLauncherKit();
	}

	if (g_config.clientIniPath.empty() && !g_config.launcherKitPath.empty())
	{
		g_config.clientIniPath = g_config.launcherKitPath + "\\mLibs\\SporeMultiplayer.ini";
	}

	if (g_config.serverExe.empty())
	{
		const std::string candidates[] = {
			directory + "Spore-Multiplayer-Server.exe",
			directory + "..\\Spore-Multiplayer-Server\\bin\\Release\\Spore-Multiplayer-Server.exe",
			directory + "..\\..\\bin\\Release\\Spore-Multiplayer-Server.exe",
		};

		for (const std::string& candidate : candidates)
		{
			char fullPath[MAX_PATH] = { 0 };
			if (GetFullPathNameA(candidate.c_str(), MAX_PATH, fullPath, nullptr) == 0) continue;

			if (FileExists(fullPath))
			{
				g_config.serverExe = fullPath;
				break;
			}
		}
	}

	for (int i = 1; i < argc; ++i)
	{
		const std::string argument = argv[i];

		if (argument == "--help" || argument == "-h")
		{
			std::printf(
				"Spore Multiplayer launcher\n"
				"\n"
				"Usage: Spore-Multiplayer-Launcher.exe [options]\n"
				"\n"
				"  --master <url>    master server (default: %s)\n"
				"  --port <number>   local HTTP port (default: pick a free one)\n"
				"  --no-browser      do not open the browser\n"
				"\n"
				"The launcher serves a small web page on 127.0.0.1 and opens it.\n",
				g_config.masterUrl.c_str());
			return 0;
		}
		else if (argument == "--master" && i + 1 < argc)
		{
			g_config.masterUrl = argv[++i];
		}
		else if (argument == "--port" && i + 1 < argc)
		{
			g_config.port = static_cast<uint16_t>(std::atoi(argv[++i]));
		}
		else if (argument == "--no-browser")
		{
			openBrowser = false;
		}
	}

	// Winsock.
	WSADATA wsaData;
	if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
	{
		MP_LOG_ERROR("WSAStartup failed");
		return 1;
	}

	SOCKET listenSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (listenSocket == INVALID_SOCKET)
	{
		MP_LOG_ERROR("Could not create the listening socket");
		return 1;
	}

	BOOL reuse = TRUE;
	setsockopt(listenSocket, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&reuse), sizeof(reuse));

	sockaddr_in address;
	std::memset(&address, 0, sizeof(address));
	address.sin_family = AF_INET;
	address.sin_port = htons(g_config.port);
	inet_pton(AF_INET, g_config.bindAddress.c_str(), &address.sin_addr);

	if (bind(listenSocket, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == SOCKET_ERROR)
	{
		MP_LOG_ERROR("Could not bind %s:%u (error %d)", g_config.bindAddress.c_str(), g_config.port, WSAGetLastError());
		return 1;
	}

	if (listen(listenSocket, SOMAXCONN) == SOCKET_ERROR)
	{
		MP_LOG_ERROR("Could not listen (error %d)", WSAGetLastError());
		return 1;
	}

	// Read back the port that was actually assigned (0 means "any").
	{
		sockaddr_in bound;
		int length = sizeof(bound);
		getsockname(listenSocket, reinterpret_cast<sockaddr*>(&bound), &length);
		g_config.port = ntohs(bound.sin_port);
	}

	const std::string url = "http://127.0.0.1:" + std::to_string(g_config.port) + "/";

	std::printf("Spore Multiplayer launcher\n");
	std::printf("  UI:     %s\n", url.c_str());
	std::printf("  Master: %s\n", g_config.masterUrl.c_str());
	std::printf("  Kit:    %s\n", g_config.launcherKitPath.empty() ? "(not found)" : g_config.launcherKitPath.c_str());
	std::printf("\n");

	MP_LOG_INFO("Launcher UI at %s", url.c_str());

	// The HTTP server runs on its own thread: the main thread is owned by the
	// WebView2 window (or by the browser fallback).
	std::thread serverThread([listenSocket]()
	{
		while (true)
		{
			SOCKET client = accept(listenSocket, nullptr, nullptr);
			if (client == INVALID_SOCKET)
			{
				// A transient error must not kill the server.
				Sleep(10);
				continue;
			}

			// Accepted sockets inherit the non-blocking mode of the listener on
			// Windows, so switch it back to blocking.
			u_long blocking = 0;
			ioctlsocket(client, FIONBIO, &blocking);

			DWORD timeout = 15000;
			setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeout), sizeof(timeout));

			ServeRequest(client);

			// Close gracefully so the browser receives the whole response.
			shutdown(client, SD_SEND);
			closesocket(client);
		}
	});
	serverThread.detach();

	// Preferred: a real window with WebView2. Fallback: the default browser.
	if (openBrowser)
	{
		std::string error;
		const SporeMP::WebViewStatus status = SporeMP::ShowWebViewWindow(url, error);

		if (status == SporeMP::WebViewStatus::Ok)
		{
			std::printf("Opened the launcher window (WebView2). Close it to stop.\n");
			SporeMP::RunWebViewLoop();
			SporeMP::ShutdownWebViewWindow();
			std::printf("Launcher window closed.\n");
			return 0;
		}

		if (status == SporeMP::WebViewStatus::RuntimeMissing)
		{
			std::printf("WebView2 is not installed, opening the launcher in the browser instead.\n");
			MP_LOG_WARN("WebView2 runtime missing, falling back to the browser");
		}
		else
		{
			std::printf("Could not create the launcher window (%s), opening the browser instead.\n",
				error.c_str());
			MP_LOG_WARN("WebView2 window failed (%s), falling back to the browser", error.c_str());
		}

		ShellExecuteA(nullptr, "open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
	}

	std::printf("Launcher is running. Press Ctrl+C to stop.\n");

	SetConsoleCtrlHandler([](DWORD signal) -> BOOL
	{
		if (signal == CTRL_C_EVENT || signal == CTRL_BREAK_EVENT || signal == CTRL_CLOSE_EVENT)
		{
			std::exit(0);
		}
		return FALSE;
	}, TRUE);

	// Keep the process alive; the server thread does the work.
	while (true)
	{
		Sleep(1000);
	}

	return 0;
}
