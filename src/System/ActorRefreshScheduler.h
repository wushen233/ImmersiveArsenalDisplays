#pragma once

#include <atomic>
#include <cstdint>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace IAD
{
	// Scheduling flags are policy state only. The scheduler never touches an
	// Actor, inventory entry, task queue, or scene node.
	enum class ActorRefreshFlag : std::uint32_t {
		kNone = 0,
		kEvaluateEquip = 1u << 0,
		kUpdateTransform = 1u << 1
	};

	struct ActorRefreshState {
		std::uint32_t pendingFlags = 0;
		std::uint64_t lastSeenTick = 0;
		std::uint64_t lastEvaluationTick = 0;
		std::uint64_t activeEffectSignature = 0;
		bool evaluationQueued = false;
		bool retired = false;
		bool activeEffectSignatureInitialized = false;
	};

	struct ActorRefreshObservation {
		bool newlyTracked = false;
		bool needsCacheClear = false;
		bool activeEffectChanged = false;
	};

	struct ActorRefreshSnapshot {
		std::uint32_t pendingFlags = 0;
		std::uint64_t lastEvaluationTick = 0;
		bool evaluationQueued = false;
		bool retired = false;

		bool HasPending(ActorRefreshFlag a_flag) const noexcept
		{
			return (pendingFlags & static_cast<std::uint32_t>(a_flag)) != 0;
		}
	};

	// Coalesces actor refresh requests and global refresh wakeups behind a small
	// Interface. HolsterManager remains responsible for deciding what to evaluate
	// and for submitting the resulting game-thread task.
	class ActorRefreshScheduler final
	{
	public:
		void RequestEvaluate(std::uint32_t a_actorID);
		void RequestTransformUpdate(std::uint32_t a_actorID);

		bool BeginEvaluation(std::uint32_t a_actorID, bool a_force);
		void CompleteEvaluation(std::uint32_t a_actorID, std::uint64_t a_currentTick);

		ActorRefreshObservation ObserveActor(
			std::uint32_t a_actorID,
			std::uint64_t a_currentTick,
			bool a_hasActiveEffectSignature,
			std::uint64_t a_activeEffectSignature);
		ActorRefreshSnapshot Snapshot(std::uint32_t a_actorID) const;
		bool MarkRetired(std::uint32_t a_actorID);
		std::vector<std::uint32_t> CollectStaleActors(
			std::uint32_t a_playerID,
			std::uint64_t a_currentTick,
			std::uint64_t a_staleTicks);

		void ClearAll();

		void RequestGlobalRefresh() noexcept;
		bool ConsumeGlobalRefreshRequest() noexcept;
		bool TryQueueGlobalRefreshTask() noexcept;
		void CompleteGlobalRefreshTask() noexcept;
		void RequeueGlobalRefresh() noexcept;

	private:
		mutable std::mutex m_mutex;
		std::unordered_map<std::uint32_t, ActorRefreshState> m_states;
		std::atomic<bool> m_globalRefreshRequested{ false };
		std::atomic<bool> m_globalRefreshTaskQueued{ false };
	};
}
