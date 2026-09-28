# Local Development

The public source repository is the `open-source` directory inside the
workspace project. It has its own Git history and remote:

```text
https://github.com/wushen233/ImmersiveArsenalDisplays.git
```

The outer project directory is the Fallout 4 workspace adapter. It owns the
MO2 install image, generated Papyrus output, test configurations, and local
build metadata. Do not commit those generated or machine-specific files to the
public source repository.

## Build From The Public Source

From `open-source`:

```powershell
.\scripts\build.ps1
```

The script finds the workspace `commonlibf4-wushen-main` profile when the
checkout is inside `FO4ModWorkspace`. For a standalone clone, pass an explicit
dependency path:

```powershell
.\scripts\build.ps1 -CommonLibF4Path C:\path\to\commonlibf4
```

The public xmake file also works with any compatible CommonLibF4 checkout when
`COMMONLIBF4_PATH` is set. The DLL and PDB are copied to the local `data`
directory for debugging, but generated binaries remain ignored by Git.

## Build And Deploy In The Workspace

From the outer project directory:

```powershell
.\scripts\build-and-sync.ps1
```

This builds the public source, compiles the Papyrus source, and synchronizes
the project `data` image to the configured MO2 development mod. Use
`-SkipSync` when only local build outputs are needed.

The current test-only `IAD_Test_10mm.json` profile and the
`10mm_R_thigh_f.nif` test mesh are not part of the public repository or release
archive.

The public repository also carries the canonical `DefaultConfig.json`. It is
the shipped first-launch display setup, while `ActiveConfig.json` remains a
runtime-generated user copy. When the current MO2 `ActiveConfig.json` contains
new formal slot, node, or custom rules, promote its `Data` section into the
default snapshot from the outer project directory:

```powershell
.\scripts\Update-DefaultConfig.ps1 `
    -SourceSnapshot 'D:\path\to\ImmersiveArsenalDisplays Dev\F4SE\Plugins\ImmersiveArsenalDisplays\ActiveConfig.json' `
    -FallbackSnapshot '.\data\F4SE\Plugins\ImmersiveArsenalDisplays\Exports\IAD_DefaultConfigUser.json'
```

The promotion deliberately excludes `RuntimeSelection` and `Debug` session
state. It retains the exported form-filter profiles when ActiveConfig does not
serialize them, preserves formal Data entries, removes the known treatment-kit
test entry and copied editor node, and clears the excluded `10mm_R_thigh_f.nif`
test asset reference. Review those exclusions if a new test entry is intended
to become a release feature. On repeated promotions, formal custom entries
already present in the canonical default are retained by target FormID, while
the current ActiveConfig wins when the same item exists in both files.

The default includes dedicated power-armor states. Each CME node switches to
the vanilla `*_Armor` host nodes while the model transform is stored separately
from the normal-body transform, so later preview edits do not cross-contaminate
the two equipment contexts.

## Build A Nexus Release Candidate

Release packaging is driven by an explicit allow-list from the outer project
data image. It keeps the developer's `ActiveConfig.json`, `IAD_Settings.json`,
and `ImmersiveArsenalDisplays.ini`, along with test profiles and PDB,
NIF, and Papyrus source out of the archive while retaining the default display
snapshot required for a first launch:

```powershell
..\scripts\package-release.ps1 -Version 3.0.0 -PackageName ImmersiveArsenalDisplays-3.0.0-Nexus-candidate -Force
```

The script writes the candidate directory and ZIP under the workspace `dist`
directory and records the DLL SHA256 in `Release-Manifest.txt`. This is a
release candidate until the remaining in-game checklist in
`World-Preview-Editor-Implementation-Plan.md` is accepted.

## Update GitHub

Always inspect the source repository before editing it:

```powershell
git status --short --branch
git pull --ff-only
```

Edit C++ under `src`, Papyrus source under `data/Scripts/Source/User`, and
documentation under `docs`. Run the build and static checks before committing:

```powershell
.\scripts\build.ps1
powershell -ExecutionPolicy Bypass -File .\scripts\Test-StaticInvariants.ps1
git diff --check
git status --short
```

Then publish the tested source:

```powershell
git add README.md LICENSE .gitignore xmake.lua data docs scripts src
git commit -m "Describe the tested change"
git push origin main
```

The workspace helper can inspect this independent repository without touching
the workspace root history:

```powershell
..\..\..\scripts\project-git.ps1 -ProjectName ImmersiveArsenalDisplays -Scope source status
```

Before changing CommonLibF4, verify the pinned commit and dependency license in
`docs/CommonLibF4-Build-Notes.md`.
