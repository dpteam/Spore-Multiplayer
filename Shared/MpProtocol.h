/****************************************************************************
* Spore Multiplayer
*
* Shared network protocol between the client mod and the dedicated server.
*
* The protocol is a small binary, message oriented protocol on top of TCP.
* Every message is framed as:
*
*     [uint16 type][uint16 payloadSize][payloadSize bytes of payload]
*
* Both ends are 32 bit little endian Windows processes, so the structures below
* are sent in memory order (packed) without any further byte swapping.
*
* This header must stay dependency free: it is compiled both by the Spore mod
* (which uses the ModAPI SDK / EASTL) and by the standalone server.
****************************************************************************/
#pragma once

#include <cstdint>
#include <cstddef>
#include <cstring>

namespace SporeMP
{
	/// Version of the wire protocol. Both sides must use the same value.
	constexpr uint16_t kProtocolVersion = 1;

	/// Human readable name, used in log messages.
	constexpr const char* kProtocolName = "Spore Multiplayer";

	/// Maximum number of players a server will accept in a single session.
	constexpr int kMaxPlayers = 8;

	/// Maximum length of a player name, including the null terminator.
	constexpr int kMaxNameLength = 24;

	/// Maximum length of a chat message, including the null terminator.
	constexpr int kMaxChatLength = 160;

	/// Maximum length of the session name, including the null terminator.
	constexpr int kMaxSessionNameLength = 32;

	/// Maximum length of the text of reject/kick messages, including the null terminator.
	constexpr int kMaxReasonTextLength = 128;

	/// Size of the message header: uint16 type + uint16 size.
	constexpr size_t kHeaderSize = 4;

	/// Largest payload we are willing to accept. Anything bigger is a protocol error.
	constexpr size_t kMaxPayloadSize = 2048;

	/// Largest creation PNG we transfer. Creature PNGs are usually 30-150 KB.
	constexpr size_t kMaxAssetSize = 512 * 1024;

	/// Bytes of creation data sent in a single AssetChunk message.
	constexpr size_t kAssetChunkSize = 1024;

	// ------------------------------------------------------------------------
	// Message types
	// ------------------------------------------------------------------------

	enum class MsgType : uint16_t
	{
		/// Client -> server, first message after connecting.
		Hello = 1,
		/// Server -> client, the client was accepted into the session.
		Welcome = 2,
		/// Server -> client, the connection will be closed.
		Reject = 3,
		/// Server -> client, full list of players already in the session.
		PlayerList = 4,
		/// Server -> client, someone joined the session.
		PlayerJoined = 5,
		/// Server -> client, someone left the session.
		PlayerLeft = 6,
		/// Server -> client, a player changed its name, stage or ship.
		PlayerUpdated = 7,
		/// Client -> server, the local player state (position, stage...).
		LocalState = 8,
		/// Server -> client, the states of the other players.
		PlayerStates = 9,
		/// Both directions, a chat line. senderId == 0 means "server".
		Chat = 10,
		/// Client -> server, latency measurement.
		Ping = 11,
		/// Server -> client, answer to Ping.
		Pong = 12,
		/// Client -> server, change the player name.
		SetName = 13,
		/// Server -> client, the player was kicked and the connection will be closed.
		Kick = 14,
		/// Both directions, starts the transfer of a creation PNG (avatar asset).
		AssetInfo = 15,
		/// Both directions, a piece of a creation PNG.
		AssetChunk = 16,
		/// Both directions, the transfer of a creation PNG is finished.
		AssetDone = 17,
	};

	/// Why a connection was refused/closed by the server.
	enum class RejectReason : uint8_t
	{
		None = 0,
		ProtocolMismatch = 1,
		ServerFull = 2,
		InvalidName = 3,
		Timeout = 4,
		ShuttingDown = 5,
		Kicked = 6,
		BadMessage = 7,
	};

	/// The game stage the player is currently in. Used to know which objects can be
	/// represented and to avoid syncing players that are in different stages.
	/// The values are prefixed with `k` because the ModAPI SDK defines macros such as
	/// `Editor`, which would otherwise break this enumeration.
	enum class Stage : uint8_t
	{
		kNone = 0,
		kCell = 1,
		kCreature = 2,
		kTribe = 3,
		kCivilization = 4,
		kSpace = 5,
		kEditor = 6,
		kScenario = 7,
	};

	/// Flags of a state entry.
	enum StateFlags : uint8_t
	{
		/// The position/orientation fields contain valid data.
		kStateHasPosition = 1 << 0,
	};

	// ------------------------------------------------------------------------
	// Structures
	//
	// Everything below is packed: the same bytes are sent over the network.
	// ------------------------------------------------------------------------

#pragma pack(push, 1)

	struct Vec3f
	{
		float x;
		float y;
		float z;
	};

	struct Quatf
	{
		float x;
		float y;
		float z;
		float w;
	};

	/// Same layout as Spore::ResourceKey (instanceID, typeID, groupID).
	struct ResKey
	{
		uint32_t instance;
		uint32_t type;
		uint32_t group;
	};

	/// Information about a player that is shared with the other clients.
	struct PlayerInfo
	{
		uint32_t id;
		char name[kMaxNameLength];
		Stage stage;
		uint8_t reserved;
		ResKey shipModel;
	};

	/// Position/orientation of a player at a given moment.
	struct StateEntry
	{
		uint32_t id;
		Vec3f position;
		Quatf orientation;
		uint8_t stage;
		uint8_t flags;
		uint16_t reserved;
		/// Identifies the star system / world the player is in, 0 if unknown.
		uint32_t systemId;
	};

	struct MsgHello
	{
		uint16_t protocolVersion;
		uint16_t clientVersion;
		uint32_t clientNonce;
		char name[kMaxNameLength];
		Stage stage;
		uint8_t reserved;
		ResKey shipModel;
	};

	struct MsgWelcome
	{
		uint32_t playerId;
		uint16_t tickRate;
		uint16_t maxPlayers;
		char sessionName[kMaxSessionNameLength];
		char assignedName[kMaxNameLength];
	};

	struct MsgReject
	{
		RejectReason reason;
		uint8_t reserved;
		char message[kMaxReasonTextLength];
	};

	struct MsgPlayerJoined
	{
		PlayerInfo player;
	};

	struct MsgPlayerLeft
	{
		uint32_t id;
		RejectReason reason;
		uint8_t reserved[3];
	};

	struct MsgPlayerUpdated
	{
		PlayerInfo player;
	};

	struct MsgLocalState
	{
		Vec3f position;
		Quatf orientation;
		Stage stage;
		uint8_t flags;
		uint16_t reserved;
		/// Identifies the star system / world the player is in, 0 if unknown.
		uint32_t systemId;
	};

	/// Header of the PlayerStates message; the payload is followed by `count` StateEntry.
	struct MsgPlayerStatesHeader
	{
		uint8_t count;
		uint8_t reserved[3];
	};

	/// Header of the PlayerList message; the payload is followed by `count` PlayerInfo.
	struct MsgPlayerListHeader
	{
		uint8_t count;
		uint8_t reserved[3];
	};

	struct MsgChat
	{
		uint32_t senderId;
		char text[kMaxChatLength];
	};

	struct MsgPing
	{
		uint32_t clientTime;
	};

	struct MsgPong
	{
		uint32_t clientTime;
		uint32_t serverTime;
	};

	struct MsgSetName
	{
		char name[kMaxNameLength];
	};

	struct MsgKick
	{
		RejectReason reason;
		uint8_t reserved;
		char message[kMaxReasonTextLength];
	};

	// ------------------------------------------------------------------------
	// Avatar assets (creation PNGs)
	//
	// Spore stores creations as "Model-in-Picture" PNG files: a normal image with
	// the creation data hidden inside it. Sending that file to the other players
	// is enough for them to import the exact creature/ship that is being used,
	// without replacing anything that already exists in their Sporepedia.
	// ------------------------------------------------------------------------

	struct MsgAssetInfo
	{
		/// Identifies the transfer; it is the hash of the resource key.
		uint32_t assetId;
		uint32_t totalSize;
		/// Player that provides the asset.
		uint32_t ownerId;
		ResKey key;
	};

	struct MsgAssetChunk
	{
		uint32_t assetId;
		uint32_t offset;
		uint16_t size;
		uint16_t reserved;
	};

	struct MsgAssetDone
	{
		uint32_t assetId;
		uint8_t success;
		uint8_t reserved[3];
	};

#pragma pack(pop)

	// ------------------------------------------------------------------------
	// Helpers
	// ------------------------------------------------------------------------

	inline size_t PlayerStatesPayloadSize(int count)
	{
		return sizeof(MsgPlayerStatesHeader) + sizeof(StateEntry) * static_cast<size_t>(count);
	}

	inline size_t PlayerListPayloadSize(int count)
	{
		return sizeof(MsgPlayerListHeader) + sizeof(PlayerInfo) * static_cast<size_t>(count);
	}

	inline StateEntry* PlayerStatesEntries(MsgPlayerStatesHeader* header)
	{
		return reinterpret_cast<StateEntry*>(header + 1);
	}

	inline const StateEntry* PlayerStatesEntries(const MsgPlayerStatesHeader* header)
	{
		return reinterpret_cast<const StateEntry*>(header + 1);
	}

	inline PlayerInfo* PlayerListEntries(MsgPlayerListHeader* header)
	{
		return reinterpret_cast<PlayerInfo*>(header + 1);
	}

	inline const PlayerInfo* PlayerListEntries(const MsgPlayerListHeader* header)
	{
		return reinterpret_cast<const PlayerInfo*>(header + 1);
	}

	/// Copies a string into a fixed size buffer, always null terminating.
	inline void CopyString(char* dst, size_t dstSize, const char* src)
	{
		if (dstSize == 0) return;

		size_t i = 0;
		if (src != nullptr)
		{
			for (; i + 1 < dstSize && src[i] != '\0'; ++i)
			{
				dst[i] = src[i];
			}
		}
		dst[i] = '\0';
	}

	/// Returns true if the character is allowed in player names and chat messages.
	inline bool IsAllowedChar(char c)	{
		const unsigned char u = static_cast<unsigned char>(c);
		// Printable ASCII only. Control characters are never allowed because they could be
		// used to inject ArgScript commands in the in game console.
		return u >= 0x20 && u < 0x7F;
	}

	/// Removes control characters and duplicated/leading/trailing spaces.
	/// Text that becomes empty is replaced with `fallback`.
	inline void SanitizeText(const char* src, char* dst, size_t dstSize, const char* fallback)
	{
		if (dstSize == 0) return;

		size_t out = 0;
		bool lastWasSpace = true;

		if (src != nullptr)
		{
			for (size_t i = 0; src[i] != '\0' && out + 1 < dstSize; ++i)
			{
				char c = src[i];
				if (!IsAllowedChar(c)) c = ' ';

				if (c == ' ')
				{
					if (lastWasSpace) continue;
					lastWasSpace = true;
				}
				else
				{
					lastWasSpace = false;
				}
				dst[out++] = c;
			}
		}

		while (out > 0 && dst[out - 1] == ' ')
		{
			--out;
		}
		dst[out] = '\0';

		if (out == 0)
		{
			CopyString(dst, dstSize, fallback != nullptr ? fallback : "Player");
		}
	}

	inline bool ResKeyIsValid(const ResKey& key)
	{
		return key.instance != 0;
	}

	/// FNV-1a hash of a resource key, used as the asset transfer id.
	inline uint32_t HashResKey(const ResKey& key)
	{
		uint32_t hash = 2166136261u;
		const uint32_t values[3] = { key.instance, key.type, key.group };
		for (int i = 0; i < 3; ++i)
		{
			hash ^= values[i];
			hash *= 16777619u;
		}
		return hash;
	}

	/// Human readable name of a stage, used in the HUD and the console.
	inline const char* StageName(Stage stage)
	{
		switch (stage)
		{
		case Stage::kCell: return "Cell";
		case Stage::kCreature: return "Creature";
		case Stage::kTribe: return "Tribe";
		case Stage::kCivilization: return "Civilization";
		case Stage::kSpace: return "Space";
		case Stage::kEditor: return "Editor";
		case Stage::kScenario: return "Scenario";
		default: return "None";
		}
	}

	// ------------------------------------------------------------------------
	// Compile time checks: the structures travel over the wire in memory order.
	// ------------------------------------------------------------------------

	static_assert(sizeof(Vec3f) == 12, "Unexpected Vec3f size");
	static_assert(sizeof(Quatf) == 16, "Unexpected Quatf size");
	static_assert(sizeof(ResKey) == 12, "Unexpected ResKey size");
	static_assert(sizeof(PlayerInfo) == 42, "Unexpected PlayerInfo size");
	static_assert(sizeof(StateEntry) == 40, "Unexpected StateEntry size");
	static_assert(sizeof(MsgHello) == 46, "Unexpected MsgHello size");
	static_assert(sizeof(MsgWelcome) == 64, "Unexpected MsgWelcome size");
	static_assert(sizeof(MsgReject) == 130, "Unexpected MsgReject size");
	static_assert(sizeof(MsgPlayerLeft) == 8, "Unexpected MsgPlayerLeft size");
	static_assert(sizeof(MsgLocalState) == 36, "Unexpected MsgLocalState size");
	static_assert(sizeof(MsgChat) == 164, "Unexpected MsgChat size");
	static_assert(sizeof(MsgPong) == 8, "Unexpected MsgPong size");
	static_assert(sizeof(MsgSetName) == 24, "Unexpected MsgSetName size");
	static_assert(sizeof(MsgKick) == 130, "Unexpected MsgKick size");
}
