# CommonLibF4 Build Notes

IAD's current workspace lock references the `main` branch of
`Dear-Modding-FO4/commonlibf4` at commit
`adc0687f5c41a58308b5be2046644f7f4926424f`. That historical commit carries
the MIT License.

The current remote `main` branch has since moved to commit
`81f541e074bfbe95974fea62d45671fc3d4aa216` and carries GPL-3.0. The active
`wushen233/commonlibf4` remote `main` is also GPL-3.0. Do not describe the
current upstream branch as MIT; the MIT statement above applies only to the
locked historical commit used by the current workspace build.

The workspace profile used for the tested Fallout 4 build is
`wushen233/commonlibf4`, also on `main` at that same commit. The workspace
profile contains a local MSVC compatibility edit in `src/F4SE/API.cpp` that
undefines the Windows SDK `MAX_PATH` and `ERROR` macros before the
`REX::W32` declarations are parsed. This edit is outside the IAD repository
and is not represented as IAD source code.

The CommonLibF4 checkout is a build dependency, not a vendored part of IAD.
Keep its own `LICENSE` and notices when redistributing that dependency.
