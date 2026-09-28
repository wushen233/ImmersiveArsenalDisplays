#include "pch.h"
#include "PreviewRenderCapabilities.h"

namespace IAD::UI {
    PreviewRenderCapabilities& PreviewRenderCapabilities::GetSingleton() noexcept
    {
        static PreviewRenderCapabilities instance;
        return instance;
    }

    const PreviewRenderCapabilitySnapshot& PreviewRenderCapabilities::Refresh(
        IDXGISwapChain* a_swapChain,
        ID3D11Device* a_device,
        ID3D11DeviceContext* a_context)
    {
        PreviewRenderCapabilitySnapshot next;
        auto* rendererData = RE::BSGraphics::GetRendererData();
        next.rendererDataAvailable = rendererData != nullptr;
        next.rendererInitialized = rendererData && rendererData->initialized;
        next.deviceAvailable = rendererData && rendererData->device != nullptr && a_device != nullptr;
        next.contextAvailable = rendererData && rendererData->context != nullptr && a_context != nullptr;
        next.swapChainAvailable = a_swapChain != nullptr;

        if (a_swapChain) {
            DXGI_SWAP_CHAIN_DESC swapChainDesc{};
            if (SUCCEEDED(a_swapChain->GetDesc(&swapChainDesc))) {
                next.width = swapChainDesc.BufferDesc.Width;
                next.height = swapChainDesc.BufferDesc.Height;
            }
        }

        if (rendererData) {
            const auto& window = rendererData->renderWindow[0];
            next.swapChainTargetAvailable =
                window.swapChainRenderTarget.rtView != nullptr &&
                (window.swapChain != nullptr || a_swapChain != nullptr);
        }

        if (next.rendererInitialized && next.deviceAvailable && next.contextAvailable && next.swapChainAvailable) {
            if (!_hasProbe || _probedDevice != a_device) {
                _probedDevice = a_device;
                _hasProbe = true;
                ++_probeSequence;
                _independentTargetSupported = ProbeIndependentTarget(a_device);
            }
            next.independentTargetSupported = _independentTargetSupported;
            next.featureLevel = static_cast<std::uint32_t>(a_device->GetFeatureLevel());
            next.probeSequence = _probeSequence;
        }

        LogStateChange(next);
        _snapshot = next;
        return _snapshot;
    }

    bool PreviewRenderCapabilities::ProbeIndependentTarget(ID3D11Device* a_device)
    {
        if (!a_device) {
            return false;
        }

        D3D11_TEXTURE2D_DESC textureDesc{};
        textureDesc.Width = 1;
        textureDesc.Height = 1;
        textureDesc.MipLevels = 1;
        textureDesc.ArraySize = 1;
        textureDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        textureDesc.SampleDesc.Count = 1;
        textureDesc.Usage = D3D11_USAGE_DEFAULT;
        textureDesc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;

        ID3D11Texture2D* texture = nullptr;
        if (FAILED(a_device->CreateTexture2D(&textureDesc, nullptr, &texture))) {
            return false;
        }

        ID3D11RenderTargetView* renderTargetView = nullptr;
        ID3D11ShaderResourceView* shaderResourceView = nullptr;
        const bool renderTargetCreated = SUCCEEDED(a_device->CreateRenderTargetView(texture, nullptr, &renderTargetView));
        const bool shaderResourceCreated = SUCCEEDED(a_device->CreateShaderResourceView(texture, nullptr, &shaderResourceView));
        const bool supported = renderTargetCreated && shaderResourceCreated;

        if (shaderResourceView) {
            shaderResourceView->Release();
        }
        if (renderTargetView) {
            renderTargetView->Release();
        }
        texture->Release();
        return supported;
    }

    void PreviewRenderCapabilities::LogStateChange(const PreviewRenderCapabilitySnapshot& a_snapshot)
    {
        const bool stateChanged = !_hasLoggedState ||
            _lastLoggedSnapshot.rendererDataAvailable != a_snapshot.rendererDataAvailable ||
            _lastLoggedSnapshot.rendererInitialized != a_snapshot.rendererInitialized ||
            _lastLoggedSnapshot.deviceAvailable != a_snapshot.deviceAvailable ||
            _lastLoggedSnapshot.contextAvailable != a_snapshot.contextAvailable ||
            _lastLoggedSnapshot.swapChainAvailable != a_snapshot.swapChainAvailable ||
            _lastLoggedSnapshot.swapChainTargetAvailable != a_snapshot.swapChainTargetAvailable ||
            _lastLoggedSnapshot.independentTargetSupported != a_snapshot.independentTargetSupported ||
            _lastLoggedSnapshot.width != a_snapshot.width ||
            _lastLoggedSnapshot.height != a_snapshot.height ||
            _lastLoggedSnapshot.featureLevel != a_snapshot.featureLevel;
        if (!stateChanged) {
            return;
        }

        _lastLoggedSnapshot = a_snapshot;
        _hasLoggedState = true;
        if (!a_snapshot.rendererInitialized || !a_snapshot.deviceAvailable || !a_snapshot.contextAvailable) {
            return;
        }

        if (a_snapshot.independentTargetSupported) {
            REX::INFO(
                "[IAD Preview] independent DX11 target probe passed ({}x{}, feature level 0x{:X})",
                a_snapshot.width,
                a_snapshot.height,
                a_snapshot.featureLevel);
        } else {
            REX::WARN(
                "[IAD Preview] independent DX11 target probe failed ({}x{}, feature level 0x{:X})",
                a_snapshot.width,
                a_snapshot.height,
                a_snapshot.featureLevel);
        }
    }
}
