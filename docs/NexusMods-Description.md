[center]
[size=6][b]Immersive Arsenal Displays[/b][/size]

[size=4]A native Fallout 4 equipment display system for weapons, armor, ammunition, and more.[/size]

[i]English first | 中文在下[/i]
[/center]

[size=5][b]English[/b][/size]

[size=4][b]Overview[/b][/size]

Immersive Arsenal Displays (IAD) adds configurable, in-world equipment displays to Fallout 4. Display weapons and other equipment on your character and high-process NPCs using custom slots, dynamic nodes, model transforms, conditions, and an in-game Dear ImGui editor.

IAD is display-only. It does not replace gameplay equipment, change inventories, add weapons or armor, or require an ESP/ESM plugin.

[size=4][b]Features[/b][/size]

[list]
[*]Display weapons, melee weapons, armor, ammunition, chems, food, and miscellaneous items.
[*]Player and optional high-process NPC displays.
[*]Configurable slots with custom target nodes, transforms, scale, rotation, and pivot controls.
[*]Global, Actor, NPC Base, and Race configuration layers.
[*]Custom rules with priorities, preferred items, selection modes, conditions, Form/keyword filters, and spawn limits.
[*]Last-equipped, strongest, random, recently acquired, and configured-order selection behaviors.
[*]Holster models, model swaps, model groups, magazine/ammunition extraction, effects, lights, and optional equipment physics.
[*]Dynamic CME/MOV nodes and race-aware skeleton handling.
[*]Named keybind states, runtime variables, active-effect conditions, quest-stage conditions, and Papyrus integration.
[*]Equipment-event refresh, asynchronous model loading, scene-generation checks, death cleanup, and save/load protection.
[*]In-game ImGui configuration editor with import/export support for global snapshots and Form-filter profiles.
[*]Live World Preview Editor with clickable MOV equipment contours and CME node gizmos.
[*]Right-drag character rotation, middle-drag pan, mouse-wheel zoom, precision modifiers, and multi-step Ctrl+Z editing history.
[*]Ships with ready-to-use normal-body and power-armor display defaults; power armor automatically uses dedicated armor skeleton hosts and independent model transforms.
[/list]

[size=4][b]World Preview Editor[/b][/size]

The Visualizer can show the live player character as an editing preview. Preview editing is enabled by default while the IAD menu is open.

[list]
[*]The visible marker family follows the focused Slots or Nodes window.
[*]Click an equipment contour to select its MOV slot, or click a CME point to select its node.
[*]Drag the red, green, or blue axis to edit the persistent local transform.
[*]Use Shift for fine movement, Ctrl for coarse movement, and Ctrl + Shift for extra-fine movement.
[*]Press Ctrl + Z repeatedly to undo completed drags in reverse order. Up to 64 edits are kept per preview session.
[*]Right-drag empty preview space to rotate the character; use the middle button to pan and the mouse wheel to zoom.
[*]Closing the IAD menu hides the preview and restores the temporary camera/character state.
[/list]

[size=4][b]Requirements[/b][/size]

[list]
[*]Fallout 4 runtime 1.10.163 through 1.11.221 via multi-runtime CommonLibF4 builds. Currently verified in testing on 1.10.163 and 1.11.221.
[*]Fallout 4 Script Extender (F4SE), matching your Fallout 4 runtime.
[*]Address Library for F4SE Plugins, matching your Fallout 4 runtime.
[*]x64 Microsoft Visual C++ runtime.
[/list]

[size=4][b]Installation[/b][/size]

[list=1]
[*]Install Fallout 4, F4SE, and Address Library for your runtime.
[*]Install the IAD archive with your mod manager. Mod Organizer 2 is recommended.
[*]Launch Fallout 4 through F4SE.
[*]Open the editor with [b]Shift + F1[/b]. The shortcut can be changed in the IAD INI settings.
[/list]

The archive uses the normal Fallout 4 Data layout:

[code]
Data/F4SE/Plugins/ImmersiveArsenalDisplays.dll
Data/F4SE/Plugins/ImmersiveArsenalDisplays/
Data/Scripts/IAD_Native.pex[/code]

IAD does not include an ESP/ESM plugin.

[size=4][b]Configuration[/b][/size]

The main configuration files are stored in:

[code]
Data/F4SE/Plugins/ImmersiveArsenalDisplays/ActiveConfig.json
Data/F4SE/Plugins/ImmersiveArsenalDisplays/DefaultConfig.json
Data/F4SE/Plugins/ImmersiveArsenalDisplays/IAD_Settings.json
Data/F4SE/Plugins/ImmersiveArsenalDisplays/ImmersiveArsenalDisplays.ini[/code]

Use the in-game editor for normal configuration. If no active configuration exists, IAD creates one from the shipped default configuration. The default includes separate normal-body and power-armor display states; exported snapshots and Form-filter profiles can be backed up independently.

Custom model and holster paths must point to files that exist in your Fallout 4 Data installation. IAD does not ship the test-only [b]10mm_R_thigh_f.nif[/b] holster mesh used during development.

[size=4][b]Papyrus API[/b][/size]

The optional [b]IAD_Native[/b] Papyrus interface allows Quest and scene scripts to control runtime variables, model paths, target Forms, and display refreshes. See the source repository documentation for the API reference.

[size=4][b]Compatibility and Limitations[/b][/size]

[list]
[*]IAD is a native F4SE plugin and must be used with a compatible Fallout 4 runtime and F4SE build.
[*]IAD does not alter gameplay equipment, NPC inventories, or weapon behavior.
[*]The displayed result depends on slot filters, conditions, model paths, skeleton nodes, and the equipment installed in your game.
[*]Custom skeletons and custom equipment may require manual node, transform, or model adjustments.
[*]Immersive Equipment Displays (IED) is a design and behavior reference for IAD. IAD is an independent Fallout 4 implementation and does not include IED source files or runtime assets.
[/list]

[size=4][b]Source Code[/b][/size]

The IAD source code is available here:

[url=https://github.com/wushen233/ImmersiveArsenalDisplays]Immersive Arsenal Displays on GitHub[/url]

The published source includes the C++ source, Papyrus source, xmake build file, architecture notes, API documentation, and third-party notices. Runtime binaries and personal development configurations are not included in the source repository.

[size=4][b]Credits and Acknowledgements[/b][/size]

[list]
[*][b]Fallout 4[/b] and [b]Bethesda Game Studios[/b] for the game, its runtime, and its asset ecosystem.
[*][b]F4SE[/b] by the F4SE team: [url=https://f4se.silverlock.org/]f4se.silverlock.org[/url].
[*][b]CommonLibF4[/b] and its contributors: [url=https://github.com/Dear-Modding-FO4/commonlibf4]Dear-Modding-FO4/commonlibf4[/url], [url=https://github.com/wushen233/commonlibf4]wushen233/commonlibf4[/url], and the upstream project lineage.
[*][b]Dear ImGui[/b] by ocornut: [url=https://github.com/ocornut/imgui]github.com/ocornut/imgui[/url].
[*][b]Microsoft Detours[/b]: [url=https://github.com/microsoft/Detours]github.com/microsoft/Detours[/url].
[*][b]SimpleIni[/b] by brofield: [url=https://github.com/brofield/simpleini]github.com/brofield/simpleini[/url].
[*][b]JSON for Modern C++[/b] by nlohmann: [url=https://github.com/nlohmann/json]github.com/nlohmann/json[/url].
[*][b]Immersive Equipment Displays[/b] by SlavicPotato: [url=https://github.com/SlavicPotato/ied-dev]github.com/SlavicPotato/ied-dev[/url]. IAD uses IED as a behavioral, configuration, and UI design reference; IAD is not affiliated with IED.
[*][b]Community testers and beta testers[/b] for repeated gameplay, equipment-transition, death, save/load, and regression testing. Individual names are intentionally omitted.
[/list]

[size=4][b]License[/b][/size]

IAD source code is released under the [b]GNU GPL v3.0[/b]. Third-party libraries and reference projects retain their own licenses. The runtime archive includes the license and third-party notices.

[size=4][b]Author[/b][/size]

[url=https://www.nexusmods.com/profile/Hwushen]Hwushen on Nexus Mods[/url] | [url=https://github.com/wushen233]wushen233 on GitHub[/url]

[size=4][b]Support and Bug Reports[/b][/size]

When reporting a problem, please include:

[list]
[*]Fallout 4 runtime version and F4SE version.
[*]IAD version.
[*]The relevant [b]ImmersiveArsenalDisplays.log[/b] file.
[*]The active slot, custom rule, model path, or holster path involved.
[*]A reproducible sequence of actions, such as equip, draw, sheath, death, or save loading.
[/list]

[hr]

[size=5][b]中文说明[/b][/size]

[size=4][b]模组概述[/b][/size]

Immersive Arsenal Displays（IAD）是一个原生 Fallout 4 的装备展示系统。它可以将武器、护甲、弹药和其他装备按照自定义插槽、动态节点、模型变换、条件和显示规则显示在角色身上，并提供游戏内 Dear ImGui 配置编辑器。

IAD 仅负责展示，不会替换游戏装备、修改物品库存、添加武器或护甲，也不需要 ESP/ESM 插件。

[size=4][b]主要功能[/b][/size]

[list]
[*]支持手枪、步枪、近战武器、护甲、弹药、药品、食物和其他物品的展示。
[*]支持玩家和可选的 high-process NPC 展示。
[*]支持自定义插槽、目标节点、位置、旋转、缩放和轴心调整。
[*]支持 Global、Actor、NPC Base 和 Race 四级配置覆盖。
[*]支持优先级、首选物品、条件、Form/Keyword 过滤和生成数量限制。
[*]支持最近装备、最强物品、随机、最近获得和配置顺序等选择方式。
[*]支持枪套、模型替换、模型组、弹匣/弹药提取、灯光、效果和可选的装备物理。
[*]支持 CME/MOV 动态节点和不同种族骨骼的自适应处理。
[*]支持命名按键状态、运行时变量、活动效果条件、任务阶段条件和 Papyrus 接口。
[*]支持装备事件刷新、异步模型加载、场景生命周期检查、死亡清理和读档保护。
[*]提供游戏内 ImGui 配置编辑器，支持全局快照和 Form 过滤配置导入/导出。
[*]提供实时角色预览编辑器，可点击 MOV 装备轮廓和 CME 节点操作柄。
[*]支持右键旋转角色、中键平移、滚轮缩放、精细/粗略移动以及多步 Ctrl+Z 撤回。
[*]内置普通人体和动力甲两套开箱即用的显示配置；进入动力甲后会自动使用专用 Armor 骨骼节点和独立模型变换。
[/list]

[size=4][b]角色预览编辑器[/b][/size]

打开 IAD 菜单后，Visualizer 会默认启用实时角色预览编辑。预览显示当前游戏中的真实角色，不创建离屏替身。

[list]
[*]可视化内容会根据当前聚焦的装备插槽窗口或挂载节点窗口自动切换。
[*]点击装备模型轮廓可选择对应 MOV 插槽，点击 CME 点位可选择对应节点。
[*]拖动红、绿、蓝轴可以修改持久化的本地变换。
[*]按住 Shift 进行精细移动，Ctrl 进行粗略移动，Ctrl + Shift 进行超精细移动。
[*]连续按 Ctrl + Z 可以按编辑时间倒序撤回已完成的拖动，每次预览会话最多保留 64 条记录。
[*]在空白预览区域按住右键旋转角色，中键平移，滚轮缩放。
[*]关闭 IAD 菜单后预览会隐藏，并恢复临时镜头/角色状态。
[/list]

[size=4][b]环境要求[/b][/size]

[list]
[*]通过多版本 CommonLibF4 构建，理论支持 Fallout 4 1.10.163 至 1.11.221 运行时。目前已实际测试 1.10.163 和 1.11.221。
[*]与 Fallout 4 运行时匹配的 F4SE。
[*]与 Fallout 4 运行时匹配的 Address Library for F4SE Plugins。
[*]x64 Microsoft Visual C++ 运行库。
[/list]

[size=4][b]安装方法[/b][/size]

[list=1]
[*]先安装与游戏运行时匹配的 Fallout 4、F4SE 和 Address Library。
[*]使用模组管理器安装 IAD，推荐使用 Mod Organizer 2。
[*]通过 F4SE 启动游戏。
[*]默认使用 [b]Shift + F1[/b] 打开配置编辑器，可在 INI 中修改。
[/list]

压缩包使用标准 Fallout 4 Data 目录结构：

[code]
Data/F4SE/Plugins/ImmersiveArsenalDisplays.dll
Data/F4SE/Plugins/ImmersiveArsenalDisplays/
Data/Scripts/IAD_Native.pex[/code]

IAD 不包含 ESP/ESM 插件。

[size=4][b]配置文件[/b][/size]

主要配置文件位于：

[code]
Data/F4SE/Plugins/ImmersiveArsenalDisplays/ActiveConfig.json
Data/F4SE/Plugins/ImmersiveArsenalDisplays/DefaultConfig.json
Data/F4SE/Plugins/ImmersiveArsenalDisplays/IAD_Settings.json
Data/F4SE/Plugins/ImmersiveArsenalDisplays/ImmersiveArsenalDisplays.ini[/code]

日常配置请使用游戏内编辑器。如果没有活动配置，IAD 会根据压缩包内置的默认配置创建。默认配置已经区分普通人体和动力甲显示状态；导出的快照和 Form 过滤配置可以单独备份。

自定义模型和枪套路径必须指向 Fallout 4 Data 目录中实际存在的文件。本模组不包含开发测试使用的 [b]10mm_R_thigh_f.nif[/b] 枪套模型。

[size=4][b]Papyrus 接口[/b][/size]

可选的 [b]IAD_Native[/b] Papyrus 接口可供 Quest 和场景脚本控制运行时变量、模型路径、目标 Form 和展示刷新。详细接口参数请参考源码仓库中的文档。

[size=4][b]兼容性与限制[/b][/size]

[list]
[*]IAD 是原生 F4SE 插件，必须使用与游戏运行时和 F4SE 匹配的版本。
[*]IAD 不会修改游戏装备、NPC 物品库存或武器行为。
[*]实际显示效果取决于插槽过滤、条件、模型路径、骨骼节点和游戏中已安装的装备。
[*]使用自定义骨骼或自定义装备时，可能需要手动调整节点、位置和模型。
[*]Immersive Equipment Displays（IED）是 IAD 学习的行为和配置参考。IAD 是独立的 Fallout 4 实现，不包含 IED 源码或运行时资产。
[/list]

[size=4][b]源码[/b][/size]

IAD 源码地址：

[url=https://github.com/wushen233/ImmersiveArsenalDisplays]Immersive Arsenal Displays GitHub 仓库[/url]

公开源码包含 C++ 源码、Papyrus 源码、xmake 构建文件、架构说明、API 文档和第三方鸣谢。运行时二进制文件和个人开发配置不包含在源码仓库中。

[size=4][b]鸣谢[/b][/size]

[list]
[*][b]Fallout 4[/b] 与 [b]Bethesda Game Studios[/b]。
[*][b]F4SE[/b] 团队：[url=https://f4se.silverlock.org/]f4se.silverlock.org[/url]。
[*][b]CommonLibF4[/b] 及其贡献者：[url=https://github.com/Dear-Modding-FO4/commonlibf4]Dear-Modding-FO4/commonlibf4[/url]、[url=https://github.com/wushen233/commonlibf4]wushen233/commonlibf4[/url] 以及上游项目。
[*][b]Dear ImGui[/b]：[url=https://github.com/ocornut/imgui]github.com/ocornut/imgui[/url]。
[*][b]Microsoft Detours[/b]：[url=https://github.com/microsoft/Detours]github.com/microsoft/Detours[/url]。
[*][b]SimpleIni[/b]：[url=https://github.com/brofield/simpleini]github.com/brofield/simpleini[/url]。
[*][b]JSON for Modern C++[/b]：[url=https://github.com/nlohmann/json]github.com/nlohmann/json[/url]。
[*][b]Immersive Equipment Displays[/b]：由 SlavicPotato 开发：[url=https://github.com/SlavicPotato/ied-dev]github.com/SlavicPotato/ied-dev[/url]。IAD 将 IED 作为行为、配置和 UI 设计参考，是独立的 Fallout 4 实现，与 IED 没有隶属或关联关系。
[*][b]社区测试者和 Beta 测试者[/b]，感谢各位对装备切换、拔枪/收枪、死亡、读档和回归测试提供帮助。个人姓名未在此逐一列出。
[/list]

[size=4][b]许可证[/b][/size]

IAD 源码使用 [b]GNU GPL v3.0[/b] 发布。第三方库和参考项目使用各自的许可证。运行时压缩包中包含许可证和第三方鸣谢文件。

[size=4][b]作者[/b][/size]

[url=https://www.nexusmods.com/profile/Hwushen]Hwushen 的 Nexus Mods 资料页[/url] | [url=https://github.com/wushen233]wushen233 的 GitHub 资料页[/url]

[size=4][b]问题反馈[/b][/size]

反馈问题时请尽量提供：

[list]
[*]Fallout 4 运行时和 F4SE 版本。
[*]IAD 版本。
[*]相关的 [b]ImmersiveArsenalDisplays.log[/b] 日志。
[*]涉及的插槽、自定义规则、模型路径或枪套路径。
[*]可重现的操作步骤，例如装备、拔枪、收枪、死亡或读档。
[/list]
