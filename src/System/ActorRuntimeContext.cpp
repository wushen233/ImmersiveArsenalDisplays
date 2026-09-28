#include "pch.h"

#include "ActorRuntimeContext.h"

#include <utility>

namespace IAD {
	void ActorRuntimeContext::Publish(ActorRuntimeSnapshot a_snapshot)
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		const auto nextPublishSequence = m_snapshot.identity.publishSequence + 1;
		a_snapshot.valid = true;
		a_snapshot.identity.publishSequence = nextPublishSequence;
		m_snapshot = std::move(a_snapshot);
	}

	bool ActorRuntimeContext::Acquire(ActorRuntimeSnapshot& a_snapshot) const
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		if (!m_snapshot.valid) {
			return false;
		}
		a_snapshot = m_snapshot;
		return true;
	}

	bool ActorRuntimeContext::AcquireFor(
		const ActorRuntimeIdentity& a_identity,
		ActorRuntimeSnapshot& a_snapshot) const
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		if (!m_snapshot.valid || !m_snapshot.identity.SameScene(a_identity)) {
			return false;
		}
		a_snapshot = m_snapshot;
		return true;
	}

	void ActorRuntimeContext::Invalidate(RE::TESFormID a_actorFormID)
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		if (m_snapshot.valid && m_snapshot.identity.actorFormID == a_actorFormID) {
			m_snapshot = {};
		}
	}
}
