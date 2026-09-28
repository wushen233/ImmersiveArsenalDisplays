#include "pch.h"

#include "ActorDisplayContext.h"

#include <utility>

namespace IAD {
	void ActorDisplayContext::Publish(ActorDisplaySnapshot a_snapshot)
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		const auto nextPublishSequence = m_snapshot.identity.publishSequence + 1;
		a_snapshot.identity.publishSequence = nextPublishSequence;
		m_snapshot = std::move(a_snapshot);
	}

	void ActorDisplayContext::Acquire(ActorDisplaySnapshot& a_snapshot) const
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		a_snapshot = m_snapshot;
	}

	void ActorDisplayContext::Acquire(
		DebugSettings& a_settings,
		std::vector<DebugBox>& a_boxes,
		std::vector<DebugNode>& a_nodes,
		std::vector<DebugBoundSphere>& a_modelBounds) const
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		a_settings = m_snapshot.settings;
		a_boxes = m_snapshot.boxes;
		a_nodes = m_snapshot.nodes;
		a_modelBounds = m_snapshot.modelBounds;
	}
}
