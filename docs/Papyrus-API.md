# IAD Papyrus API

`IAD_Native` exposes session-only runtime controls for Fallout 4 scripts. Every setter queues a consolidated IAD refresh; it does not write `ActiveConfig.json`.

```papyrus
IAD_Native.SetBooleanVariable("IAD_CombatDisplay", true)
IAD_Native.SetNumberVariable("IAD_DisplayStage", 2.0)
IAD_Native.SetModelPathVariable("IAD_BackModel", "Meshes\\Weapons\\Rifle.nif")
IAD_Native.SetFormVariable("IAD_TestTargetForm", Game.GetPlayer().GetEquippedWeapon())
```

Use `RuntimeVariable` in a condition tree for boolean state, `RuntimeNumberVariable` for numeric comparisons such as `>=2`, a model-swap variable source for model paths, and `Use Runtime Target Form` in a Custom rule for Form values.

## Native functions

| Function | Purpose |
| --- | --- |
| `SetBooleanVariable(string, bool)` | Set a condition-tree boolean variable. |
| `GetBooleanVariable(string)` | Read a condition-tree boolean variable. |
| `SetNumberVariable(string, float)` | Set a numeric condition-tree variable. |
| `GetNumberVariable(string)` | Read a numeric condition-tree variable. |
| `SetModelPathVariable(string, string)` | Set a runtime model path. |
| `GetModelPathVariable(string)` | Read a runtime model path. |
| `SetFormVariable(string, Form)` | Set a runtime target/model Form; `None` clears it. |
| `GetFormVariable(string)` | Read a runtime Form. |
| `ClearVariable(string)` | Remove all runtime values with that name. |
| `ForceRefresh()` | Re-evaluate active IAD actors without changing a value. |

The declaration is installed as `Scripts/IAD_Native.pex`; source is included at `Scripts/Source/User/IAD_Native.psc`.
