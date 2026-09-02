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
