# Third-Party Notices

Immersive Arsenal Displays is an independent Fallout 4 project. The following
projects are used by the source build, runtime integration, or design
reference. Each project retains its own license.

## Runtime and Native Integration

- Fallout 4 and Bethesda Game Studios: the game runtime and Bethesda assets
  remain the property of their respective owners. IAD is not affiliated with
  or endorsed by Bethesda Game Studios.
- Fallout 4 Script Extender (F4SE): https://f4se.silverlock.org/
- Address Library for F4SE Plugins: https://www.nexusmods.com/fallout4/mods/47327

## Source Dependencies

- CommonLibF4, active profile: https://github.com/wushen233/commonlibf4
  - Parent reference: https://github.com/Dear-Modding-FO4/commonlibf4
  - Upstream lineage: https://github.com/libxse/commonlibf4
  - The current `main` branches of the active and parent repositories are
    GPL-3.0. The IAD workspace is pinned to older commit
    `adc0687f5c41a58308b5be2046644f7f4926424f`, whose LICENSE is MIT.
  - The applicable CommonLibF4 copyright and license text must be retained
    when CommonLibF4 source is redistributed.
- Dear ImGui: https://github.com/ocornut/imgui
  - MIT License.
- Microsoft Detours: https://github.com/microsoft/Detours
  - MIT License.
- SimpleIni: https://github.com/brofield/simpleini
  - MIT License.
- JSON for Modern C++: https://github.com/nlohmann/json
  - MIT License.
- xmake: https://github.com/xmake-io/xmake
  - Build tool used by the source project; it is not bundled in the runtime
    mod archive.

## Design Reference

- Immersive Equipment Displays by SlavicPotato:
  https://github.com/SlavicPotato/ied-dev
  - MIT License.
  - IAD uses IED as a behavioral, configuration, and UI design reference for
    a Fallout 4-specific implementation. IAD does not include IED source files
    or IED runtime assets.

## Community

Community testers and beta testers are acknowledged for gameplay testing,
equipment-transition testing, death testing, save/load testing, and bug
reports. Individual names are intentionally omitted.

## Project License

IAD source code is released under the GNU GPL v3.0. The complete license text is
provided in the repository root as `LICENSE`.
