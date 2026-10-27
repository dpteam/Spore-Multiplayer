/****************************************************************************
* Spore Multiplayer
****************************************************************************/
#include "stdafx.h"
#include "MpAssets.h"
#include "MpLog.h"
#include "MpNet.h"

#include <algorithm>
#include <cstdio>
#include <fstream>

#include <Spore\App\Thumbnail_cImportExport.h>
#include <Spore\Editors\SpeciesManager.h>

namespace SporeMP
{
	namespace
	{
		/// Converts a resource key to the key type used by the game.
		ResourceKey ToGameKey(const ResKey& key)
		{
			return ResourceKey(key.instance, key.type, key.group);
		}

		ResKey FromGameKey(const ResourceKey& key)
		{
			ResKey result;
			result.instance = key.instanceID;
			result.type = key.typeID;
			result.group = key.groupID;
			return result;
		}

		std::string ToUtf8(const eastl::string16& text)
		{
			std::string result;
			result.reserve(text.size());

			for (size_t i = 0; i < text.size(); ++i)
			{
				const char16_t c = text[i];
				if (c < 0x80)
				{
					result += static_cast<char>(c);
				}
				else
				{
					// Creations live in the user profile, whose path is usually ASCII.
					// Non ASCII characters are replaced instead of breaking the path.
					result += '?';
				}
			}

			return result;
		}
	}

	AvatarAssets::AvatarAssets()
	{
	}

	AvatarAssets& AvatarAssets::Get()
	{
		static AvatarAssets instance;
		return instance;
	}

	void AvatarAssets::Initialize(const std::string& directory)
	{
		mAssetDirectory = directory;

		// Received creations are written to a temporary folder: the "My Spore
		// Creations" folder of the user is never touched.
		char tempPath[MAX_PATH] = { 0 };
		const DWORD length = GetTempPathA(MAX_PATH, tempPath);
		if (length > 0 && length < MAX_PATH)
		{
			mTempDirectory = std::string(tempPath) + "SporeMultiplayer\\";
			CreateDirectoryA(mTempDirectory.c_str(), nullptr);
		}
		else
		{
			mTempDirectory = mAssetDirectory;
		}

		MP_LOG_INFO("Avatar assets are stored in %s", mTempDirectory.c_str());
	}

	void AvatarAssets::Dispose()
	{
		mTransfer = Transfer();
		mImported.clear();
		mSentKey = ResKey{ 0, 0, 0 };
		mSentStage = Stage::kNone;
	}

	// ------------------------------------------------------------------------
	// Sending
	// ------------------------------------------------------------------------

	void AvatarAssets::UpdateLocalAsset(NetworkClient& network, const ResKey& key, Stage stage)
	{
		if (!ResKeyIsValid(key)) return;

		// Send it once per avatar: when the player changes the ship or the species,
		// or when it moves to another stage that uses a different creation.
		if (mSentKey.instance == key.instance && mSentKey.type == key.type &&
			mSentKey.group == key.group && mSentStage == stage)
		{
			return;
		}

		std::string path;
		if (!FindLocalPng(key, path))
		{
			MP_LOG_DEBUG("No PNG found for the local avatar, it will not be shared");
			mSentKey = key;
			mSentStage = stage;
			return;
		}

		std::vector<uint8_t> data;
		if (!ReadFile(path, data) || data.empty() || data.size() > kMaxAssetSize)
		{
			MP_LOG_WARN("Could not read the avatar PNG (%s)", path.c_str());
			return;
		}

		const uint32_t assetId = HashResKey(key);

		MsgAssetInfo info;
		std::memset(&info, 0, sizeof(info));
		info.assetId = assetId;
		info.totalSize = static_cast<uint32_t>(data.size());
		info.ownerId = 0;  // the server fills it
		info.key = key;
		network.Send(MsgType::AssetInfo, &info, sizeof(info));

		for (size_t offset = 0; offset < data.size(); offset += kAssetChunkSize)
		{
			const size_t count = (std::min)(kAssetChunkSize, data.size() - offset);

			MsgAssetChunk chunk;
			std::memset(&chunk, 0, sizeof(chunk));
			chunk.assetId = assetId;
			chunk.offset = static_cast<uint32_t>(offset);
			chunk.size = static_cast<uint16_t>(count);

			std::vector<uint8_t> frame(sizeof(chunk) + count);
			std::memcpy(frame.data(), &chunk, sizeof(chunk));
			std::memcpy(frame.data() + sizeof(chunk), data.data() + offset, count);

			network.Send(MsgType::AssetChunk, frame.data(), frame.size());
		}

		MsgAssetDone done;
		std::memset(&done, 0, sizeof(done));
		done.assetId = assetId;
		done.success = 1;
		network.Send(MsgType::AssetDone, &done, sizeof(done));

		mSentKey = key;
		mSentStage = stage;

		MP_LOG_INFO("Sharing the local avatar creation (%zu bytes, %s)", data.size(), path.c_str());
	}

	bool AvatarAssets::FindLocalPng(const ResKey& key, std::string& path) const
	{
		auto* importExport = App::Thumbnail_cImportExport::Get();
		if (importExport == nullptr) return false;

		// The game keeps the relation between a creation key and its PNG file.
		auto it = importExport->mKeyToPngPath.find(ToGameKey(key));
		if (it != importExport->mKeyToPngPath.end())
		{
			path = ToUtf8(it->second);
			if (!path.empty()) return true;
		}

		// Fallback: scan the creations folder for a PNG whose name contains the
		// instance ID of the creation. Spore names them
		// "<name>.<group>.<instance>.png" (or with the IDs swapped), so the
		// instance is enough to identify the file.
		const uint32_t creationType = (key.type == TypeIDs::ufo) ? TypeIDs::ufo : TypeIDs::crt;

		eastl::string16 folder;
		if (!importExport->GetFolderPath(creationType, folder))
		{
			return false;
		}

		char pattern[MAX_PATH];
		std::snprintf(pattern, sizeof(pattern), "0x%08x", key.instance);

		std::string lowerPattern(pattern);
		for (char& c : lowerPattern)
		{
			if (c >= 'A' && c <= 'F') c = static_cast<char>(c - 'A' + 'a');
		}

		WIN32_FIND_DATAA findData;
		const std::string searchPath = ToUtf8(folder) + "*.png";
		HANDLE handle = FindFirstFileA(searchPath.c_str(), &findData);
		if (handle == INVALID_HANDLE_VALUE) return false;

		bool found = false;
		do
		{
			std::string name(findData.cFileName);
			for (char& c : name)
			{
				if (c >= 'A' && c <= 'F') c = static_cast<char>(c - 'A' + 'a');
			}

			if (name.find(lowerPattern) != std::string::npos)
			{
				path = ToUtf8(folder) + findData.cFileName;
				found = true;
				break;
			}
		} while (FindNextFileA(handle, &findData));

		FindClose(handle);
		return found;
	}

	bool AvatarAssets::ReadFile(const std::string& path, std::vector<uint8_t>& dst) const
	{
		std::ifstream file(path.c_str(), std::ios::binary | std::ios::ate);
		if (!file.is_open()) return false;

		const std::streamsize size = file.tellg();
		if (size <= 0) return false;

		file.seekg(0, std::ios::beg);
		dst.resize(static_cast<size_t>(size));

		if (!file.read(reinterpret_cast<char*>(dst.data()), size)) return false;
		return true;
	}

	bool AvatarAssets::WriteFile(const std::string& path, const std::vector<uint8_t>& data) const
	{
		std::ofstream file(path.c_str(), std::ios::binary | std::ios::trunc);
		if (!file.is_open()) return false;

		file.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
		return file.good();
	}

	// ------------------------------------------------------------------------
	// Receiving
	// ------------------------------------------------------------------------

	void AvatarAssets::HandleInfo(const uint8_t* data, size_t size)
	{
		if (data == nullptr || size < sizeof(MsgAssetInfo)) return;

		MsgAssetInfo info;
		std::memcpy(&info, data, sizeof(info));

		if (info.totalSize == 0 || info.totalSize > kMaxAssetSize)
		{
			MP_LOG_WARN("Received an avatar asset with an invalid size (%u)", info.totalSize);
			return;
		}

		// Already imported: nothing to do.
		if (mImported.find(info.assetId) != mImported.end()) return;

		mTransfer = Transfer();
		mTransfer.assetId = info.assetId;
		mTransfer.totalSize = info.totalSize;
		mTransfer.ownerId = info.ownerId;
		mTransfer.key = info.key;
		mTransfer.data.reserve(info.totalSize);
		mTransfer.active = true;

		MP_LOG_DEBUG("Receiving avatar asset %08x from player %u (%u bytes)",
			info.assetId, info.ownerId, info.totalSize);
	}

	void AvatarAssets::HandleChunk(const uint8_t* data, size_t size)
	{
		if (!mTransfer.active) return;
		if (data == nullptr || size < sizeof(MsgAssetChunk)) return;

		MsgAssetChunk chunk;
		std::memcpy(&chunk, data, sizeof(chunk));

		if (chunk.assetId != mTransfer.assetId) return;

		const size_t count = size - sizeof(MsgAssetChunk);
		if (chunk.size != count) return;
		if (chunk.offset + count > mTransfer.totalSize) return;

		if (mTransfer.data.size() < chunk.offset + count)
		{
			mTransfer.data.resize(chunk.offset + count);
		}

		std::memcpy(mTransfer.data.data() + chunk.offset, data + sizeof(MsgAssetChunk), count);
	}

	void AvatarAssets::HandleDone(const uint8_t* data, size_t size)
	{
		if (!mTransfer.active) return;
		if (data == nullptr || size < sizeof(MsgAssetDone)) return;

		MsgAssetDone done;
		std::memcpy(&done, data, sizeof(done));

		if (done.assetId != mTransfer.assetId) return;

		Transfer transfer = mTransfer;
		mTransfer = Transfer();

		if (!done.success || transfer.data.size() != transfer.totalSize)
		{
			MP_LOG_WARN("Avatar asset %08x was not received completely", transfer.assetId);
			return;
		}

		ResKey key = transfer.key;
		if (!ImportAsset(transfer.assetId, key, transfer.data))
		{
			MP_LOG_WARN("Could not import the avatar asset %08x", transfer.assetId);
			return;
		}

		mImported[transfer.assetId] = key;
		MP_LOG_INFO("Imported the avatar creation of player %u (key %08x)",
			transfer.ownerId, key.instance);
	}

	bool AvatarAssets::ImportAsset(uint32_t assetId, ResKey& key, const std::vector<uint8_t>& data)
	{
		auto* importExport = App::Thumbnail_cImportExport::Get();
		if (importExport == nullptr) return false;

		// The creation is written to a temporary folder, never to the folder of
		// the user, so nothing that already exists can be overwritten.
		char fileName[64];
		std::snprintf(fileName, sizeof(fileName), "mp_%08x.png", assetId);
		const std::string path = mTempDirectory + fileName;

		if (!WriteFile(path, data))
		{
			MP_LOG_WARN("Could not write %s", path.c_str());
			return false;
		}

		const eastl::string16 path16 = ConvertToString16(path.c_str());

		ResourceKey importedKey = ToGameKey(key);
		if (!importExport->ImportPNG(path16.c_str(), importedKey))
		{
			return false;
		}

		// ImportPNG assigns the key of the creation that was actually imported,
		// which may differ from the key we expected.
		ResKey result = FromGameKey(importedKey);
		if (ResKeyIsValid(result))
		{
			key = result;
		}

		return true;
	}

	bool AvatarAssets::GetImportedKey(uint32_t assetId, ResKey& dst) const
	{
		auto it = mImported.find(assetId);
		if (it == mImported.end()) return false;

		dst = it->second;
		return true;
	}

	ResKey AvatarAssets::ResolveKey(const ResKey& key) const
	{
		ResKey imported;
		if (GetImportedKey(HashResKey(key), imported))
		{
			return imported;
		}
		return key;
	}
}
