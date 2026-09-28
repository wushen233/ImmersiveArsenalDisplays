#pragma once

#include "ActorRuntimeContext.h"
#include "HolsterManager.h"

namespace IAD {
	using ActorDisplayIdentity = ActorRuntimeIdentity;

	struct ActorDisplaySnapshot {
		ActorDisplayIdentity identity;
		DebugSettings settings;
		std::vector<DebugBox> boxes;
		std::vector<DebugNode> nodes;
		std::vector<DebugBoundSphere> modelBounds;
	};

	// Stable UI-facing snapshot seam. ActorDisplayContext owns the latest copied
	// render/debug snapshot; HolsterManager only assembles and publishes it from
	// the game-thread scene pass. ImGui never reaches live runtime containers.
	class ActorDisplayContext final {
	public:
		static ActorDisplayContext& GetSingleton()
		{
			static ActorDisplayContext instance;
			return instance;
		}

		void Publish(ActorDisplaySnapshot a_snapshot);
		void Acquire(ActorDisplaySnapshot& a_snapshot) const;
		void Acquire(
			DebugSettings& a_settings,
			std::vector<DebugBox>& a_boxes,
			std::vector<DebugNode>& a_nodes,
			std::vector<DebugBoundSphere>& a_modelBounds) const;

	private:
		mutable std::mutex m_mutex;
		ActorDisplaySnapshot m_snapshot;
	};
}
