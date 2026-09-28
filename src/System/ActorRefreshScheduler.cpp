#include "pch.h"

#include "ActorRefreshScheduler.h"

namespace IAD
{
	namespace
	{
		constexpr auto kEvaluateFlag = static_cast<std::uint32_t>(ActorRefreshFlag::kEvaluateEquip);
		constexpr auto kTransformFlag = static_cast<std::uint32_t>(ActorRefreshFlag::kUpdateTransform);
	}

	void ActorRefreshScheduler::RequestEvaluate(std::uint32_t a_actorID)
	{
		if (a_actorID == 0) return;
		std::lock_guard<std::mutex> lock(m_mutex);
		auto& state = m_states[a_actorID];
		state.retired = false;
		state.pendingFlags |= kEvaluateFlag;
	}

	void ActorRefreshScheduler::RequestTransformUpdate(std::uint32_t a_actorID)
	{
		if (a_actorID == 0) return;
		std::lock_guard<std::mutex> lock(m_mutex);
		auto& state = m_states[a_actorID];
		state.retired = false;
		state.pendingFlags |= kTransformFlag;
	}

	bool ActorRefreshScheduler::BeginEvaluation(std::uint32_t a_actorID, bool a_force)
	{
		if (a_actorID == 0) return false;
		std::lock_guard<std::mutex> lock(m_mutex);
		auto& state = m_states[a_actorID];
		if (state.retired || state.evaluationQueued || (!a_force && (state.pendingFlags & kEvaluateFlag) == 0)) {
			return false;
		}

		state.pendingFlags &= ~kEvaluateFlag;
		state.evaluationQueued = true;
		return true;
	}

	void ActorRefreshScheduler::CompleteEvaluation(std::uint32_t a_actorID, std::uint64_t a_currentTick)
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		auto it = m_states.find(a_actorID);
		if (it == m_states.end()) return;

		it->second.evaluationQueued = false;
		it->second.lastEvaluationTick = a_currentTick;
	}

	ActorRefreshObservation ActorRefreshScheduler::ObserveActor(
		std::uint32_t a_actorID,
		std::uint64_t a_currentTick,
		bool a_hasActiveEffectSignature,
		std::uint64_t a_activeEffectSignature)
	{
		ActorRefreshObservation observation;
		if (a_actorID == 0) return observation;

		std::lock_guard<std::mutex> lock(m_mutex);
		auto& state = m_states[a_actorID];
		observation.newlyTracked = state.lastSeenTick == 0;
		observation.needsCacheClear = state.retired || observation.newlyTracked;
		if (observation.needsCacheClear) {
			state.retired = false;
			state.pendingFlags |= kEvaluateFlag;
		}

		if (a_hasActiveEffectSignature) {
			observation.activeEffectChanged = state.activeEffectSignatureInitialized &&
				state.activeEffectSignature != a_activeEffectSignature;
			state.activeEffectSignature = a_activeEffectSignature;
			state.activeEffectSignatureInitialized = true;
		}

		state.lastSeenTick = a_currentTick;
		return observation;
	}

	ActorRefreshSnapshot ActorRefreshScheduler::Snapshot(std::uint32_t a_actorID) const
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		const auto it = m_states.find(a_actorID);
		if (it == m_states.end()) return {};

		const auto& state = it->second;
		return {
			state.pendingFlags,
			state.lastEvaluationTick,
			state.evaluationQueued,
			state.retired
		};
	}

	bool ActorRefreshScheduler::MarkRetired(std::uint32_t a_actorID)
	{
		if (a_actorID == 0) return false;
		std::lock_guard<std::mutex> lock(m_mutex);
		auto& state = m_states[a_actorID];
		if (state.retired) return false;

		state.pendingFlags = 0;
		state.retired = true;
		return true;
	}

	std::vector<std::uint32_t> ActorRefreshScheduler::CollectStaleActors(
		std::uint32_t a_playerID,
		std::uint64_t a_currentTick,
		std::uint64_t a_staleTicks)
	{
		std::vector<std::uint32_t> staleActors;
		std::lock_guard<std::mutex> lock(m_mutex);
		for (auto it = m_states.begin(); it != m_states.end();) {
			const auto actorID = it->first;
			const auto& state = it->second;
			const bool stale = actorID != a_playerID &&
				!state.evaluationQueued &&
				a_currentTick - state.lastSeenTick > a_staleTicks;
			if (!stale) {
				++it;
				continue;
			}

			staleActors.push_back(actorID);
			it = m_states.erase(it);
		}
		return staleActors;
	}

	void ActorRefreshScheduler::ClearAll()
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		m_states.clear();
	}

	void ActorRefreshScheduler::RequestGlobalRefresh() noexcept
	{
		m_globalRefreshRequested.store(true, std::memory_order_release);
	}

	bool ActorRefreshScheduler::ConsumeGlobalRefreshRequest() noexcept
	{
		return m_globalRefreshRequested.exchange(false, std::memory_order_acq_rel);
	}

	bool ActorRefreshScheduler::TryQueueGlobalRefreshTask() noexcept
	{
		bool expected = false;
		return m_globalRefreshTaskQueued.compare_exchange_strong(
			expected,
			true,
			std::memory_order_acq_rel,
			std::memory_order_acquire);
	}

	void ActorRefreshScheduler::CompleteGlobalRefreshTask() noexcept
	{
		m_globalRefreshTaskQueued.store(false, std::memory_order_release);
	}

	void ActorRefreshScheduler::RequeueGlobalRefresh() noexcept
	{
		CompleteGlobalRefreshTask();
		RequestGlobalRefresh();
	}
}
