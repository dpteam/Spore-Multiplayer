/****************************************************************************
* Spore Multiplayer
*
* Non blocking TCP client used by the Spore mod. All the socket work happens in
* a dedicated thread; the game thread only pushes outgoing messages and drains
* the incoming queue, so a slow or dead server can never freeze the game.
****************************************************************************/
#pragma once

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "../../Shared/MpProtocol.h"

namespace SporeMP
{
	enum class ConnectionState
	{
		Disconnected = 0,
		Connecting = 1,
		Connected = 2,
	};

	struct NetMessage
	{
		MsgType type;
		std::vector<uint8_t> payload;
	};

	class NetworkClient
	{
	public:
		NetworkClient();
		~NetworkClient();

		NetworkClient(const NetworkClient&) = delete;
		NetworkClient& operator=(const NetworkClient&) = delete;

		/// Starts connecting in the background. The Hello message is sent as soon as
		/// the connection is established.
		/// @returns false if a connection is already in progress.
		bool Connect(const std::string& host, uint16_t port, const MsgHello& hello);

		/// Closes the connection and stops the worker thread.
		void Disconnect();

		ConnectionState GetState() const { return mState.load(); }

		/// Human readable reason of the last disconnection, empty if none.
		std::string GetLastError();

		/// Queues a message. It is sent by the worker thread.
		void Send(MsgType type, const void* data, size_t size);

		/// Removes all the messages received since the last call.
		void DrainIncoming(std::vector<NetMessage>& out);

		/// Round trip time in milliseconds, measured with Ping/Pong messages.
		int GetLatency() const { return mLatency.load(); }

		/// Called by the session when a Pong is received.
		void OnPongReceived(uint32_t clientTime);

		/// Sends a Ping if enough time has passed. Called from the game thread.
		void UpdatePing(float deltaTime);

	private:
		void ThreadMain(std::string host, uint16_t port, MsgHello hello);
		bool OpenSocket(const std::string& host, uint16_t port);
		void CloseSocket();
		bool SendAll(const uint8_t* data, size_t size);
		void ParseIncoming();
		void PushMessage(MsgType type, const uint8_t* data, size_t size);
		void SetError(const char* error);

		std::thread mThread;
		std::atomic<ConnectionState> mState{ ConnectionState::Disconnected };
		std::atomic<bool> mStopRequested{ false };
		std::atomic<int> mLatency{ 0 };
		std::atomic<uint32_t> mPingSequence{ 0 };
		std::atomic<float> mPingTimer{ 0.0f };

		std::mutex mSocketMutex;
		uintptr_t mSocket = static_cast<uintptr_t>(~0);

		std::mutex mSendMutex;
		std::vector<uint8_t> mSendBuffer;

		std::mutex mReceiveMutex;
		std::vector<NetMessage> mIncoming;

		std::mutex mErrorMutex;
		std::string mLastError;

		std::vector<uint8_t> mReadBuffer;
	};
}
