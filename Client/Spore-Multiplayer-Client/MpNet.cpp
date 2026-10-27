/****************************************************************************
* Spore Multiplayer
****************************************************************************/
#include "stdafx.h"
#include "MpNet.h"
#include "MpLog.h"

#include <winsock2.h>
#include <ws2tcpip.h>

#pragma comment(lib, "ws2_32.lib")

namespace SporeMP
{
	namespace
	{
		SOCKET ToSocket(uintptr_t value)
		{
			return static_cast<SOCKET>(value);
		}

		uintptr_t FromSocket(SOCKET value)
		{
			return static_cast<uintptr_t>(value);
		}

		const uintptr_t kInvalidSocket = static_cast<uintptr_t>(INVALID_SOCKET);

		/// Initializes Winsock once for the whole process.
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
	}

	NetworkClient::NetworkClient()
	{
	}

	NetworkClient::~NetworkClient()
	{
		Disconnect();

		if (mThread.joinable())
		{
			mThread.join();
		}
	}

	bool NetworkClient::Connect(const std::string& host, uint16_t port, const MsgHello& hello)
	{
		if (mState.load() != ConnectionState::Disconnected)
		{
			return false;
		}

		if (!EnsureWinsock())
		{
			SetError("Could not initialize Winsock");
			return false;
		}

		// Make sure a previous worker is gone before starting a new one.
		if (mThread.joinable())
		{
			mStopRequested.store(true);
			mThread.join();
		}

		mStopRequested.store(false);
		mLatency.store(0);
		mPingTimer.store(0.0f);
		SetError("");

		{
			std::lock_guard<std::mutex> lock(mReceiveMutex);
			mIncoming.clear();
		}
		{
			std::lock_guard<std::mutex> lock(mSendMutex);
			mSendBuffer.clear();
		}

		mState.store(ConnectionState::Connecting);

		MP_LOG_INFO("Connecting to %s:%u", host.c_str(), port);
		mThread = std::thread(&NetworkClient::ThreadMain, this, host, port, hello);
		return true;
	}

	void NetworkClient::Disconnect()
	{
		mStopRequested.store(true);

		// Closing the socket here wakes up select() in the worker thread.
		std::lock_guard<std::mutex> lock(mSocketMutex);
		if (mSocket != kInvalidSocket)
		{
			shutdown(ToSocket(mSocket), SD_BOTH);
			closesocket(ToSocket(mSocket));
			mSocket = kInvalidSocket;
		}

		mState.store(ConnectionState::Disconnected);
	}

	std::string NetworkClient::GetLastError()
	{
		std::lock_guard<std::mutex> lock(mErrorMutex);
		return mLastError;
	}

	void NetworkClient::SetError(const char* error)
	{
		std::lock_guard<std::mutex> lock(mErrorMutex);
		mLastError = error != nullptr ? error : "";
	}

	void NetworkClient::Send(MsgType type, const void* data, size_t size)
	{
		if (size > kMaxPayloadSize) return;

		const size_t offset = kHeaderSize + size;

		std::lock_guard<std::mutex> lock(mSendMutex);
		const size_t start = mSendBuffer.size();
		mSendBuffer.resize(start + offset);

		uint16_t messageType = static_cast<uint16_t>(type);
		uint16_t payloadSize = static_cast<uint16_t>(size);
		std::memcpy(mSendBuffer.data() + start, &messageType, sizeof(messageType));
		std::memcpy(mSendBuffer.data() + start + 2, &payloadSize, sizeof(payloadSize));
		if (size > 0 && data != nullptr)
		{
			std::memcpy(mSendBuffer.data() + start + kHeaderSize, data, size);
		}
	}

	void NetworkClient::DrainIncoming(std::vector<NetMessage>& out)
	{
		std::lock_guard<std::mutex> lock(mReceiveMutex);
		out.insert(out.end(), mIncoming.begin(), mIncoming.end());
		mIncoming.clear();
	}

	void NetworkClient::PushMessage(MsgType type, const uint8_t* data, size_t size)
	{
		NetMessage message;
		message.type = type;
		if (size > 0 && data != nullptr)
		{
			message.payload.assign(data, data + size);
		}

		std::lock_guard<std::mutex> lock(mReceiveMutex);
		mIncoming.push_back(std::move(message));
	}

	void NetworkClient::OnPongReceived(uint32_t clientTime)
	{
		const uint32_t now = static_cast<uint32_t>(GetTickCount());
		const int32_t roundTrip = static_cast<int32_t>(now - clientTime);
		if (roundTrip >= 0 && roundTrip < 10000)
		{
			mLatency.store(roundTrip);
		}
	}

	void NetworkClient::UpdatePing(float deltaTime)
	{
		if (mState.load() != ConnectionState::Connected) return;

		const float timer = mPingTimer.load() + deltaTime;
		if (timer < 2.0f)
		{
			mPingTimer.store(timer);
			return;
		}
		mPingTimer.store(0.0f);

		MsgPing ping;
		ping.clientTime = static_cast<uint32_t>(GetTickCount());
		mPingSequence.fetch_add(1);
		Send(MsgType::Ping, &ping, sizeof(ping));
	}

	// ------------------------------------------------------------------------
	// Worker thread
	// ------------------------------------------------------------------------

	bool NetworkClient::OpenSocket(const std::string& host, uint16_t port)
	{
		SOCKET sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
		if (sock == INVALID_SOCKET)
		{
			SetError("Could not create the socket");
			return false;
		}

		BOOL noDelay = TRUE;
		setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&noDelay), sizeof(noDelay));

		u_long nonBlocking = 1;
		ioctlsocket(sock, FIONBIO, &nonBlocking);

		sockaddr_in address;
		std::memset(&address, 0, sizeof(address));
		address.sin_family = AF_INET;
		address.sin_port = htons(port);

		if (inet_pton(AF_INET, host.c_str(), &address.sin_addr) != 1)
		{
			// Not a literal address, resolve the name.
			addrinfo hints;
			std::memset(&hints, 0, sizeof(hints));
			hints.ai_family = AF_INET;
			hints.ai_socktype = SOCK_STREAM;

			addrinfo* result = nullptr;
			if (getaddrinfo(host.c_str(), nullptr, &hints, &result) != 0 || result == nullptr)
			{
				closesocket(sock);
				SetError("Could not resolve the server address");
				return false;
			}

			address.sin_addr = reinterpret_cast<sockaddr_in*>(result->ai_addr)->sin_addr;
			freeaddrinfo(result);
		}

		connect(sock, reinterpret_cast<sockaddr*>(&address), sizeof(address));

		// Wait up to 5 seconds for the connection to complete.
		fd_set writeSet;
		fd_set errorSet;
		FD_ZERO(&writeSet);
		FD_ZERO(&errorSet);
		FD_SET(sock, &writeSet);
		FD_SET(sock, &errorSet);

		timeval timeout;
		timeout.tv_sec = 5;
		timeout.tv_usec = 0;

		const int result = select(0, nullptr, &writeSet, &errorSet, &timeout);
		if (result <= 0 || FD_ISSET(sock, &errorSet))
		{
			closesocket(sock);
			SetError("Could not connect to the server");
			return false;
		}

		std::lock_guard<std::mutex> lock(mSocketMutex);
		mSocket = FromSocket(sock);
		return true;
	}

	void NetworkClient::CloseSocket()
	{
		std::lock_guard<std::mutex> lock(mSocketMutex);
		if (mSocket != kInvalidSocket)
		{
			closesocket(ToSocket(mSocket));
			mSocket = kInvalidSocket;
		}
	}

	bool NetworkClient::SendAll(const uint8_t* data, size_t size)
	{
		size_t offset = 0;
		while (offset < size)
		{
			SOCKET sock;
			{
				std::lock_guard<std::mutex> lock(mSocketMutex);
				if (mSocket == kInvalidSocket) return false;
				sock = ToSocket(mSocket);
			}

			const int sent = send(sock, reinterpret_cast<const char*>(data + offset),
				static_cast<int>(size - offset), 0);

			if (sent == SOCKET_ERROR)
			{
				const int error = WSAGetLastError();
				if (error == WSAEWOULDBLOCK)
				{
					fd_set writeSet;
					FD_ZERO(&writeSet);
					FD_SET(sock, &writeSet);
					timeval timeout = { 0, 20000 };
					select(0, nullptr, &writeSet, nullptr, &timeout);
					continue;
				}

				SetError("The connection was lost while sending data");
				return false;
			}

			offset += static_cast<size_t>(sent);
		}
		return true;
	}

	void NetworkClient::ParseIncoming()
	{
		while (true)
		{
			const size_t available = mReadBuffer.size();
			if (available < kHeaderSize) return;

			uint16_t type = 0;
			uint16_t size = 0;
			std::memcpy(&type, mReadBuffer.data(), sizeof(type));
			std::memcpy(&size, mReadBuffer.data() + 2, sizeof(size));

			if (size > kMaxPayloadSize)
			{
				SetError("The server sent an invalid message");
				mStopRequested.store(true);
				return;
			}

			if (available < kHeaderSize + size) return;

			PushMessage(static_cast<MsgType>(type), mReadBuffer.data() + kHeaderSize, size);
			mReadBuffer.erase(mReadBuffer.begin(), mReadBuffer.begin() + kHeaderSize + size);
		}
	}

	void NetworkClient::ThreadMain(std::string host, uint16_t port, MsgHello hello)
	{
		if (!OpenSocket(host, port))
		{
			mState.store(ConnectionState::Disconnected);
			return;
		}

		// The Hello message is the first thing the server must receive.
		{
			std::vector<uint8_t> frame(kHeaderSize + sizeof(hello));
			uint16_t messageType = static_cast<uint16_t>(MsgType::Hello);
			uint16_t payloadSize = static_cast<uint16_t>(sizeof(hello));
			std::memcpy(frame.data(), &messageType, 2);
			std::memcpy(frame.data() + 2, &payloadSize, 2);
			std::memcpy(frame.data() + kHeaderSize, &hello, sizeof(hello));

			if (!SendAll(frame.data(), frame.size()))
			{
				CloseSocket();
				mState.store(ConnectionState::Disconnected);
				return;
			}
		}

		mState.store(ConnectionState::Connected);
		mReadBuffer.clear();

		uint64_t lastReceive = GetTickCount64();

		while (!mStopRequested.load())
		{
			SOCKET sock;
			{
				std::lock_guard<std::mutex> lock(mSocketMutex);
				if (mSocket == kInvalidSocket)
				{
					mState.store(ConnectionState::Disconnected);
					return;
				}
				sock = ToSocket(mSocket);
			}

			// 1) Send everything that is queued.
			std::vector<uint8_t> outgoing;
			{
				std::lock_guard<std::mutex> lock(mSendMutex);
				outgoing.swap(mSendBuffer);
			}
			if (!outgoing.empty())
			{
				if (!SendAll(outgoing.data(), outgoing.size()))
				{
					CloseSocket();
					mState.store(ConnectionState::Disconnected);
					return;
				}
			}

			// 2) Read whatever the server sent.
			fd_set readSet;
			FD_ZERO(&readSet);
			FD_SET(sock, &readSet);

			timeval timeout = { 0, 5000 };  // 5 ms
			const int ready = select(0, &readSet, nullptr, nullptr, &timeout);

			if (ready > 0 && FD_ISSET(sock, &readSet))
			{
				char buffer[4096];
				const int received = recv(sock, buffer, sizeof(buffer), 0);

				if (received == 0)
				{
					SetError("The server closed the connection");
					CloseSocket();
					mState.store(ConnectionState::Disconnected);
					return;
				}

				if (received == SOCKET_ERROR)
				{
					const int error = WSAGetLastError();
					if (error != WSAEWOULDBLOCK)
					{
						SetError("The connection was lost");
						CloseSocket();
						mState.store(ConnectionState::Disconnected);
						return;
					}
				}
				else
				{
					lastReceive = GetTickCount64();
					mReadBuffer.insert(mReadBuffer.end(), buffer, buffer + received);
					ParseIncoming();
				}
			}

			// 3) Drop the connection if the server went silent for too long.
			if (GetTickCount64() - lastReceive > 20000)
			{
				SetError("The connection timed out");
				CloseSocket();
				mState.store(ConnectionState::Disconnected);
				return;
			}
		}

		CloseSocket();
		mState.store(ConnectionState::Disconnected);
	}
}
