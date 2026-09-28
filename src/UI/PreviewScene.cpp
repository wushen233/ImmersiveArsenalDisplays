#include "pch.h"

#include "PreviewScene.h"
#include "DetachedPreviewScene.h"
#include "UIWorldPreviewCamera.h"

#include "Engine/NodeManager.h"
#include "ModelManager.h"

#include <RE/P/PlayerCharacter.h>

namespace IAD::UI {
    namespace {
        // Keep the stable in-world preview active while the detached Interface3D
        // renderer remains a future implementation branch.
        constexpr bool kDetachedPreviewEnabled = false;
    }

    PreviewScene& PreviewScene::GetSingleton() noexcept
    {
        static PreviewScene instance;
        return instance;
    }

    IPreviewSceneAdapter& PreviewScene::GetActive() noexcept
    {
        if constexpr (kDetachedPreviewEnabled) {
            if (m_detachedRequested.load(std::memory_order_acquire) && GetDetached().IsReady()) {
                return GetDetached();
            }
        }
        return UIWorldPreviewCamera::GetSingleton();
    }

    const IPreviewSceneAdapter& PreviewScene::GetActive() const noexcept
    {
        if constexpr (kDetachedPreviewEnabled) {
            if (m_detachedRequested.load(std::memory_order_acquire) && GetDetached().IsReady()) {
                return GetDetached();
            }
        }
        return UIWorldPreviewCamera::GetSingleton();
    }

	DetachedPreviewScene& PreviewScene::GetDetached() noexcept
	{
		return DetachedPreviewScene::GetSingleton();
	}

	const DetachedPreviewScene& PreviewScene::GetDetached() const noexcept
	{
		return DetachedPreviewScene::GetSingleton();
	}

	void PreviewScene::ProcessGameThread(bool a_enabled, const PreviewSceneSnapshot& a_snapshot)
	{
		if constexpr (kDetachedPreviewEnabled) {
		if (m_detachedRequested.load(std::memory_order_acquire)) {
			auto snapshot = a_snapshot;
			// The detached scene must be able to build its first frame before the
			// ImGui session exists. Take a current player identity for that first
			// game-thread pass; later passes use the session's pinned snapshot.
			if (snapshot.identity.actorFormID == 0) {
				if (auto* player = RE::PlayerCharacter::GetSingleton(); player && player->Get3D(false) &&
					!player->IsDead(false) && !player->IsDeleted() && !player->IsDisabled()) {
					snapshot.identity.actorFormID = player->GetFormID();
					snapshot.identity.sceneGeneration = ModelManager::GetSceneGeneration();
					snapshot.identity.actor3DGeneration = NodeManager::GetActor3DGeneration(player->GetFormID());
					snapshot.actorPosition = player->GetPosition();
				}
			}
			GetDetached().ProcessGameThread(a_enabled || snapshot.identity.actorFormID != 0, snapshot);
		}
		else {
			GetDetached().RetireGameThread("detached adapter not requested");
		}
		}
		else {
			GetDetached().RetireGameThread("detached preview temporarily disabled");
		}
	}

	void PreviewScene::SetDetachedRequested(bool) noexcept
	{
		m_detachedRequested.store(false, std::memory_order_release);
	}

	bool PreviewScene::IsDetachedRequested() const noexcept
	{
		return kDetachedPreviewEnabled && m_detachedRequested.load(std::memory_order_acquire);
	}
}
