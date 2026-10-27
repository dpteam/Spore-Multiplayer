/****************************************************************************
* Spore Multiplayer
*
* The session is the bridge between the game and the network. It runs on the
* game thread (called from the App update message) and:
*
*   - owns the NetworkClient and the connection state machine
*   - sends the local player state at a fixed rate
*   - keeps the table of the other players and their interpolated transforms
*   - relays the chat lines and the notifications
****************************************************************************/
#pragma once

#include <cstdint>
#include <deque>
#include <string>
#include <vector>

#include "MpConfig.h"
#include "MpNet.h"

namespace SporeMP
{
	/// One line of the chat/notification log.
	struct ChatLine
	{
		uint32_t senderId = 0;
		std::string sender;
		std::string text;
		bool system = false;
	};

	/// A player that is not the local one.
	struct RemotePlayer
	{
		uint32_t id = 0;
		std::string name;
		Stage stage = Stage::kNone;
		ResKey avatarKey = { 0, 0, 0 };

		bool hasState = false;
		uint32_t systemId = 0;
		Vec3f position = { 0, 0, 0 };
		Quatf orientation = { 0, 0, 0, 1 };

		// Interpolation data used to smooth the received positions.
		Vec3f previousPosition = { 0, 0, 0 };
		Quatf previousOrientation = { 0, 0, 0, 1 };
		Vec3f targetPosition = { 0, 0, 0 };
		Quatf targetOrientation = { 0, 0, 0, 1 };
		float interpolationTime = 1.0f;
		bool interpolating = false;

		/// True if the avatar of this player is currently spawned in our world.
		bool avatarActive = false;
	};

	class Session
	{
	public:
		static Session& Get();

		void Initialize(const std::string& directory);
		void Update(float deltaTime);
		void Dispose();

		// --- connection ------------------------------------------------------
		bool Connect(const std::string& host, uint16_t port);
		void Disconnect();
		bool IsConnected() const;
		ConnectionState GetState() const;
		std::string GetStatusText() const;

		// --- commands --------------------------------------------------------
		void SendChat(const std::string& text);
		void SetName(const std::string& name);

		// --- data ------------------------------------------------------------
		ClientConfig& Config() { return mConfig; }
		uint32_t GetLocalId() const { return mLocalId; }
		const std::string& GetLocalName() const { return mLocalName; }
		int GetLatency() const { return mLatency; }
		const std::string& GetSessionName() const { return mSessionName; }
		const std::vector<RemotePlayer>& GetPlayers() const { return mPlayers; }
		const std::deque<ChatLine>& GetChatLog() const { return mChatLog; }
		int GetChatLineCount() const { return mConfig.chatLines; }

		/// Adds a local notification (shown in the HUD and in the console).
		void AddSystemLine(const std::string& text);

		/// Writes a line both to the HUD log and to the game console.
		void Notify(const std::string& text);

		/// The stage the local player is in right now.
		Stage GetLocalStage() const { return mLocalStage; }

		/// Identifier of the star system / planet the local player is in, 0 if unknown.
		uint32_t GetLocalSystemId() const { return mLocalSystemId; }

		/// Returns the directory where the DLL and the configuration file live.
		const std::string& GetDirectory() const { return mLogDirectory; }

	private:
		Session();

		void UpdateLocalContext();

		void ProcessMessages();
		void HandleWelcome(const uint8_t* data, size_t size);
		void HandleReject(const uint8_t* data, size_t size);
		void HandlePlayerList(const uint8_t* data, size_t size);
		void HandlePlayerJoined(const uint8_t* data, size_t size);
		void HandlePlayerLeft(const uint8_t* data, size_t size);
		void HandlePlayerUpdated(const uint8_t* data, size_t size);
		void HandlePlayerStates(const uint8_t* data, size_t size);
		void HandleChat(const uint8_t* data, size_t size);
		void HandleKick(const uint8_t* data, size_t size);

		void SendLocalState();
		void ApplyState(RemotePlayer& player, const StateEntry& entry);

		RemotePlayer* FindPlayer(uint32_t id);

		ClientConfig mConfig;
		std::string mConfigPath;
		std::string mLogDirectory;

		NetworkClient mNetwork;
		std::vector<NetMessage> mIncoming;

		bool mInitialized = false;
		bool mWasConnected = false;
		float mSendTimer = 0.0f;
		float mConnectTimer = 0.0f;
		uint32_t mConnectTimeout = 0;

		uint32_t mLocalId = 0;
		std::string mLocalName;
		std::string mSessionName;
		std::string mLastError;
		int mLatency = 0;

		Stage mLocalStage = Stage::kNone;
		uint32_t mLocalSystemId = 0;
		ResKey mLocalAvatarKey = { 0, 0, 0 };

		std::vector<RemotePlayer> mPlayers;
		std::deque<ChatLine> mChatLog;
	};
}
