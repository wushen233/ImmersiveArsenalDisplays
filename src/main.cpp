#include "pch.h"
#include "UI/ImGuiManager.h"
#include "System/HolsterManager.h"
#include "System/KeyBindStateManager.h"
#include "Data/ConfigManager.h"
#include "Profile/GlobalProfileManager.h"
#include "PapyrusBridge.h"
#include "ModelManager.h"
#include "Engine/NodeManager.h"
#include <RE/M/MenuOpenCloseEvent.h>
#include <RE/U/UI.h>
// [DISABLED] VATS combat hooks - kept for future reference
// #include "Combat/VATSHitHook.h"
// #include "Combat/VATSWeaponPart.h"
// #include "Combat/VATSPatch.h"

// -----------------------------------------------------------
// 1. OG 兼容补丁 (保障 1.10.163 运行时不闪退)
// -----------------------------------------------------------
namespace OGSupport
{
    static F4SE::Impl::F4SEInterface RestoreLoadInterface;

    [[nodiscard]] inline static const char* F4SEAPI F4SEGetSaveFolderName() noexcept
    {
        return "Fallout4";
    }

    void Init(const F4SE::LoadInterface* a_f4se)
    {
        if (a_f4se->RuntimeVersion() <= F4SE::RUNTIME_1_10_163) {
            memcpy(&RestoreLoadInterface, a_f4se, 48);
            (((F4SE::Impl::F4SEInterface*)(&RestoreLoadInterface))->GetSaveFolderName) = F4SEGetSaveFolderName;
            F4SE::Init((const F4SE::LoadInterface*)(&RestoreLoadInterface));
        }
        else {
            F4SE::Init(a_f4se);
        }
    }
}

namespace
{
    class MainMenuLifecycleSink final : public RE::BSTEventSink<RE::MenuOpenCloseEvent>
    {
    public:
        RE::BSEventNotifyControl ProcessEvent(const RE::MenuOpenCloseEvent& a_event, RE::BSTEventSource<RE::MenuOpenCloseEvent>*) override
        {
            if (!a_event.opening) {
                return RE::BSEventNotifyControl::kContinue;
            }

			if (a_event.menuName != "MainMenu") {
				return RE::BSEventNotifyControl::kContinue;
			}

			// This event is delivered on the game thread and is the only lifecycle
			// boundary used to detach IAD nodes from a live actor scene.
			REX::INFO("[IAD Lifecycle] MainMenu opening: beginning game-thread IAD teardown");
			IAD::ModelManager::GetSingleton()->BeginMainMenuTransition();
			return RE::BSEventNotifyControl::kContinue;
        }

        static MainMenuLifecycleSink* GetSingleton()
        {
            static MainMenuLifecycleSink singleton;
            return &singleton;
        }
    };
}

// -----------------------------------------------------------
// 2. 现代与经典双重导出 (全版本通杀通行证)
// -----------------------------------------------------------

// [通行证 B]：给 OG 老世代 (1.10.163) F4SE 使用
F4SE_EXPORT bool F4SEAPI F4SEPlugin_Query(const F4SE::QueryInterface* a_f4se, F4SE::PluginInfo* a_info)
{
    if (!a_f4se || !a_info) return false;
    a_info->infoVersion = F4SE::PluginInfo::kVersion;
    a_info->name = "ImmersiveArsenalDisplays";
    a_info->version = 3;
    return true;
}

// -----------------------------------------------------------
// 3. 核心初始化逻辑
// -----------------------------------------------------------
static bool Initialize(const F4SE::LoadInterface* a_f4se)
{
    static std::once_flag once;
    std::call_once(once, [&]() {
        OGSupport::Init(a_f4se);

        REX::INFO("Immersive Arsenal Displays (IAD) v3.0 模块化架构就绪。");
        auto runtimeIndex = static_cast<std::uint8_t>(REX::FModule::GetRuntimeIndex());
        REX::INFO("当前游戏版本索引: {}", runtimeIndex);

        IAD::ConfigManager::GetSingleton()->LoadINISettings();

		if (auto* serialization = F4SE::GetSerializationInterface()) {
			serialization->SetUniqueID('IAD3');
			serialization->SetRevertCallback(IAD::KeyBindStateManager::OnRevert);
			serialization->SetSaveCallback(IAD::KeyBindStateManager::OnSave);
			serialization->SetLoadCallback(IAD::KeyBindStateManager::OnLoad);
			REX::INFO("[IAD Keybind] registered F4SE serialization callbacks");
		}
		else {
			REX::WARN("[IAD Keybind] F4SE serialization interface unavailable");
		}

        // 👇========== 🌟 核心破局：恢复你原本完美的死循环锚点注入 ==========👇
        std::thread([]() {
            auto rd = RE::BSGraphics::GetRendererData();
            // 1. 死循环监听：精确等待引擎和 ReShade 创建出 SwapChain 的那一刻
            while (!rd || !rd->renderWindow[0].swapChain) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                rd = RE::BSGraphics::GetRendererData();
            }

            // 2. 🚨 致命关键点：以此为锚点，给 ReShade 穿裤子的 1.5 秒时间！
            std::this_thread::sleep_for(std::chrono::milliseconds(1500));

            // 3. 安全注入！此时 ReShade 已经稳定，VTable 劫持绝对安全！
            if (IAD::UI::ImGuiManager::GetSingleton().Install()) {
                REX::INFO("IAD: ImGui 延迟安全注入成功，完美兼容各种画质补丁！");
            }
            }).detach();
        // 👆==============================================================👆

        auto messaging = F4SE::GetMessagingInterface();
        if (messaging) {
            messaging->RegisterListener([](F4SE::MessagingInterface::Message* a_msg) {
                IAD::ModelManager::GetSingleton()->ProcessF4SEMessage(a_msg->type);

                if (a_msg->type == F4SE::MessagingInterface::kPostLoad) {
                    if (auto* papyrus = F4SE::GetPapyrusInterface()) {
                        papyrus->Register(IAD::PapyrusBridge::RegisterFunctions);
                    }
                    else {
                        REX::WARN("[IAD Papyrus] interface unavailable");
                    }
                }

                // 🔥 kGameDataReady: 首次游戏数据就绪，加载配置 & 启动更新循环
                if (a_msg->type == F4SE::MessagingInterface::kGameDataReady) {
                    REX::INFO("[IAD] kGameDataReady triggered. Loading configs...");
                    IAD::ConfigManager::GetSingleton()->LoadConfig();
                    IAD::Profile::GlobalProfileManager::GetSingleton().LoadAll();
                    REX::INFO("[IAD] Config Loaded successfully.");

                    // [DISABLED] VATS combat init
                    // IAD::Combat::InjectWeaponBodyParts();
                    // IAD::Combat::PatchVATSBodyPartIndices();

                    REX::INFO("[IAD] Starting Update Loop...");
                    IAD::HolsterManager::GetSingleton()->StartUpdateLoop();
                    if (auto* ui = RE::UI::GetSingleton()) {
                        ui->GetEventSource<RE::MenuOpenCloseEvent>()->RegisterSink(MainMenuLifecycleSink::GetSingleton());
                        REX::INFO("[IAD Lifecycle] registered MainMenu teardown listener");
                    }
                }

                // 🔥 kPostLoadGame / kNewGame: 游戏世界就绪，注册事件监听器
                // 在此时注册 TESHitEvent sink，因为加载存档后事件源可能被重建
                // [DISABLED] static bool g_combatHooksInstalled = false;
                // [DISABLED] if (!g_combatHooksInstalled && ...) {
                // [DISABLED]   IAD::Combat::InjectWeaponBodyParts();
                // [DISABLED]   IAD::Combat::VATSHitHandler::GetSingleton()->Register();
                // [DISABLED]   g_combatHooksInstalled = true;
                // [DISABLED] }
                static bool g_combatHooksInstalled = true; // [DISABLED]
                });
        }
        });
    return true;
}

// [加载入口]：标准 F4SE 加载口，不再使用可能会被误屏蔽的宏
F4SE_EXPORT bool F4SEAPI F4SEPlugin_Load(const F4SE::LoadInterface* a_f4se)
{
    return Initialize(a_f4se);
}
