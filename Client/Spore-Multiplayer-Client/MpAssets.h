/****************************************************************************
* Spore Multiplayer
*
* Sharing the creations of the players.
*
* Spore saves creations as "Model-in-Picture" PNG files: a regular image with
* the creation data hidden inside it. That means the exact creature or ship of
* another player can be transferred as a single PNG and imported with
* Thumbnail_cImportExport::ImportPNG(), which adds it to the Sporepedia only if
* it is not there yet. Nothing that already exists is replaced or modified.
*
* Flow:
*   1. The client finds the PNG of its own avatar (ship/creature) and sends it
*      to the server right after joining.
*   2. The server caches it and forwards it to everybody, and to the players
*      that join later.
*   3. The receivers write the PNG to a temporary folder, import it and use the
*      resulting resource key to spawn the real creation of that player.
****************************************************************************/
#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "../../Shared/MpProtocol.h"

namespace SporeMP
{
	class NetworkClient;

	class AvatarAssets
	{
	public:
		static AvatarAssets& Get();

		void Initialize(const std::string& directory);
		void Dispose();

		/// Sends the PNG of the local avatar if it was not sent yet.
		/// `key` is the resource key of the avatar (ship model or species).
		void UpdateLocalAsset(NetworkClient& network, const ResKey& key, Stage stage);

		// --- incoming transfers ----------------------------------------------

		void HandleInfo(const uint8_t* data, size_t size);
		void HandleChunk(const uint8_t* data, size_t size);
		void HandleDone(const uint8_t* data, size_t size);

		/// Returns the resource key of an imported asset, or false if it is not
		/// available (not received yet, or the import failed).
		bool GetImportedKey(uint32_t assetId, ResKey& dst) const;

		/// Returns the imported version of a key if the creation was received,
		/// otherwise the key itself. Use it before spawning a remote avatar.
		ResKey ResolveKey(const ResKey& key) const;

		/// Number of creations imported so far.
		int GetImportedCount() const { return static_cast<int>(mImported.size()); }

	private:
		AvatarAssets();

		/// Finds the PNG file of a creation in the local Sporepedia folders.
		bool FindLocalPng(const ResKey& key, std::string& path) const;

		bool ReadFile(const std::string& path, std::vector<uint8_t>& dst) const;
		bool WriteFile(const std::string& path, const std::vector<uint8_t>& data) const;

		/// Writes the received PNG and imports it into the Sporepedia.
		/// `key` receives the key of the creation that was actually imported.
		bool ImportAsset(uint32_t assetId, ResKey& key, const std::vector<uint8_t>& data);

		struct Transfer
		{
			uint32_t assetId = 0;
			uint32_t totalSize = 0;
			uint32_t ownerId = 0;
			ResKey key = { 0, 0, 0 };
			std::vector<uint8_t> data;
			bool active = false;
		};

		std::string mAssetDirectory;
		std::string mTempDirectory;

		/// Resource key of the asset that was already sent to the server.
		ResKey mSentKey = { 0, 0, 0 };
		Stage mSentStage = Stage::kNone;

		Transfer mTransfer;
		std::map<uint32_t, ResKey> mImported;
	};
}
