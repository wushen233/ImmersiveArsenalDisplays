#pragma once

#include <chrono>
#include <cstdint>
#include <mutex>

namespace IAD::UI {
    class UICommitManager final {
    public:
        static UICommitManager& GetSingleton();

        static void RequestConfigSave();
        static void RequestINISettingsSave();
        static void CommitNow();

        void FlushIfDue();
        bool HasPendingChanges() const;
        void DrawStatus() const;

    private:
        using Clock = std::chrono::steady_clock;

        void Request(bool a_config, bool a_ini);
        void Commit(bool a_forceConfig, bool a_forceINI);

        mutable std::mutex _stateMutex;
        std::mutex _commitMutex;
        std::uint64_t _configRequested = 0;
        std::uint64_t _configCommitted = 0;
        std::uint64_t _iniRequested = 0;
        std::uint64_t _iniCommitted = 0;
        Clock::time_point _deadline{};
    };
}
