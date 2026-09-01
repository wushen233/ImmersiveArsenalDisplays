# Immersive Arsenal Displays

[center]
[size=6][b]Immersive Arsenal Displays[/b][/size]

[size=4]A native Fallout 4 equipment display system for weapons, armor, ammunition, and more.[/size]
[/center]

[h1]Overview[/h1]

Immersive Arsenal Displays (IAD) adds configurable, in-world equipment displays to Fallout 4. Display weapons and other equipment on your character and high-process NPCs using custom slots, dynamic nodes, model transforms, conditions, and an in-game ImGui editor.

IAD is display-only. It does not replace gameplay equipment, change NPC inventories, or require an ESP/ESM plugin.

[h1]Features[/h1]

[list]
[*]Display weapons, melee weapons, armor, ammunition, chems, food, and miscellaneous items.
[*]Player and optional high-process NPC displays.
[*]Configurable display slots with custom target nodes, transforms, scale, rotation, and pivot controls.
[*]Custom display rules with priorities, preferred items, selection modes, conditions, Form/keyword filters, and spawn limits.
[*]Last-equipped, strongest, random, recent-acquired, and configured-order selection behaviors.
[*]Holster models, model swaps, model groups, magazine/ammunition extraction, effects, lights, and optional equipment physics.
[*]Dynamic CME/MOV nodes and race-aware skeleton handling.
[*]Named keybind states, runtime variables, active-effect conditions, quest-stage conditions, and Papyrus integration.
[*]Runtime-safe equipment events, asynchronous model loading, scene-generation checks, and save/load cleanup.
[*]In-game ImGui configuration editor with import/export support for global snapshots and form-filter profiles.
[/list]

[h1]Requirements[/h1]

[list]
[*]Fallout 4
[*]Fallout 4 Script Extender (F4SE), matching your Fallout 4 runtime
[*]Address Library for F4SE Plugins
[*]A working x64 Microsoft Visual C++ runtime environment
[/list]

[h1]Installation[/h1]

[list=1]
[*]Install the requirements first.
[*]Install the IAD archive with your mod manager. Mod Organizer 2 is recommended.
[*]Launch Fallout 4 through F4SE.
[*]The default editor shortcut is [b]Shift + F1[/b]. It can be changed in the IAD settings.
[/list]

The installed files should be placed under:

[code]
Data/F4SE/Plugins/ImmersiveArsenalDisplays.dll
Data/F4SE/Plugins/ImmersiveArsenalDisplays/
Data/Scripts/IAD_Native.pex
[/code]

[h1]Configuration[/h1]

The main configuration files are stored in:

[code]
Data/F4SE/Plugins/ImmersiveArsenalDisplays/ActiveConfig.json
Data/F4SE/Plugins/ImmersiveArsenalDisplays/IAD_Settings.json
Data/F4SE/Plugins/ImmersiveArsenalDisplays/ImmersiveArsenalDisplays.ini
[/code]

Use the in-game editor for normal configuration. Exported snapshots and form-filter profiles are stored in the IAD configuration directory and can be backed up independently.

[h1]Compatibility and Limitations[/h1]

[list]
[*]IAD is a native F4SE plugin and must be used with a compatible Fallout 4 runtime and F4SE build.
[*]IAD does not add gameplay weapons or armor and does not alter NPC gameplay equipment.
[*]Custom model and holster paths must point to files that exist in your Fallout 4 Data installation.
[*]The displayed result depends on the selected slot filters, conditions, model paths, and the equipment installed in your game.
[*]The test-only `10mm_R_thigh_f.nif` holster mesh used during development is not included in this release.
[/list]

[h1]Source Code[/h1]

The source code is available on GitHub:

[url=https://github.com/wushen233/ImmersiveArsenalDisplays]Immersive Arsenal Displays on GitHub[/url]

[h1]Credits and Acknowledgements[/h1]

[list]
[*][b]Fallout 4[/b] and [b]Bethesda Game Studios[/b] for the game, its runtime, and its asset ecosystem.
[*][b]F4SE[/b] by the F4SE team: [url=https://f4se.silverlock.org/]f4se.silverlock.org[/url].
[*][b]CommonLibF4[/b] and its contributors. IAD is built against the [url=https://github.com/wushen233/commonlibf4]wushen233/commonlibf4[/url] profile, with lineage from [url=https://github.com/Dear-Modding-FO4/commonlibf4]Dear-Modding-FO4/commonlibf4[/url] and [url=https://github.com/libxse/commonlibf4]libxse/commonlibf4[/url].
[*][b]Dear ImGui[/b] by ocornut: [url=https://github.com/ocornut/imgui]github.com/ocornut/imgui[/url].
[*][b]Microsoft Detours[/b]: [url=https://github.com/microsoft/Detours]github.com/microsoft/Detours[/url].
[*][b]SimpleIni[/b] by brofield: [url=https://github.com/brofield/simpleini]github.com/brofield/simpleini[/url].
[*][b]JSON for Modern C++[/b] by nlohmann: [url=https://github.com/nlohmann/json]github.com/nlohmann/json[/url].
[*][b]Immersive Equipment Displays[/b] by SlavicPotato: [url=https://github.com/SlavicPotato/ied-dev]github.com/SlavicPotato/ied-dev[/url]. IAD uses IED as a behavioral and configuration reference for its Fallout 4-specific implementation; IAD is not affiliated with IED.
[*][b]Community testers and beta testers[/b] for repeated gameplay, equipment-transition, death, and save/load verification.
[/list]

[h2]Author[/h2]

[url=https://www.nexusmods.com/profile/Hwushen]Hwushen on Nexus Mods[/url] | [url=https://github.com/wushen233]wushen233 on GitHub[/url]

[h1]License[/h1]

IAD source code is released under the [b]GNU GPL v3.0[/b]. Third-party libraries and reference projects retain their own licenses. See the GitHub repository for the complete license and third-party notices.

[h1]Support and Bug Reports[/h1]

When reporting a problem, include:

[list]
[*]Fallout 4 runtime version and F4SE version.
[*]IAD version.
[*]The relevant `ImmersiveArsenalDisplays.log` file.
[*]The active slot/custom rule and model or holster path involved.
[*]A reproducible sequence of actions, such as equip, draw, sheath, death, or save loading.
[/list]
