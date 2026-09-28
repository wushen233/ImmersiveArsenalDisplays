#pragma once

#include <cstdint>

struct IDXGISwapChain;
struct ID3D11Device;
struct ID3D11DeviceContext;

namespace IAD::UI {
    struct PreviewRenderCapabilitySnapshot {
        bool rendererDataAvailable = false;
        bool rendererInitialized = false;
        bool deviceAvailable = false;
        bool contextAvailable = false;
        bool swapChainAvailable = false;
        bool swapChainTargetAvailable = false;
        bool independentTargetSupported = false;
        std::uint32_t width = 0;
        std::uint32_t height = 0;
        std::uint32_t featureLevel = 0;
        std::uint64_t probeSequence = 0;
    };

    class PreviewRenderCapabilities final {
    public:
        static PreviewRenderCapabilities& GetSingleton() noexcept;

        const PreviewRenderCapabilitySnapshot& Refresh(
            IDXGISwapChain* a_swapChain,
            ID3D11Device* a_device,
            ID3D11DeviceContext* a_context);

        [[nodiscard]] const PreviewRenderCapabilitySnapshot& Get() const noexcept
        {
            return _snapshot;
        }

    private:
        bool ProbeIndependentTarget(ID3D11Device* a_device);
        void LogStateChange(const PreviewRenderCapabilitySnapshot& a_snapshot);

        PreviewRenderCapabilitySnapshot _snapshot;
        PreviewRenderCapabilitySnapshot _lastLoggedSnapshot;
        ID3D11Device* _probedDevice = nullptr;
        bool _independentTargetSupported = false;
        bool _hasProbe = false;
        bool _hasLoggedState = false;
        std::uint64_t _probeSequence = 0;
    };
}
