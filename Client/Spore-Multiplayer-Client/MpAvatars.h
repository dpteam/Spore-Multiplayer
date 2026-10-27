/****************************************************************************
* Spore Multiplayer
*
* Spawns and moves the visual representation of the other players:
*
*   - Space stage:  a ship (cGameDataUFO) owned by the player empire
*   - Creature stage: a creature of the same species as the local avatar
*
* The avatars are purely visual: their AI is disabled and they are teleported
* to the interpolated position received from the server. They never simulate
* anything, so they can not interfere with the local game state.
****************************************************************************/
#pragma once

#include <cstdint>
#include <map>

#include <Spore\BasicIncludes.h>

#include "MpSession.h"

namespace SporeMP
{
	class Avatars
	{
	public:
		static Avatars& Get();

		/// Spawns, moves and destroys the remote avatars. Called every frame.
		void Update(float deltaTime);

		/// Destroys every remote avatar.
		void Clear();

		/// Number of avatars currently spawned, for the HUD and the status command.
		int GetActiveCount() const { return static_cast<int>(mSlots.size()); }

	private:
		Avatars();

		struct Slot
		{
			Stage stage = Stage::kNone;
			uint32_t systemId = 0;
			eastl::intrusive_ptr<Simulator::cGameDataUFO> ufo;
			eastl::intrusive_ptr<Simulator::cHerd> herd;
			bool seen = false;
		};

		Slot& GetOrCreateSlot(uint32_t playerId);
		bool IsSlotValid(const Slot& slot) const;
		void DestroySlot(Slot& slot);

		bool SpawnSpaceAvatar(Slot& slot, const RemotePlayer& player);
		bool SpawnCreatureAvatar(Slot& slot, const RemotePlayer& player);
		void MoveSpaceAvatar(Slot& slot, const RemotePlayer& player);
		void MoveCreatureAvatar(Slot& slot, const RemotePlayer& player);

		std::map<uint32_t, Slot> mSlots;
	};
}
