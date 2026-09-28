#pragma once

#include "Data/ConfigManager.h"

#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

namespace IAD {
	struct ActorRuntimeIdentity {
		RE::TESFormID actorFormID = 0;
		std::uint64_t sceneGeneration = 0;
		std::uint64_t actor3DGeneration = 0;
		std::uint64_t publishSequence = 0;

		bool SameScene(const ActorRuntimeIdentity& a_other) const noexcept
		{
			return actorFormID == a_other.actorFormID &&
				sceneGeneration == a_other.sceneGeneration &&
				actor3DGeneration == a_other.actor3DGeneration;
		}
	};

	struct ActorRuntimeItemSnapshot {
		RE::TESFormID formID = 0;
		std::uint32_t stackID = 0;
		std::uint64_t uid = 0;
		std::uint32_t count = 0;
		bool isEquipped = false;
		bool isFavorited = false;
		bool ratingUsesInstanceData = false;
		float rating = 0.0f;
	};

	struct ActorRuntimeNodeSnapshot {
		std::string nodeName;
		bool isHidden = false;
		bool isAbsolute = false;
		TransformData finalTransform;
		PhysicsValues activePhys;
		std::vector<std::string> targetBones;
	};

	// A copied, data-only view of the inputs used by one actor evaluation. Form
	// and stack pointers are intentionally reduced to stable IDs; the native
	// objects remain owned and resolved by the game-thread adapter.
	struct ActorRuntimeSnapshot {
		ActorRuntimeIdentity identity;
		RuntimeSettingsSnapshot runtimeSettings;
		bool valid = false;
		bool isPlayer = false;
		bool isFemale = false;
		bool actor3DReady = false;

		std::vector<ActorRuntimeItemSnapshot> candidateItems;
		std::vector<ScopedData<SlotDefinition>> scopedSlots;
		std::vector<ScopedData<CustomDefinition>> scopedCustoms;
		std::vector<ActorRuntimeNodeSnapshot> nodeStates;
	};

	// The game-thread producer publishes a coherent actor input snapshot. Other
	// modules can acquire it only after matching actor and scene identity, so a
	// replaced FO4 3D tree cannot be mistaken for the previous evaluation.
	class ActorRuntimeContext final {
	public:
		static ActorRuntimeContext& GetSingleton()
		{
			static ActorRuntimeContext instance;
			return instance;
		}

		void Publish(ActorRuntimeSnapshot a_snapshot);
		bool Acquire(ActorRuntimeSnapshot& a_snapshot) const;
		bool AcquireFor(const ActorRuntimeIdentity& a_identity, ActorRuntimeSnapshot& a_snapshot) const;
		void Invalidate(RE::TESFormID a_actorFormID);

	private:
		mutable std::mutex m_mutex;
		ActorRuntimeSnapshot m_snapshot;
	};
}
