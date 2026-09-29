# Local Development

This guide separates maintainer development in the Fallout 4 workspace from public contributions made from a standalone GitHub clone.

## Maintainer workspace development

The canonical IAD development source is the workspace-owned project at `FO4ModWorkspace/projects/ImmersiveArsenalDisplays`. The active authority is determined by the workspace metadata and rules. The GitHub publication repository is not a second persistent canonical checkout.

Maintainers build and test the canonical workspace project using the workspace toolchain profile. Workspace integration scripts can build the project, synchronize its data image to the configured MO2 overlay, promote a reviewed default configuration, and package a release candidate.

The following scripts are maintainer workspace tooling and are not guaranteed to exist in a standalone GitHub clone:

- `scripts/build-and-sync.ps1`
- `scripts/package-release.ps1`
- `scripts/Update-DefaultConfig.ps1`

Run these scripts only in the configured workspace project. They depend on workspace metadata, tools, paths, or release layout.

## GitHub publication repository

The public source repository is [wushen233/ImmersiveArsenalDisplays](https://github.com/wushen233/ImmersiveArsenalDisplays). It is the public publication history and contributor entry point; it is not a persistent second source tree inside the maintainer workspace.

Maintainers publish from the canonical workspace through a temporary clean clone:

1. Clone the publication repository into a temporary location and verify the expected remote baseline.
2. Copy only the approved publication files from the canonical workspace.
3. Preserve publication-specific files such as `.gitignore`, `README.md`, `xmake.lua`, and `scripts/build.ps1` unless a change to them is explicitly approved.
4. Run the static checks and standalone build in the publication clone.
5. Review the exact diff, then publish a branch and pull request. Do not mirror every workspace file or publish runtime state.

Do not pull or push a nested project repository from the maintainer workspace. Keep publication work in the temporary clone and do not push directly to `main`.

## Standalone contributor build

A clean GitHub clone supports a standalone build when a compatible CommonLibF4 checkout is available. Set `COMMONLIBF4_PATH` to that checkout, or pass its path directly to the build script:

```powershell
$env:COMMONLIBF4_PATH = '<path-to-CommonLibF4>'
.\scripts\build.ps1 -Configuration releasedbg -CommonLibF4Path $env:COMMONLIBF4_PATH
powershell -ExecutionPolicy Bypass -File .\scripts\Test-StaticInvariants.ps1
```

The build script uses the standalone repository configuration. CommonLibF4 is an external dependency and is not vendored in this repository.

## Source and generated files

The public source tree includes C++ source, Papyrus `.psc` source such as `data/Scripts/Source/User/IAD_Native.psc`, the shipped `data/F4SE/Plugins/ImmersiveArsenalDisplays/DefaultConfig.json`, localization JSON under `data/F4SE/Plugins/ImmersiveArsenalDisplays/Localization/`, build definitions, and public documentation.

Runtime and generated state is not part of source publication. This includes `ActiveConfig.json`, `IAD_Settings.json`, `ImmersiveArsenalDisplays.ini`, DLL/PDB/PEX files, build caches, and personal or test profiles. `ActiveConfig.json` is created and maintained at runtime; `DefaultConfig.json` is the source-controlled first-launch configuration.
