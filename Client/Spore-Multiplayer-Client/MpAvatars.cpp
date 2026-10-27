/****************************************************************************
* Spore Multiplayer
****************************************************************************/
#include "stdafx.h"
#include "MpAvatars.h"
#include "MpLog.h"

namespace SporeMP
{
	namespace
	{
		/// Neutral UFO type: not the player ship, not a pirate, not a turret.
		/// Combined with a cleared behavior list the ship is a passive prop.
		const Simulator::UfoType kRemoteUfoType = Simulator::UfoType::Unk3;

		Math::Vector3 ToVector3(const Vec3f& value)
		{
			return Math::Vector3(value.x, value.y, value.z);
		}

		Math::Quaternion ToQuaternion(const Quatf& value)
		{
			return Math::Quaternion(value.x, value.y, value.z, value.w);
		}

		/// The UFO AI is a list of behaviors; clearing it turns the ship into a
		/// passive object that only moves when we teleport it.
		void DisableUfoBehaviors(Simulator::cGameDataUFO* ufo)
		{
			if (ufo == nullptr) return;

			ufo->mData.clear();
			ufo->mbFixed = true;
			ufo->mNextVelocity = Math::Vector3(0.0f, 0.0f, 0.0f);
			ufo->mbAtDestination = true;
			ufo->mDestination = ufo->mPosition;
		}
	}

	Avatars::Avatars()
	{
	}

	Avatars& Avatars::Get()
	{
		static Avatars instance;
		return instance;
	}

	void Avatars::Update(float deltaTime)
	{
		auto& session = Session::Get();

		if (!session.IsConnected() || !session.Config().showAvatars)
		{
			Clear();
			return;
		}

		// The stage and the world are global for the local player, so they are
		// read once per frame instead of once per player.
		const Stage localStage = session.GetLocalStage();
		const uint32_t localSystem = session.GetLocalSystemId();

		for (auto& pair : mSlots)
		{
			pair.second.seen = false;
		}

		for (const RemotePlayer& player : session.GetPlayers())
		{
			if (!player.hasState) continue;
			if (player.stage != localStage) continue;
			if (player.systemId != localSystem) continue;

			Slot& slot = GetOrCreateSlot(player.id);

			if (!IsSlotValid(slot) || slot.stage != player.stage)
			{
				DestroySlot(slot);

				bool spawned = false;
				if (player.stage == Stage::kSpace)
				{
					spawned = SpawnSpaceAvatar(slot, player);
				}
				else if (player.stage == Stage::kCreature)
				{
					spawned = SpawnCreatureAvatar(slot, player);
				}

				if (!spawned)
				{
					slot.seen = false;
					continue;
				}

				slot.stage = player.stage;
				slot.systemId = player.systemId;
			}

			if (player.stage == Stage::kSpace)
			{
				MoveSpaceAvatar(slot, player);
			}
			else if (player.stage == Stage::kCreature)
			{
				MoveCreatureAvatar(slot, player);
			}

			slot.seen = true;
		}

		// Despawn the avatars that are no longer visible (player left, changed
		// stage or changed solar system).
		for (auto it = mSlots.begin(); it != mSlots.end();)
		{
			if (!it->second.seen)
			{
				DestroySlot(it->second);
				it = mSlots.erase(it);
			}
			else
			{
				++it;
			}
		}
	}

	void Avatars::Clear()
	{
		for (auto& pair : mSlots)
		{
			DestroySlot(pair.second);
		}
		mSlots.clear();
	}

	Avatars::Slot& Avatars::GetOrCreateSlot(uint32_t playerId)
	{
		return mSlots[playerId];
	}

	bool Avatars::IsSlotValid(const Slot& slot) const
	{
		if (slot.ufo != nullptr)
		{
			return !slot.ufo->mbIsDestroyed;
		}
		if (slot.herd != nullptr)
		{
			return !slot.herd->mbIsDestroyed && !slot.herd->mHerd.empty();
		}
		return false;
	}

	void Avatars::DestroySlot(Slot& slot)
	{
		if (slot.ufo != nullptr)
		{
			if (!slot.ufo->mbIsDestroyed)
			{
				GameNounManager.DestroyInstance(slot.ufo.get());
			}
			slot.ufo = nullptr;
		}

		if (slot.herd != nullptr)
		{
			if (!slot.herd->mbIsDestroyed)
			{
				GameNounManager.DestroyInstance(slot.herd.get());
			}
			slot.herd = nullptr;
		}

		slot.stage = Stage::kNone;
		slot.systemId = 0;
		slot.seen = false;
	}

	bool Avatars::SpawnSpaceAvatar(Slot& slot, const RemotePlayer& player)
	{
		const uint32_t empireId = Simulator::GetPlayerEmpireID();

		ResourceKey modelKey;
		if (ResKeyIsValid(player.avatarKey))
		{
			modelKey = ResourceKey(player.avatarKey.instance, player.avatarKey.type, player.avatarKey.group);
		}
		else
		{
			// Fall back to the ship of the local player.
			auto* localUfo = Simulator::GetPlayerUFO();
			if (localUfo != nullptr)
			{
				modelKey = localUfo->GetModelKey();
			}
		}

		auto* ufo = Simulator::SpawnUFO(kRemoteUfoType, empireId, modelKey);
		if (ufo == nullptr)
		{
			MP_LOG_WARN("Could not spawn the ship of player %u", player.id);
			return false;
		}

		DisableUfoBehaviors(ufo);
		ufo->Teleport(ToVector3(player.position), ToQuaternion(player.orientation));
		ufo->SetScale(ufo->GetScale());

		slot.ufo = ufo;

		MP_LOG_INFO("Spawned the ship of player %u ('%s')", player.id, player.name.c_str());
		return true;
	}

	bool Avatars::SpawnCreatureAvatar(Slot& slot, const RemotePlayer& player)
	{
		Simulator::cSpeciesProfile* profile = nullptr;
		if (ResKeyIsValid(player.avatarKey))
		{
			profile = SpeciesManager.GetSpeciesProfile(
				ResourceKey(player.avatarKey.instance, player.avatarKey.type, player.avatarKey.group));
		}

		if (profile == nullptr)
		{
			auto* avatar = GameNounManager.GetAvatar();
			if (avatar != nullptr)
			{
				profile = avatar->mpSpeciesProfile;
			}
		}

		if (profile == nullptr)
		{
			MP_LOG_WARN("Could not find a species profile for player %u", player.id);
			return false;
		}

		auto* herd = GameNounManager.CreateHerd(
			ToVector3(player.position), profile, 1, false, 0, false);

		if (herd == nullptr || herd->mHerd.empty())
		{
			MP_LOG_WARN("Could not spawn the creature of player %u", player.id);
			return false;
		}

		herd->mbShouldEvolve = false;
		herd->mTargetHerdSize = 1;

		auto* creature = herd->mHerd[0].get();
		if (creature != nullptr)
		{
			creature->Teleport(ToVector3(player.position), ToQuaternion(player.orientation));
		}

		slot.herd = herd;

		MP_LOG_INFO("Spawned the creature of player %u ('%s')", player.id, player.name.c_str());
		return true;
	}

	void Avatars::MoveSpaceAvatar(Slot& slot, const RemotePlayer& player)
	{
		auto* ufo = slot.ufo.get();
		if (ufo == nullptr) return;

		// Teleport() recalculates the physics, so the ship keeps the exact
		// position even though it is not driven by the AI.
		ufo->Teleport(ToVector3(player.position), ToQuaternion(player.orientation));
	}

	void Avatars::MoveCreatureAvatar(Slot& slot, const RemotePlayer& player)
	{
		auto* herd = slot.herd.get();
		if (herd == nullptr || herd->mHerd.empty()) return;

		auto* creature = herd->mHerd[0].get();
		if (creature == nullptr) return;

		creature->Teleport(ToVector3(player.position), ToQuaternion(player.orientation));
	}
}
