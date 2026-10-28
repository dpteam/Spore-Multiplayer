/****************************************************************************
* Spore Multiplayer - Dedicated server
****************************************************************************/
#include "MpServer.h"
#include "MpLog.h"

#include <winsock2.h>
#include <ws2tcpip.h>

#pragma comment(lib, "ws2_32.lib")

#include <algorithm>
#include <cstdio>
#include <cstring>

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

		const char* RejectReasonText(RejectReason reason)
		{
			switch (reason)
			{
			case RejectReason::None: return "none";
			case RejectReason::ProtocolMismatch: return "protocol mismatch";
			case RejectReason::ServerFull: return "server full";
			case RejectReason::InvalidName: return "invalid name";
			case RejectReason::Timeout: return "timeout";
			case RejectReason::ShuttingDown: return "shutting down";
			case RejectReason::Kicked: return "kicked";
			case RejectReason::BadMessage: return "bad message";
			default: return "unknown";
			}
		}
	}

	Server::Server(const ServerConfig& config)
		: mConfig(config)
	{
	}

	Server::~Server()
	{
		if (mListenSocket != kInvalidSocket)
		{
			closesocket(ToSocket(mListenSocket));
			mListenSocket = kInvalidSocket;
		}
		WSACleanup();
	}

	bool Server::Start()
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

		StartDiscovery();

		MP_LOG_INFO("%s server listening on %s:%d", kProtocolName, mConfig.bindAddress.c_str(), mConfig.port);
		MP_LOG_INFO("Session '%s', max %d players, %d ticks/s, protocol v%u",
			mConfig.sessionName.c_str(), mConfig.maxPlayers, mConfig.tickRate, kProtocolVersion);
		return true;
	}

	void Server::RequestStop()
	{
		mStopRequested.store(true);
	}

	void Server::QueueCommand(const std::string& command)
	{
		std::lock_guard<std::mutex> lock(mCommandMutex);
		mCommands.push_back(command);
	}

	void Server::Run()
	{
		uint64_t lastTick = NowMs();
		uint64_t lastBeacon = 0;

		while (mRunning && !mStopRequested.load())
		{
			fd_set readSet;
			FD_ZERO(&readSet);
			FD_SET(ToSocket(mListenSocket), &readSet);

			for (const auto& client : mClients)
			{
				if (client->socket != kInvalidSocket)
				{
					FD_SET(ToSocket(client->socket), &readSet);
				}
			}

			timeval timeout;
			timeout.tv_sec = 0;
			timeout.tv_usec = 2000;  // 2 ms

			const int result = select(0, &readSet, nullptr, nullptr, &timeout);
			if (result == SOCKET_ERROR)
			{
				MP_LOG_ERROR("select() failed (error %d)", WSAGetLastError());
				break;
			}

			if (result > 0)
			{
				if (FD_ISSET(ToSocket(mListenSocket), &readSet))
				{
					AcceptNewClients();
				}

				// Note: AcceptNewClients() only appends clients, so iterating by index is safe.
				for (size_t i = 0; i < mClients.size(); ++i)
				{
					Client& client = *mClients[i];
					if (client.socket == kInvalidSocket) continue;
					if (FD_ISSET(ToSocket(client.socket), &readSet))
					{
						ReadClient(client);
					}
				}
			}

			for (auto& client : mClients)
			{
				if (client->socket != kInvalidSocket)
				{
					FlushClient(*client);
				}
			}

			const uint64_t now = NowMs();
			const uint64_t tickInterval = 1000 / static_cast<uint64_t>((std::max)(1, mConfig.tickRate));
			if (now - lastTick >= tickInterval)
			{
				lastTick = now;
				BroadcastStates();
			}

			if (now - lastBeacon >= static_cast<uint64_t>(kDiscoveryIntervalMs))
			{
				lastBeacon = now;
				BroadcastBeacon();
			}

			CheckTimeouts();
			ProcessCommands();
			RemoveClosedClients();
		}

		// Graceful shutdown: tell everybody why the connection is going away.
		for (auto& client : mClients)
		{
			if (client->socket != kInvalidSocket)
			{
				CloseClient(*client, RejectReason::ShuttingDown, "Server is shutting down", true);
				FlushClient(*client);
			}
		}
		RemoveClosedClients();

		if (mListenSocket != kInvalidSocket)
		{
			closesocket(ToSocket(mListenSocket));
			mListenSocket = kInvalidSocket;
		}

		if (mDiscoverySocket != kInvalidSocket)
		{
			closesocket(ToSocket(mDiscoverySocket));
			mDiscoverySocket = kInvalidSocket;
		}

		mRunning = false;
		MP_LOG_INFO("Server stopped");
	}

	// ------------------------------------------------------------------------
	// Networking
	// ------------------------------------------------------------------------

	bool Server::StartDiscovery()
	{
		SOCKET sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
		if (sock == INVALID_SOCKET)
		{
			MP_LOG_WARN("Could not create the discovery socket, LAN discovery is disabled");
			return false;
		}

		BOOL broadcast = TRUE;
		setsockopt(sock, SOL_SOCKET, SO_BROADCAST, reinterpret_cast<const char*>(&broadcast), sizeof(broadcast));

		BOOL reuse = TRUE;
		setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&reuse), sizeof(reuse));

		// The beacons are sent from the same port as the TCP server, so the clients
		// only have to know one port number.
		sockaddr_in address;
		std::memset(&address, 0, sizeof(address));
		address.sin_family = AF_INET;
		address.sin_port = htons(mConfig.port);
		address.sin_addr.s_addr = htonl(INADDR_ANY);

		if (bind(sock, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == SOCKET_ERROR)
		{
			MP_LOG_WARN("Could not bind the discovery port %d (error %d), LAN discovery is disabled",
				mConfig.port, WSAGetLastError());
			closesocket(sock);
			return false;
		}

		mDiscoverySocket = static_cast<uintptr_t>(sock);
		return true;
	}

	void Server::BroadcastBeacon()
	{
		if (mDiscoverySocket == kInvalidSocket) return;

		MsgDiscoveryBeacon beacon;
		std::memset(&beacon, 0, sizeof(beacon));
		beacon.magic = kDiscoveryMagic;
		beacon.protocolVersion = kProtocolVersion;
		beacon.port = mConfig.port;
		beacon.players = static_cast<uint16_t>(ReadyClientCount());
		beacon.maxPlayers = static_cast<uint16_t>(mConfig.maxPlayers);
		CopyString(beacon.sessionName, sizeof(beacon.sessionName), mConfig.sessionName.c_str());

		sockaddr_in address;
		std::memset(&address, 0, sizeof(address));
		address.sin_family = AF_INET;
		address.sin_port = htons(mConfig.port);
		address.sin_addr.s_addr = htonl(INADDR_BROADCAST);

		// Send to the global broadcast address and to the local subnet broadcast,
		// which is more reliable on machines with several network adapters.
		const int sent = sendto(ToSocket(mDiscoverySocket), reinterpret_cast<const char*>(&beacon), sizeof(beacon), 0,
			reinterpret_cast<sockaddr*>(&address), sizeof(address));

		if (sent == SOCKET_ERROR)
		{
			static bool reported = false;
			if (!reported)
			{
				reported = true;
				MP_LOG_WARN("Could not broadcast the discovery beacon (error %d)", WSAGetLastError());
			}
		}
	}
	void Server::AcceptNewClients()
	{
		while (true)
		{
			sockaddr_in address;
			int addressLength = sizeof(address);
			SOCKET newSocket = accept(ToSocket(mListenSocket), reinterpret_cast<sockaddr*>(&address), &addressLength);
			if (newSocket == INVALID_SOCKET)
			{
				const int error = WSAGetLastError();
				if (error != WSAEWOULDBLOCK)
				{
					MP_LOG_WARN("accept() failed (error %d)", error);
				}
				break;
			}

			u_long nonBlocking = 1;
			ioctlsocket(newSocket, FIONBIO, &nonBlocking);

			BOOL noDelay = TRUE;
			setsockopt(newSocket, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&noDelay), sizeof(noDelay));

			char ip[64] = { 0 };
			inet_ntop(AF_INET, &address.sin_addr, ip, sizeof(ip));

			auto client = std::make_unique<Client>();
			client->socket = static_cast<uintptr_t>(newSocket);
			client->address = ip;
			client->connectedAt = NowMs();
			client->lastRecvAt = client->connectedAt;

			MP_LOG_INFO("Connection from %s", ip);

			if (ReadyClientCount() >= static_cast<size_t>(mConfig.maxPlayers))
			{
				Client& ref = *client;
				mClients.push_back(std::move(client));
				CloseClient(ref, RejectReason::ServerFull, "The session is full", false);
				continue;
			}

			mClients.push_back(std::move(client));
		}
	}

	void Server::ReadClient(Client& client)
	{
		char buffer[4096];
		const int received = recv(ToSocket(client.socket), buffer, sizeof(buffer), 0);

		if (received == 0)
		{
			CloseClient(client, RejectReason::None, "Connection closed by the client", true);
			return;
		}

		if (received == SOCKET_ERROR)
		{
			const int error = WSAGetLastError();
			if (error != WSAEWOULDBLOCK)
			{
				MP_LOG_WARN("recv() from %s failed (error %d)", client.address.c_str(), error);
				CloseClient(client, RejectReason::None, "Socket error", true);
			}
			return;
		}

		client.lastRecvAt = NowMs();
		client.inBuffer.insert(client.inBuffer.end(), buffer, buffer + received);

		while (true)
		{
			const size_t available = client.inBuffer.size() - client.inOffset;
			if (available < kHeaderSize) break;

			uint16_t type = 0;
			uint16_t size = 0;
			std::memcpy(&type, client.inBuffer.data() + client.inOffset, sizeof(type));
			std::memcpy(&size, client.inBuffer.data() + client.inOffset + sizeof(type), sizeof(size));

			if (size > kMaxPayloadSize)
			{
				MP_LOG_WARN("Client %s sent an oversized message (%u bytes)", client.address.c_str(), size);
				CloseClient(client, RejectReason::BadMessage, "Message too large", true);
				break;
			}

			if (available < kHeaderSize + size) break;

			HandleFrame(client, type, client.inBuffer.data() + client.inOffset + kHeaderSize, size);
			client.inOffset += kHeaderSize + size;

			if (client.socket == kInvalidSocket || client.closing) break;
		}

		if (client.inOffset > 0)
		{
			client.inBuffer.erase(client.inBuffer.begin(), client.inBuffer.begin() + client.inOffset);
			client.inOffset = 0;
		}
	}

	void Server::FlushClient(Client& client)
	{
		if (client.socket == kInvalidSocket) return;

		while (!client.outBuffer.empty())
		{
			const int sent = send(ToSocket(client.socket), reinterpret_cast<const char*>(client.outBuffer.data()),
				static_cast<int>(client.outBuffer.size()), 0);

			if (sent == SOCKET_ERROR)
			{
				const int error = WSAGetLastError();
				if (error == WSAEWOULDBLOCK) return;

				MP_LOG_WARN("send() to %s failed (error %d)", client.address.c_str(), error);
				closesocket(ToSocket(client.socket));
				client.socket = kInvalidSocket;
				return;
			}

			client.outBuffer.erase(client.outBuffer.begin(), client.outBuffer.begin() + sent);
		}

		if (client.closing)
		{
			closesocket(ToSocket(client.socket));
			client.socket = kInvalidSocket;
		}
	}

	void Server::CloseClient(Client& client, RejectReason reason, const char* message, bool notify)
	{
		if (client.socket == kInvalidSocket) return;
		if (client.closing && reason == RejectReason::None) return;

		if (notify && client.handshakeDone)
		{
			MsgPlayerLeft left;
			std::memset(&left, 0, sizeof(left));
			left.id = client.id;
			left.reason = reason;
			Broadcast(MsgType::PlayerLeft, &left, sizeof(left), client.id);

			// The avatar asset of a player that left is no longer needed.
			for (auto it = mAssets.begin(); it != mAssets.end();)
			{
				if (it->ownerId == client.id)
				{
					it = mAssets.erase(it);
				}
				else
				{
					++it;
				}
			}

			MP_LOG_INFO("Player %u ('%s') disconnected (%s)", client.id, client.info.name, RejectReasonText(reason));
		}
		else if (!client.handshakeDone)
		{
			MP_LOG_INFO("Connection from %s closed before handshake (%s)", client.address.c_str(), RejectReasonText(reason));
		}

		if (reason != RejectReason::None)
		{
			if (client.handshakeDone)
			{
				MsgKick kick;
				std::memset(&kick, 0, sizeof(kick));
				kick.reason = reason;
				CopyString(kick.message, sizeof(kick.message), message);
				Send(client, MsgType::Kick, &kick, sizeof(kick));
			}
			else
			{
				MsgReject reject;
				std::memset(&reject, 0, sizeof(reject));
				reject.reason = reason;
				CopyString(reject.message, sizeof(reject.message), message);
				Send(client, MsgType::Reject, &reject, sizeof(reject));
			}
		}

		client.handshakeDone = false;
		client.closing = true;
		client.closingSince = NowMs();
		client.outBuffer.shrink_to_fit();

		FlushClient(client);
	}

	void Server::RemoveClosedClients()
	{
		mClients.erase(
			std::remove_if(mClients.begin(), mClients.end(),
				[](const std::unique_ptr<Client>& client) { return client->socket == kInvalidSocket; }),
			mClients.end());
	}

	// ------------------------------------------------------------------------
	// Message handlers
	// ------------------------------------------------------------------------

	void Server::HandleFrame(Client& client, uint16_t type, const uint8_t* payload, size_t size)
	{
		if (!client.handshakeDone && type != static_cast<uint16_t>(MsgType::Hello))
		{
			CloseClient(client, RejectReason::BadMessage, "Expected a Hello message", false);
			return;
		}

		switch (static_cast<MsgType>(type))
		{
		case MsgType::Hello:
			HandleHello(client, payload, size);
			break;
		case MsgType::LocalState:
			HandleLocalState(client, payload, size);
			break;
		case MsgType::Chat:
			HandleChat(client, payload, size);
			break;
		case MsgType::SetName:
			HandleSetName(client, payload, size);
			break;
		case MsgType::Ping:
			HandlePing(client, payload, size);
			break;
		case MsgType::AssetInfo:
			HandleAssetInfo(client, payload, size);
			break;
		case MsgType::AssetChunk:
			HandleAssetChunk(client, payload, size);
			break;
		case MsgType::AssetDone:
			HandleAssetDone(client, payload, size);
			break;
		default:
			MP_LOG_DEBUG("Ignoring unexpected message type %u from %s", type, client.address.c_str());
			break;
		}
	}

	void Server::HandleHello(Client& client, const uint8_t* payload, size_t size)
	{
		if (client.handshakeDone)
		{
			CloseClient(client, RejectReason::BadMessage, "Duplicate Hello message", true);
			return;
		}

		if (size < sizeof(MsgHello))
		{
			CloseClient(client, RejectReason::BadMessage, "Malformed Hello message", false);
			return;
		}

		MsgHello hello;
		std::memcpy(&hello, payload, sizeof(hello));

		if (hello.protocolVersion != kProtocolVersion)
		{
			char message[kMaxReasonTextLength];
			std::snprintf(message, sizeof(message), "Protocol mismatch: server v%u, client v%u",
				kProtocolVersion, hello.protocolVersion);
			CloseClient(client, RejectReason::ProtocolMismatch, message, false);
			return;
		}

		char name[kMaxNameLength];
		SanitizeText(hello.name, name, sizeof(name), nullptr);
		if (!IsNameValid(name))
		{
			CloseClient(client, RejectReason::InvalidName, "Invalid player name", false);
			return;
		}

		if (ReadyClientCount() >= static_cast<size_t>(mConfig.maxPlayers))
		{
			CloseClient(client, RejectReason::ServerFull, "The session is full", false);
			return;
		}

		client.id = NextPlayerId();
		client.handshakeDone = true;

		std::memset(&client.info, 0, sizeof(client.info));
		client.info.id = client.id;
		client.info.stage = hello.stage;
		client.info.shipModel = hello.shipModel;
		MakeUniqueName(name, client.info.name, sizeof(client.info.name), client.id);

		client.state.id = client.id;
		client.state.stage = static_cast<uint8_t>(hello.stage);
		client.hasState = false;

		MP_LOG_INFO("Player %u '%s' joined from %s (stage %u, protocol v%u, client build %u)",
			client.id, client.info.name, client.address.c_str(),
			static_cast<unsigned>(hello.stage), hello.protocolVersion, hello.clientVersion);

		MsgWelcome welcome;
		std::memset(&welcome, 0, sizeof(welcome));
		welcome.playerId = client.id;
		welcome.tickRate = static_cast<uint16_t>(mConfig.tickRate);
		welcome.maxPlayers = static_cast<uint16_t>(mConfig.maxPlayers);
		CopyString(welcome.sessionName, sizeof(welcome.sessionName), mConfig.sessionName.c_str());
		CopyString(welcome.assignedName, sizeof(welcome.assignedName), client.info.name);
		Send(client, MsgType::Welcome, &welcome, sizeof(welcome));

		SendPlayerList(client);
		SendCachedAssets(client);

		MsgPlayerJoined joined;
		std::memset(&joined, 0, sizeof(joined));
		joined.player = client.info;
		Broadcast(MsgType::PlayerJoined, &joined, sizeof(joined), client.id);
	}

	void Server::HandleLocalState(Client& client, const uint8_t* payload, size_t size)
	{
		if (size < sizeof(MsgLocalState)) return;

		MsgLocalState state;
		std::memcpy(&state, payload, sizeof(state));

		const bool stageChanged = client.state.stage != static_cast<uint8_t>(state.stage);

		client.state.position = state.position;
		client.state.orientation = state.orientation;
		client.state.stage = static_cast<uint8_t>(state.stage);
		client.state.flags = state.flags;
		client.state.systemId = state.systemId;
		client.hasState = true;

		if (stageChanged)
		{
			client.info.stage = state.stage;
			MsgPlayerUpdated updated;
			std::memset(&updated, 0, sizeof(updated));
			updated.player = client.info;
			Broadcast(MsgType::PlayerUpdated, &updated, sizeof(updated), client.id);
		}
	}

	void Server::HandleChat(Client& client, const uint8_t* payload, size_t size)
	{
		if (size < sizeof(MsgChat)) return;

		MsgChat chat;
		std::memcpy(&chat, payload, sizeof(chat));

		char text[kMaxChatLength];
		SanitizeText(chat.text, text, sizeof(text), "");
		if (text[0] == '\0') return;

		MP_LOG_INFO("[chat] %s: %s", client.info.name, text);
		BroadcastChat(client.id, text);
	}

	void Server::HandleSetName(Client& client, const uint8_t* payload, size_t size)
	{
		if (size < sizeof(MsgSetName)) return;

		MsgSetName request;
		std::memcpy(&request, payload, sizeof(request));

		char name[kMaxNameLength];
		SanitizeText(request.name, name, sizeof(name), "");
		if (!IsNameValid(name))
		{
			SendSystemChat(client, "That name is not valid");
			return;
		}

		char unique[kMaxNameLength];
		MakeUniqueName(name, unique, sizeof(unique), client.id);

		char oldName[kMaxNameLength];
		CopyString(oldName, sizeof(oldName), client.info.name);
		CopyString(client.info.name, sizeof(client.info.name), unique);

		MsgPlayerUpdated updated;
		std::memset(&updated, 0, sizeof(updated));
		updated.player = client.info;
		Broadcast(MsgType::PlayerUpdated, &updated, sizeof(updated));

		char text[kMaxChatLength];
		std::snprintf(text, sizeof(text), "%s is now known as %s", oldName, client.info.name);
		BroadcastChat(0, text);

		MP_LOG_INFO("Player %u renamed from '%s' to '%s'", client.id, oldName, client.info.name);
	}

	void Server::HandlePing(Client& client, const uint8_t* payload, size_t size)
	{
		if (size < sizeof(MsgPing)) return;

		MsgPing ping;
		std::memcpy(&ping, payload, sizeof(ping));

		MsgPong pong;
		pong.clientTime = ping.clientTime;
		pong.serverTime = static_cast<uint32_t>(NowMs() - mStartTime);
		Send(client, MsgType::Pong, &pong, sizeof(pong));
	}

	void Server::HandleAssetInfo(Client& client, const uint8_t* payload, size_t size)
	{
		if (size < sizeof(MsgAssetInfo)) return;

		MsgAssetInfo info;
		std::memcpy(&info, payload, sizeof(info));

		if (info.totalSize == 0 || info.totalSize > kMaxAssetSize)
		{
			MP_LOG_WARN("Player %u sent an avatar asset with an invalid size (%u)", client.id, info.totalSize);
			return;
		}

		client.assetId = info.assetId;
		client.assetSize = info.totalSize;
		client.assetKey = info.key;
		client.assetBuffer.clear();
		client.assetBuffer.reserve(info.totalSize);
		client.assetInProgress = true;

		MP_LOG_DEBUG("Player %u is sending an avatar asset (%u bytes, key %08x)",
			client.id, info.totalSize, info.key.instance);
	}

	void Server::HandleAssetChunk(Client& client, const uint8_t* payload, size_t size)
	{
		if (!client.assetInProgress) return;
		if (size < sizeof(MsgAssetChunk)) return;

		MsgAssetChunk chunk;
		std::memcpy(&chunk, payload, sizeof(chunk));

		const size_t dataSize = size - sizeof(MsgAssetChunk);
		if (chunk.size != dataSize) return;
		if (chunk.offset + dataSize > client.assetSize) return;
		if (chunk.assetId != client.assetId) return;

		if (client.assetBuffer.size() < chunk.offset + dataSize)
		{
			client.assetBuffer.resize(chunk.offset + dataSize);
		}

		std::memcpy(client.assetBuffer.data() + chunk.offset, payload + sizeof(MsgAssetChunk), dataSize);
	}

	void Server::HandleAssetDone(Client& client, const uint8_t* payload, size_t size)
	{
		if (!client.assetInProgress) return;
		if (size < sizeof(MsgAssetDone)) return;

		MsgAssetDone done;
		std::memcpy(&done, payload, sizeof(done));

		if (done.assetId != client.assetId)
		{
			client.assetInProgress = false;
			client.assetBuffer.clear();
			return;
		}

		client.assetInProgress = false;

		if (!done.success || client.assetBuffer.size() != client.assetSize)
		{
			MP_LOG_WARN("Player %u failed to send its avatar asset (%zu/%u bytes)",
				client.id, client.assetBuffer.size(), client.assetSize);
			client.assetBuffer.clear();
			return;
		}

		StoreAsset(client);

		// The key of the creation is now known: remember it in the player info so
		// the other clients can resolve it, and tell them about the change.
		if (ResKeyIsValid(client.assetKey) &&
			(client.info.shipModel.instance != client.assetKey.instance ||
			 client.info.shipModel.type != client.assetKey.type ||
			 client.info.shipModel.group != client.assetKey.group))
		{
			client.info.shipModel = client.assetKey;

			MsgPlayerUpdated updated;
			std::memset(&updated, 0, sizeof(updated));
			updated.player = client.info;
			Broadcast(MsgType::PlayerUpdated, &updated, sizeof(updated), client.id);
		}

		// Forward the asset to the players that are already connected.
		MsgAssetInfo info;
		std::memset(&info, 0, sizeof(info));
		info.assetId = client.assetId;
		info.totalSize = client.assetSize;
		info.ownerId = client.id;
		info.key = client.assetKey;
		Broadcast(MsgType::AssetInfo, &info, sizeof(info), client.id);

		for (size_t offset = 0; offset < client.assetBuffer.size(); offset += kAssetChunkSize)
		{
			const size_t count = (std::min)(kAssetChunkSize, client.assetBuffer.size() - offset);

			MsgAssetChunk chunk;
			std::memset(&chunk, 0, sizeof(chunk));
			chunk.assetId = client.assetId;
			chunk.offset = static_cast<uint32_t>(offset);
			chunk.size = static_cast<uint16_t>(count);

			std::vector<uint8_t> frame(sizeof(chunk) + count);
			std::memcpy(frame.data(), &chunk, sizeof(chunk));
			std::memcpy(frame.data() + sizeof(chunk), client.assetBuffer.data() + offset, count);

			Broadcast(MsgType::AssetChunk, frame.data(), frame.size(), client.id);
		}

		MsgAssetDone finish;
		std::memset(&finish, 0, sizeof(finish));
		finish.assetId = client.assetId;
		finish.success = 1;
		Broadcast(MsgType::AssetDone, &finish, sizeof(finish), client.id);

		MP_LOG_INFO("Player %u ('%s') shared its avatar asset (%u bytes)",
			client.id, client.info.name, client.assetSize);

		client.assetBuffer.clear();
		client.assetBuffer.shrink_to_fit();
	}

	// ------------------------------------------------------------------------
	// Session
	// ------------------------------------------------------------------------

	void Server::BroadcastStates()	{
		for (auto& receiver : mClients)
		{
			if (receiver->socket == kInvalidSocket || !receiver->handshakeDone) continue;

			std::vector<uint8_t> payload(PlayerStatesPayloadSize(mConfig.maxPlayers));
			auto* header = reinterpret_cast<MsgPlayerStatesHeader*>(payload.data());
			header->count = 0;

			StateEntry* entries = PlayerStatesEntries(header);

			for (const auto& other : mClients)
			{
				if (other.get() == receiver.get()) continue;
				if (other->socket == kInvalidSocket || !other->handshakeDone) continue;
				if (!other->hasState) continue;

				entries[header->count++] = other->state;
			}

			if (header->count == 0) continue;

			Send(*receiver, MsgType::PlayerStates, payload.data(), PlayerStatesPayloadSize(header->count));
		}
	}

	void Server::CheckTimeouts()
	{
		const uint64_t now = NowMs();

		for (auto& client : mClients)
		{
			if (client->socket == kInvalidSocket) continue;

			if (client->closing)
			{
				if (now - client->closingSince > 5000)
				{
					closesocket(ToSocket(client->socket));
					client->socket = kInvalidSocket;
				}
				continue;
			}

			if (!client->handshakeDone)
			{
				if (now - client->connectedAt > static_cast<uint64_t>(mConfig.handshakeTimeoutSeconds) * 1000)
				{
					CloseClient(*client, RejectReason::Timeout, "Handshake timeout", false);
				}
				continue;
			}

			if (now - client->lastRecvAt > static_cast<uint64_t>(mConfig.timeoutSeconds) * 1000)
			{
				MP_LOG_INFO("Player %u ('%s') timed out", client->id, client->info.name);
				CloseClient(*client, RejectReason::Timeout, "Connection timed out", true);
			}
		}
	}

	void Server::ProcessCommands()
	{
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
				return;
			}

			if (command == "help")
			{
				std::printf(
					"Commands:\n"
					"  list            - show the players in the session\n"
					"  say <text>      - send a message to every player\n"
					"  kick <id> [why] - disconnect a player\n"
					"  status          - show uptime and connection count\n"
					"  stop            - shut the server down\n");
				continue;
			}

			if (command == "list" || command == "players")
			{
				std::printf("Players (%zu/%d):\n", ReadyClientCount(), mConfig.maxPlayers);
				for (const auto& client : mClients)
				{
					if (client->socket == kInvalidSocket || !client->handshakeDone) continue;
					std::printf("  #%u  %-20s  %s  stage %u\n", client->id, client->info.name,
						client->address.c_str(), static_cast<unsigned>(client->info.stage));
				}
				continue;
			}

			if (command == "status")
			{
				const uint64_t uptime = (NowMs() - mStartTime) / 1000;
				std::printf("Session '%s' | %zu/%d players | up %llu s | tick %d Hz\n",
					mConfig.sessionName.c_str(), ReadyClientCount(), mConfig.maxPlayers,
					static_cast<unsigned long long>(uptime), mConfig.tickRate);
				continue;
			}

			if (command.compare(0, 5, "kick ") == 0)
			{
				const char* rest = command.c_str() + 5;
				while (*rest == ' ') ++rest;

				char* end = nullptr;
				const unsigned long id = std::strtoul(rest, &end, 10);
				std::string reason = "Kicked by the server";
				if (end != nullptr && *end != '\0')
				{
					while (*end == ' ') ++end;
					if (*end != '\0') reason = end;
				}

				bool found = false;
				for (auto& client : mClients)
				{
					if (client->socket == kInvalidSocket || !client->handshakeDone) continue;
					if (client->id != id) continue;

					MP_LOG_INFO("Kicking player %u ('%s'): %s", client->id, client->info.name, reason.c_str());
					CloseClient(*client, RejectReason::Kicked, reason.c_str(), true);
					found = true;
					break;
				}

				if (!found)
				{
					std::printf("No player with id %lu\n", id);
				}
				continue;
			}

			if (command.compare(0, 4, "say ") == 0)
			{
				std::string text = command.substr(4);
				BroadcastChat(0, text.c_str());
				MP_LOG_INFO("[chat] server: %s", text.c_str());
				continue;
			}

			if (!command.empty())
			{
				std::printf("Unknown command '%s'. Type 'help' for the list of commands.\n", command.c_str());
			}
		}
	}

	// ------------------------------------------------------------------------
	// Helpers
	// ------------------------------------------------------------------------

	void Server::Send(Client& client, MsgType type, const void* payload, size_t size)
	{
		if (client.socket == kInvalidSocket) return;
		if (size > kMaxPayloadSize) return;

		const size_t offset = client.outBuffer.size();
		client.outBuffer.resize(offset + kHeaderSize + size);

		uint16_t messageType = static_cast<uint16_t>(type);
		uint16_t payloadSize = static_cast<uint16_t>(size);
		std::memcpy(client.outBuffer.data() + offset, &messageType, sizeof(messageType));
		std::memcpy(client.outBuffer.data() + offset + sizeof(messageType), &payloadSize, sizeof(payloadSize));
		if (size > 0)
		{
			std::memcpy(client.outBuffer.data() + offset + kHeaderSize, payload, size);
		}
	}

	void Server::Broadcast(MsgType type, const void* payload, size_t size, uint32_t exceptId)
	{
		for (auto& client : mClients)
		{
			if (client->socket == kInvalidSocket || !client->handshakeDone) continue;
			if (client->id == exceptId) continue;
			Send(*client, type, payload, size);
		}
	}

	void Server::BroadcastChat(uint32_t senderId, const char* text)
	{
		MsgChat chat;
		std::memset(&chat, 0, sizeof(chat));
		chat.senderId = senderId;
		CopyString(chat.text, sizeof(chat.text), text);
		Broadcast(MsgType::Chat, &chat, sizeof(chat));
	}

	void Server::SendPlayerList(Client& client)
	{
		std::vector<uint8_t> payload(PlayerListPayloadSize(mConfig.maxPlayers));
		auto* header = reinterpret_cast<MsgPlayerListHeader*>(payload.data());
		header->count = 0;

		PlayerInfo* entries = PlayerListEntries(header);
		for (const auto& other : mClients)
		{
			if (other->socket == kInvalidSocket || !other->handshakeDone) continue;
			if (other->id == client.id) continue;
			entries[header->count++] = other->info;
		}

		Send(client, MsgType::PlayerList, payload.data(), PlayerListPayloadSize(header->count));
	}

	Server::CachedAsset* Server::FindAsset(uint32_t assetId)
	{
		for (CachedAsset& asset : mAssets)
		{
			if (asset.assetId == assetId) return &asset;
		}
		return nullptr;
	}

	void Server::StoreAsset(Client& client)
	{
		// Replace the previous asset of this player, if any.
		for (auto it = mAssets.begin(); it != mAssets.end(); ++it)
		{
			if (it->ownerId == client.id)
			{
				mAssets.erase(it);
				break;
			}
		}

		CachedAsset asset;
		asset.assetId = client.assetId;
		asset.ownerId = client.id;
		asset.key = client.assetKey;
		asset.data = client.assetBuffer;
		mAssets.push_back(std::move(asset));
	}

	void Server::SendCachedAssets(Client& client)
	{
		for (const CachedAsset& asset : mAssets)
		{
			if (asset.ownerId == client.id) continue;
			if (asset.data.empty()) continue;

			MsgAssetInfo info;
			std::memset(&info, 0, sizeof(info));
			info.assetId = asset.assetId;
			info.totalSize = static_cast<uint32_t>(asset.data.size());
			info.ownerId = asset.ownerId;
			info.key = asset.key;
			Send(client, MsgType::AssetInfo, &info, sizeof(info));

			for (size_t offset = 0; offset < asset.data.size(); offset += kAssetChunkSize)
			{
				const size_t count = (std::min)(kAssetChunkSize, asset.data.size() - offset);

				MsgAssetChunk chunk;
				std::memset(&chunk, 0, sizeof(chunk));
				chunk.assetId = asset.assetId;
				chunk.offset = static_cast<uint32_t>(offset);
				chunk.size = static_cast<uint16_t>(count);

				std::vector<uint8_t> frame(sizeof(chunk) + count);
				std::memcpy(frame.data(), &chunk, sizeof(chunk));
				std::memcpy(frame.data() + sizeof(chunk), asset.data.data() + offset, count);

				Send(client, MsgType::AssetChunk, frame.data(), frame.size());
			}

			MsgAssetDone done;
			std::memset(&done, 0, sizeof(done));
			done.assetId = asset.assetId;
			done.success = 1;
			Send(client, MsgType::AssetDone, &done, sizeof(done));
		}
	}

	void Server::SendSystemChat(Client& client, const char* text)	{
		MsgChat chat;
		std::memset(&chat, 0, sizeof(chat));
		chat.senderId = 0;
		CopyString(chat.text, sizeof(chat.text), text);
		Send(client, MsgType::Chat, &chat, sizeof(chat));
	}

	void Server::MakeUniqueName(const char* requested, char* dst, size_t dstSize, uint32_t exceptId)
	{
		CopyString(dst, dstSize, requested);
		if (mConfig.allowDuplicateNames) return;
		if (!IsNameInUse(dst, exceptId)) return;

		for (int suffix = 2; suffix < 1000; ++suffix)
		{
			char candidate[kMaxNameLength];
			std::snprintf(candidate, sizeof(candidate), "%.*s (%d)", kMaxNameLength - 8, requested, suffix);
			if (!IsNameInUse(candidate, exceptId))
			{
				CopyString(dst, dstSize, candidate);
				return;
			}
		}
	}

	bool Server::IsNameInUse(const char* name, uint32_t exceptId) const
	{
		for (const auto& client : mClients)
		{
			if (client->socket == kInvalidSocket || !client->handshakeDone) continue;
			if (client->id == exceptId) continue;
			if (std::strcmp(client->info.name, name) == 0) return true;
		}
		return false;
	}

	bool Server::IsNameValid(const char* name) const
	{
		if (name == nullptr || name[0] == '\0') return false;

		const size_t length = std::strlen(name);
		if (length == 0 || length >= static_cast<size_t>(kMaxNameLength)) return false;

		for (size_t i = 0; i < length; ++i)
		{
			if (!IsAllowedChar(name[i])) return false;
		}
		return true;
	}

	uint32_t Server::NextPlayerId()
	{
		// Ids are never reused inside a session, so a late packet can never be
		// applied to a different player.
		return mNextPlayerId++;
	}

	size_t Server::ReadyClientCount() const
	{
		size_t count = 0;
		for (const auto& client : mClients)
		{
			if (client->socket != kInvalidSocket && client->handshakeDone) ++count;
		}
		return count;
	}
}
