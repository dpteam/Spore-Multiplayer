/****************************************************************************
* Spore Multiplayer
****************************************************************************/
#include "stdafx.h"
#include "MpSession.h"
#include "MpAssets.h"
#include "MpAvatars.h"
#include "MpLog.h"

#include <algorithm>
#include <cmath>

namespace SporeMP
{
	namespace
	{
		const size_t kMaxChatLogLines = 64;

		/// Prints a line to the in game console, if it is available.
		void PrintToConsole(const std::string& text)
		{
			auto* manager = App::ICheatManager::Get();
			if (manager == nullptr || manager->GetArgScript() == nullptr) return;

			App::ConsolePrintF("%s", text.c_str());
		}

		/// Returns the directory of the DLL, with a trailing backslash.
		std::string GetModuleDirectory()
		{
			char path[MAX_PATH] = { 0 };
			const DWORD length = GetModuleFileNameA(nullptr, path, MAX_PATH);
			if (length == 0) return std::string();

			std::string fullPath(path, length);
			const size_t separator = fullPath.find_last_of("\\/");
			if (separator == std::string::npos) return std::string();
			return fullPath.substr(0, separator + 1);
		}

		Stage StageFromGameMode(uint32_t gameModeId)
		{
			switch (gameModeId)
			{
			case kGameCell: return Stage::kCell;
			case kGameCreature: return Stage::kCreature;
			case kGameTribe: return Stage::kTribe;
			case kGameCiv: return Stage::kCivilization;
			case kGameSpace: return Stage::kSpace;
			case kEditorMode: return Stage::kEditor;
			case kScenarioMode: return Stage::kScenario;
			default: return Stage::kNone;
			}
		}

		Vec3f FromVector3(const Math::Vector3& value)
		{
			Vec3f result;
			result.x = value.x;
			result.y = value.y;
			result.z = value.z;
			return result;
		}

		Quatf FromQuaternion(const Math::Quaternion& value)
		{
			Quatf result;
			result.x = value.x;
			result.y = value.y;
			result.z = value.z;
			result.w = value.w;
			return result;
		}

		Math::Vector3 Lerp(const Math::Vector3& a, const Math::Vector3& b, float t)
		{
			return Math::Vector3(
				a.x + (b.x - a.x) * t,
				a.y + (b.y - a.y) * t,
				a.z + (b.z - a.z) * t);
		}

		/// Normalized linear interpolation, used for the orientation. It is good
		/// enough for the small angles between two consecutive states.
		Math::Quaternion Slerp(const Math::Quaternion& a, const Math::Quaternion& b, float t)
		{
			Math::Quaternion result(
				a.x + (b.x - a.x) * t,
				a.y + (b.y - a.y) * t,
				a.z + (b.z - a.z) * t,
				a.w + (b.w - a.w) * t);

			const float length = std::sqrt(
				result.x * result.x + result.y * result.y + result.z * result.z + result.w * result.w);

			if (length > 0.00001f)
			{
				result.x /= length;
				result.y /= length;
				result.z /= length;
				result.w /= length;
			}

			return result;
		}

		Vec3f Lerp(const Vec3f& a, const Vec3f& b, float t)
		{
			Vec3f result;
			result.x = a.x + (b.x - a.x) * t;
			result.y = a.y + (b.y - a.y) * t;
			result.z = a.z + (b.z - a.z) * t;
			return result;
		}

		Quatf Lerp(const Quatf& a, const Quatf& b, float t)
		{
			const Math::Quaternion result = Slerp(
				Math::Quaternion(a.x, a.y, a.z, a.w),
				Math::Quaternion(b.x, b.y, b.z, b.w),
				t);

			Quatf output;
			output.x = result.x;
			output.y = result.y;
			output.z = result.z;
			output.w = result.w;
			return output;
		}
	}

	Session::Session()
	{
	}

	Session& Session::Get()
	{
		static Session instance;
		return instance;
	}

	void Session::Initialize(const std::string& directory)
	{
		if (mInitialized) return;

		mLogDirectory = directory;
		mConfigPath = directory + "SporeMultiplayer.ini";

		if (!LoadClientConfig(mConfigPath, mConfig))
		{
			// First run: create the file so the players can edit it easily.
			SaveClientConfig(mConfigPath, mConfig);
			MP_LOG_INFO("Created the default configuration at %s", mConfigPath.c_str());
		}
		else
		{
			MP_LOG_INFO("Loaded the configuration from %s", mConfigPath.c_str());
		}

		if (mConfig.name.empty())
		{
			mConfig.name = "Player";
		}

		mLocalName = mConfig.name;
		mInitialized = true;

		AvatarAssets::Get().Initialize(directory);

		if (mConfig.autoConnect)
		{
			Connect(mConfig.host, mConfig.port);
		}
	}

	void Session::Dispose()
	{
		Avatars::Get().Clear();
		AvatarAssets::Get().Dispose();
		mNetwork.Disconnect();
		mPlayers.clear();
		mIncoming.clear();
		mInitialized = false;
	}

	// ------------------------------------------------------------------------
	// Connection
	// ------------------------------------------------------------------------

	bool Session::Connect(const std::string& host, uint16_t port)
	{
		if (!mInitialized) return false;

		if (IsConnected())
		{
			Notify("Already connected to a session");
			return false;
		}

		mConfig.host = host;
		mConfig.port = port;
		SaveClientConfig(mConfigPath, mConfig);

		mPlayers.clear();
		Avatars::Get().Clear();
		mLocalId = 0;
		mSessionName.clear();
		mLastError.clear();

		MsgHello hello;
		std::memset(&hello, 0, sizeof(hello));
		hello.protocolVersion = kProtocolVersion;
		hello.clientVersion = 1;
		hello.clientNonce = static_cast<uint32_t>(GetTickCount());
		CopyString(hello.name, sizeof(hello.name), mLocalName.c_str());
		hello.stage = Stage::kNone;

		if (!mNetwork.Connect(host, port, hello))
		{
			Notify("Could not start the connection");
			return false;
		}

		Notify("Connecting to " + host + ":" + std::to_string(port) + "...");
		return true;
	}

	void Session::Disconnect()
	{
		if (mNetwork.GetState() == ConnectionState::Disconnected) return;

		mNetwork.Disconnect();
		Avatars::Get().Clear();
		mPlayers.clear();
		mLocalId = 0;
		Notify("Disconnected from the session");
	}

	bool Session::IsConnected() const
	{
		return mNetwork.GetState() == ConnectionState::Connected && mLocalId != 0;
	}

	ConnectionState Session::GetState() const
	{
		return mNetwork.GetState();
	}

	std::string Session::GetStatusText() const
	{
		if (IsConnected())
		{
			return "Online";
		}

		switch (mNetwork.GetState())
		{
		case ConnectionState::Connecting:
			return "Connecting";
		case ConnectionState::Connected:
			return "Handshake";
		default:
			return "Offline";
		}
	}

	// ------------------------------------------------------------------------
	// Update
	// ------------------------------------------------------------------------

	void Session::Update(float deltaTime)
	{
		if (!mInitialized) return;

		UpdateLocalContext();

		mIncoming.clear();
		mNetwork.DrainIncoming(mIncoming);
		ProcessMessages();

		// The connection can be lost at any moment; detect it here so the HUD
		// and the avatars are updated even if no message arrives.
		const bool connected = IsConnected();
		if (mWasConnected && !connected)
		{
			const std::string error = mNetwork.GetLastError();
			if (!error.empty())
			{
				Notify("Disconnected: " + error);
			}
			else
			{
				Notify("Disconnected from the session");
			}

			Avatars::Get().Clear();
			mPlayers.clear();
			mLocalId = 0;
		}
		mWasConnected = connected;

		if (!connected)
		{
			return;
		}

		mLatency = mNetwork.GetLatency();
		mNetwork.UpdatePing(deltaTime);

		// Send the local state at the configured rate.
		const float interval = 1.0f / static_cast<float>((std::max)(1, mConfig.sendRate));
		mSendTimer += deltaTime;
		if (mSendTimer >= interval)
		{
			mSendTimer = 0.0f;
			SendLocalState();
		}

		// Share the local avatar creation (the exact ship/creature PNG) once.
		AvatarAssets::Get().UpdateLocalAsset(mNetwork, mLocalAvatarKey, mLocalStage);

		// Smooth the movement of the other players.
		for (RemotePlayer& player : mPlayers)
		{
			if (!player.interpolating) continue;

			player.interpolationTime += deltaTime * static_cast<float>((std::max)(1, mConfig.sendRate));
			float alpha = player.interpolationTime;
			if (alpha > 1.0f) alpha = 1.0f;

			player.position = Lerp(player.previousPosition, player.targetPosition, alpha);
			player.orientation = Lerp(player.previousOrientation, player.targetOrientation, alpha);

			if (alpha >= 1.0f)
			{
				player.interpolating = false;
			}
		}

		Avatars::Get().Update(deltaTime);
	}

	void Session::UpdateLocalContext()
	{
		const uint32_t gameModeId = Simulator::GetGameModeID();
		mLocalStage = StageFromGameMode(gameModeId);
		mLocalSystemId = 0;
		mLocalAvatarKey = ResKey{ 0, 0, 0 };

		// The first time we get to a stage with a creature, use the species name
		// as the player name (unless the player configured one explicitly).
		if (!mNameResolved && IsCreatureStage(mLocalStage) && mConfig.name.empty())
		{
			ResolveNameFromSpecies();
		}

		if (mLocalStage == Stage::kSpace)
		{
			auto* starRecord = Simulator::GetActiveStarRecord();
			if (starRecord != nullptr)
			{
				mLocalSystemId = starRecord->GetID().internalValue;
			}

			auto* ufo = Simulator::GetPlayerUFO();
			if (ufo != nullptr)
			{
				const ResourceKey& key = ufo->GetModelKey();
				mLocalAvatarKey.instance = key.instanceID;
				mLocalAvatarKey.type = key.typeID;
				mLocalAvatarKey.group = key.groupID;
			}
		}
		else if (mLocalStage == Stage::kCreature || mLocalStage == Stage::kTribe || mLocalStage == Stage::kCivilization)
		{
			auto* planetRecord = Simulator::GetActivePlanetRecord();
			if (planetRecord != nullptr)
			{
				mLocalSystemId = planetRecord->GetID().internalValue;
			}

			if (IsCreatureStage(mLocalStage))
			{
				auto* avatar = GameNounManager.GetAvatar();
				if (avatar != nullptr && avatar->mpSpeciesProfile != nullptr)
				{
					// GetSpeciesKey() is not implemented in the SDK, but the profile
					// stores the species key directly.
					const ResourceKey& key = avatar->mpSpeciesProfile->mCreatureKey;
					mLocalAvatarKey.instance = key.instanceID;
					mLocalAvatarKey.type = key.typeID;
					mLocalAvatarKey.group = key.groupID;
				}
			}
		}
	}

	void Session::ResolveNameFromSpecies()
	{
		auto* avatar = GameNounManager.GetAvatar();
		if (avatar == nullptr || avatar->mpSpeciesProfile == nullptr) return;

		eastl::string16 speciesName;
		avatar->mpSpeciesProfile->GetSpeciesName(speciesName);
		if (speciesName.empty()) return;

		// The protocol is ASCII only, so non ASCII names are transliterated by
		// dropping the unknown characters.
		std::string ascii;
		ascii.reserve(speciesName.size());
		for (size_t i = 0; i < speciesName.size(); ++i)
		{
			const char16_t c = speciesName[i];
			if (c >= 0x20 && c < 0x7F)
			{
				ascii += static_cast<char>(c);
			}
			else if (c == ' ' || c == '-' || c == '\'')
			{
				ascii += static_cast<char>(c);
			}
		}

		char sanitized[kMaxNameLength];
		SanitizeText(ascii.c_str(), sanitized, sizeof(sanitized), "");
		if (sanitized[0] == '\0') return;

		mLocalName = sanitized;
		mNameResolved = true;

		MP_LOG_INFO("Using the species name as player name: '%s'", sanitized);

		// Tell the server so the other players see the new name.
		if (IsConnected())
		{
			MsgSetName message;
			std::memset(&message, 0, sizeof(message));
			CopyString(message.name, sizeof(message.name), sanitized);
			mNetwork.Send(MsgType::SetName, &message, sizeof(message));
		}
	}

	void Session::SendLocalState()	{
		MsgLocalState state;
		std::memset(&state, 0, sizeof(state));
		state.stage = mLocalStage;
		state.flags = 0;

		Math::Vector3 position(0.0f, 0.0f, 0.0f);
		Math::Quaternion orientation(0.0f, 0.0f, 0.0f, 1.0f);
		bool hasPosition = false;

		if (mLocalStage == Stage::kSpace)
		{
			auto* ufo = Simulator::GetPlayerUFO();
			if (ufo != nullptr)
			{
				position = ufo->mPosition;
				orientation = ufo->mOrientation;
				hasPosition = true;
			}
		}
		else if (IsCreatureStage(mLocalStage))
		{
			auto* avatar = GameNounManager.GetAvatar();
			if (avatar != nullptr)
			{
				position = avatar->mPosition;
				orientation = avatar->mOrientation;
				hasPosition = true;
			}
		}

		if (hasPosition)
		{
			state.flags |= kStateHasPosition;
			state.position = FromVector3(position);
			state.orientation = FromQuaternion(orientation);
		}

		state.systemId = mLocalSystemId;
		mNetwork.Send(MsgType::LocalState, &state, sizeof(state));
	}

	// ------------------------------------------------------------------------
	// Message handling
	// ------------------------------------------------------------------------

	void Session::ProcessMessages()
	{
		for (const NetMessage& message : mIncoming)
		{
			const uint8_t* data = message.payload.empty() ? nullptr : message.payload.data();
			const size_t size = message.payload.size();

			switch (message.type)
			{
			case MsgType::Welcome:
				HandleWelcome(data, size);
				break;
			case MsgType::Reject:
				HandleReject(data, size);
				break;
			case MsgType::PlayerList:
				HandlePlayerList(data, size);
				break;
			case MsgType::PlayerJoined:
				HandlePlayerJoined(data, size);
				break;
			case MsgType::PlayerLeft:
				HandlePlayerLeft(data, size);
				break;
			case MsgType::PlayerUpdated:
				HandlePlayerUpdated(data, size);
				break;
			case MsgType::PlayerStates:
				HandlePlayerStates(data, size);
				break;
			case MsgType::Chat:
				HandleChat(data, size);
				break;
			case MsgType::Pong:
				if (size >= sizeof(MsgPong))
				{
					MsgPong pong;
					std::memcpy(&pong, data, sizeof(pong));
					mNetwork.OnPongReceived(pong.clientTime);
				}
				break;
			case MsgType::Kick:
				HandleKick(data, size);
				break;
			case MsgType::AssetInfo:
				AvatarAssets::Get().HandleInfo(data, size);
				break;
			case MsgType::AssetChunk:
				AvatarAssets::Get().HandleChunk(data, size);
				break;
			case MsgType::AssetDone:
				AvatarAssets::Get().HandleDone(data, size);
				break;
			default:
				break;
			}
		}
	}

	void Session::HandleWelcome(const uint8_t* data, size_t size)
	{
		if (data == nullptr || size < sizeof(MsgWelcome)) return;

		MsgWelcome welcome;
		std::memcpy(&welcome, data, sizeof(welcome));

		mLocalId = welcome.playerId;
		mSessionName = welcome.sessionName;

		if (welcome.assignedName[0] != '\0')
		{
			mLocalName = welcome.assignedName;
			mConfig.name = welcome.assignedName;
		}

		mSendTimer = 0.0f;
		mWasConnected = true;

		Notify("Connected to '" + mSessionName + "' as " + mLocalName);
		MP_LOG_INFO("Connected as player %u ('%s') to session '%s'",
			mLocalId, mLocalName.c_str(), mSessionName.c_str());
	}

	void Session::HandleReject(const uint8_t* data, size_t size)
	{
		if (data == nullptr || size < sizeof(MsgReject)) return;

		MsgReject reject;
		std::memcpy(&reject, data, sizeof(reject));

		mLastError = reject.message;
		Notify("Connection refused: " + mLastError);
		MP_LOG_WARN("Connection refused: %s", mLastError.c_str());

		mNetwork.Disconnect();
	}

	void Session::HandlePlayerList(const uint8_t* data, size_t size)
	{
		if (data == nullptr || size < sizeof(MsgPlayerListHeader)) return;

		MsgPlayerListHeader header;
		std::memcpy(&header, data, sizeof(header));

		const size_t needed = PlayerListPayloadSize(header.count);
		if (size < needed) return;

		const PlayerInfo* entries = PlayerListEntries(reinterpret_cast<const MsgPlayerListHeader*>(data));

		mPlayers.clear();
		for (int i = 0; i < header.count; ++i)
		{
			const PlayerInfo& info = entries[i];
			if (info.id == mLocalId) continue;

			RemotePlayer player;
			player.id = info.id;
			player.name = info.name;
			player.stage = info.stage;
			player.avatarKey = info.shipModel;
			mPlayers.push_back(player);
		}

		MP_LOG_INFO("Received the player list (%d players)", header.count);
	}

	void Session::HandlePlayerJoined(const uint8_t* data, size_t size)
	{
		if (data == nullptr || size < sizeof(MsgPlayerJoined)) return;

		MsgPlayerJoined joined;
		std::memcpy(&joined, data, sizeof(joined));

		if (joined.player.id == mLocalId) return;
		if (FindPlayer(joined.player.id) != nullptr) return;

		RemotePlayer player;
		player.id = joined.player.id;
		player.name = joined.player.name;
		player.stage = joined.player.stage;
		player.avatarKey = joined.player.shipModel;
		mPlayers.push_back(player);

		Notify(player.name + " joined the session");
	}

	void Session::HandlePlayerLeft(const uint8_t* data, size_t size)
	{
		if (data == nullptr || size < sizeof(MsgPlayerLeft)) return;

		MsgPlayerLeft left;
		std::memcpy(&left, data, sizeof(left));

		for (auto it = mPlayers.begin(); it != mPlayers.end(); ++it)
		{
			if (it->id != left.id) continue;

			Notify(it->name + " left the session");
			mPlayers.erase(it);
			break;
		}
	}

	void Session::HandlePlayerUpdated(const uint8_t* data, size_t size)
	{
		if (data == nullptr || size < sizeof(MsgPlayerUpdated)) return;

		MsgPlayerUpdated updated;
		std::memcpy(&updated, data, sizeof(updated));

		RemotePlayer* player = FindPlayer(updated.player.id);
		if (player == nullptr) return;

		player->name = updated.player.name;
		player->stage = updated.player.stage;
		player->avatarKey = updated.player.shipModel;
	}

	void Session::HandlePlayerStates(const uint8_t* data, size_t size)
	{
		if (data == nullptr || size < sizeof(MsgPlayerStatesHeader)) return;

		MsgPlayerStatesHeader header;
		std::memcpy(&header, data, sizeof(header));

		const size_t needed = PlayerStatesPayloadSize(header.count);
		if (size < needed) return;

		const StateEntry* entries = PlayerStatesEntries(reinterpret_cast<const MsgPlayerStatesHeader*>(data));

		for (int i = 0; i < header.count; ++i)
		{
			RemotePlayer* player = FindPlayer(entries[i].id);
			if (player == nullptr) continue;

			ApplyState(*player, entries[i]);
		}
	}

	void Session::ApplyState(RemotePlayer& player, const StateEntry& entry)
	{
		player.stage = static_cast<Stage>(entry.stage);
		player.systemId = entry.systemId;

		if ((entry.flags & kStateHasPosition) == 0)
		{
			player.hasState = false;
			return;
		}

		if (!player.hasState || !mConfig.interpolation)
		{
			player.position = entry.position;
			player.orientation = entry.orientation;
			player.previousPosition = entry.position;
			player.previousOrientation = entry.orientation;
			player.targetPosition = entry.position;
			player.targetOrientation = entry.orientation;
			player.interpolating = false;
		}
		else
		{
			// Start a new interpolation from wherever the avatar is right now.
			player.previousPosition = player.position;
			player.previousOrientation = player.orientation;
			player.targetPosition = entry.position;
			player.targetOrientation = entry.orientation;
			player.interpolationTime = 0.0f;
			player.interpolating = true;
		}

		player.hasState = true;
	}

	void Session::HandleChat(const uint8_t* data, size_t size)
	{
		if (data == nullptr || size < sizeof(MsgChat)) return;

		MsgChat chat;
		std::memcpy(&chat, data, sizeof(chat));

		ChatLine line;
		line.senderId = chat.senderId;
		line.text = chat.text;

		if (chat.senderId == 0)
		{
			line.sender = "Server";
			line.system = true;
		}
		else if (chat.senderId == mLocalId)
		{
			line.sender = mLocalName;
		}
		else
		{
			RemotePlayer* player = FindPlayer(chat.senderId);
			line.sender = player != nullptr ? player->name : ("#" + std::to_string(chat.senderId));
		}

		// The server sends join/leave notifications both as a chat line and as a
		// PlayerJoined/PlayerLeft message. Keep the message, drop the duplicated chat line.
		if (line.system && !mChatLog.empty())
		{
			const ChatLine& last = mChatLog.back();
			if (last.system && last.text == line.text)
			{
				return;
			}
		}

		mChatLog.push_back(line);
		while (mChatLog.size() > kMaxChatLogLines)
		{
			mChatLog.pop_front();
		}

		if (chat.senderId != mLocalId)
		{
			PrintToConsole("[" + line.sender + "] " + line.text);
		}
	}

	void Session::HandleKick(const uint8_t* data, size_t size)
	{
		if (data == nullptr || size < sizeof(MsgKick)) return;

		MsgKick kick;
		std::memcpy(&kick, data, sizeof(kick));

		Notify("Removed from the session: " + std::string(kick.message));
		MP_LOG_WARN("Kicked from the session: %s", kick.message);

		mNetwork.Disconnect();
	}

	// ------------------------------------------------------------------------
	// Commands
	// ------------------------------------------------------------------------

	void Session::SendChat(const std::string& text)
	{
		if (!IsConnected())
		{
			Notify("Not connected to a session");
			return;
		}

		MsgChat chat;
		std::memset(&chat, 0, sizeof(chat));
		chat.senderId = mLocalId;

		char sanitized[kMaxChatLength];
		SanitizeText(text.c_str(), sanitized, sizeof(sanitized), "");
		if (sanitized[0] == '\0') return;

		CopyString(chat.text, sizeof(chat.text), sanitized);
		mNetwork.Send(MsgType::Chat, &chat, sizeof(chat));

		// Show the line immediately instead of waiting for the server to echo it.
		ChatLine line;
		line.senderId = mLocalId;
		line.sender = mLocalName;
		line.text = sanitized;
		mChatLog.push_back(line);
		while (mChatLog.size() > kMaxChatLogLines)
		{
			mChatLog.pop_front();
		}
	}

	void Session::SetName(const std::string& name)
	{
		char sanitized[kMaxNameLength];
		SanitizeText(name.c_str(), sanitized, sizeof(sanitized), "");

		if (sanitized[0] == '\0')
		{
			Notify("That name is not valid");
			return;
		}

		mLocalName = sanitized;
		mConfig.name = sanitized;
		SaveClientConfig(mConfigPath, mConfig);

		if (!IsConnected()) return;

		MsgSetName message;
		std::memset(&message, 0, sizeof(message));
		CopyString(message.name, sizeof(message.name), sanitized);
		mNetwork.Send(MsgType::SetName, &message, sizeof(message));
	}

	void Session::AddSystemLine(const std::string& text)
	{
		// Do not repeat the same notification twice in a row.
		if (!mChatLog.empty() && mChatLog.back().system && mChatLog.back().text == text)
		{
			return;
		}

		ChatLine line;
		line.senderId = 0;
		line.sender = "Server";
		line.text = text;
		line.system = true;

		mChatLog.push_back(line);
		while (mChatLog.size() > kMaxChatLogLines)
		{
			mChatLog.pop_front();
		}
	}

	void Session::Notify(const std::string& text)
	{
		AddSystemLine(text);
		PrintToConsole("[Multiplayer] " + text);
		MP_LOG_INFO("%s", text.c_str());
	}

	RemotePlayer* Session::FindPlayer(uint32_t id)
	{
		for (RemotePlayer& player : mPlayers)
		{
			if (player.id == id) return &player;
		}
		return nullptr;
	}
}
