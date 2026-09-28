# Immersive Arsenal Displays

Immersive Arsenal Displays (IAD) is a native Fallout 4 equipment display
system for weapons, armor, ammunition, and other items. It provides configurable
slots, dynamic nodes, conditions, model transforms, holsters, model groups,
optional effects and physics, and an in-game Dear ImGui editor.

IAD is display-only. It does not replace gameplay equipment, alter inventories,
or require an ESP/ESM plugin. The experimental `src/Combat` VATS/disarm sources
are kept for reference but are excluded from the build.

## Source Build

The project uses xmake, C++23, and x64 CommonLibF4. CommonLibF4 is intentionally
not vendored in this repository.

The tested workspace dependency is:

- Repository: `https://github.com/Dear-Modding-FO4/commonlibf4`
- Branch: `main`
- Commit: `adc0687f5c41a58308b5be2046644f7f4926424f` (historical MIT-licensed commit)

The active workspace profile is `https://github.com/wushen233/commonlibf4`,
also on `main` at the same historical commit. The current remote `main`
branches are GPL-3.0; see `docs/CommonLibF4-Build-Notes.md` for the distinction.
The workspace build additionally contains
local MSVC compatibility edits in that dependency checkout; those edits are
documented in `docs/CommonLibF4-Build-Notes.md` and are not IAD source files.

### PowerShell

```powershell
git clone https://github.com/Dear-Modding-FO4/commonlibf4.git external/commonlibf4
git -C external/commonlibf4 checkout adc0687f5c41a58308b5be2046644f7f4926424f
$env:COMMONLIBF4_PATH = (Resolve-Path .\external\commonlibf4).Path
xmake f -m releasedbg -p windows -a x64
xmake b ImmersiveArsenalDisplays
```

The build copies the generated DLL and PDB to `data/F4SE/Plugins/`. The PDB is
ignored by Git and is useful only for local debugging. The runtime release
archive is maintained separately from this source repository.

`data/F4SE/Plugins/ImmersiveArsenalDisplays/DefaultConfig.json` is the
source-controlled first-launch display configuration. It includes separate
power-armor node hosts and model transforms. `ActiveConfig.json` is generated
and maintained by the running game and is intentionally not part of the source
repository.

For the recommended local workflow inside the Fallout 4 workspace, see
`docs/Local-Development.md`. The workspace checkout keeps the public source in
`open-source/`; generated DLLs, Papyrus output, runtime configuration, and MO2
deployment files stay outside this repository.

## Runtime Requirements

- Fallout 4 with a matching runtime
- F4SE matching the Fallout 4 runtime
- Address Library for F4SE Plugins
- x64 Microsoft Visual C++ runtime

## Documentation

- `docs/Local-Development.md`: repeatable workspace build and GitHub update workflow
- `docs/NexusMods-Description.md`: ready-to-paste Nexus Mods page
- `docs/Third-Party-Notices.md`: third-party licenses and acknowledgements
- `docs/FO4-IED-architecture.md`: architecture and behavior notes
- `docs/Papyrus-API.md`: `IAD_Native` Papyrus interface
- `docs/World-Preview-Editor-Implementation-Plan.md`: live preview editor design and verification checklist

## License

IAD source code is released under the GNU GPL v3.0. See `LICENSE` and
`docs/Third-Party-Notices.md` for the project and dependency licensing details.

## Author

- Nexus Mods: `https://www.nexusmods.com/profile/Hwushen`
- GitHub: `https://github.com/wushen233`
