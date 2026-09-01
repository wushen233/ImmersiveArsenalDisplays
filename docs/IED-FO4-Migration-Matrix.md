# IED to Fallout 4 Migration Matrix

This document records the IED backend concepts that are portable to IAD and the
Fallout 4-specific adaptation chosen for each one.  It is a planning document,
not an API compatibility claim.

| IED capability | IAD status | Fallout 4 adaptation |
| --- | --- | --- |
| Global, race, NPC, and actor override layers | Implemented | IAD resolves the same named entry from actor -> NPC -> race -> global scope. |
| Equipment slots and per-entry configuration | Implemented | Arbitrary named display slots replace Skyrim's fixed weapon, shield, and ammo categories. |
| Preferred items and candidate selection | Implemented | Preferred items keep their explicit order; slots then use Last Equipped, Strongest, Random, or normal candidate selection. Strongest is an IAD FO4 extension, not a claimed IED behavior. |
| Form filter profiles | Implemented | Slots can copy a profile or dynamically reference it with `UseProfile` and `ProfileName`. Missing or malformed references fail closed because FO4 generic slots have no fixed item category guard. |
| Profile lifecycle | Implemented | Form-filter profile edits refresh displays immediately. Rename updates dynamic slot references; delete clears the reference and keeps the slot's inline filter data. |
| Actor display block list | Implemented | The settings page provides an IED-style player display toggle with a configurable hotkey plus a persistent actor FormID block list. Blocked actors keep their engine-owned IAD nodes but all display clones are culled and physics is suspended. |
| Condition profiles and conditional variables | Implemented | IAD uses FO4 actor, weapon, armor, combat, mount, quest, and runtime-variable conditions. Skyrim-only condition values remain legacy-inert. |
| Active-effect refresh policy | Implemented | IAD polls only effect FormIDs actually referenced by `HasActiveEffect`, `HasMagicEffect`, `HasSpell`, or automatic conditional-variable rules. Unrelated transient effects no longer trigger inventory reevaluation. |
| Key-bound display conditions | Implemented | IAD now supports IED-style named bindings with a state sequence of `0..N`, optional combo key, condition comparisons, F4SE save serialization, and load/revert state isolation. Legacy direct virtual-key conditions remain compatible. |
| Last-equipped display history | Implemented | Uses FO4 equip events, inventory candidates, biped occupancy, display-slot history, and recent-acquired fallback. |
| Node overrides and attachment transforms | Implemented | FO4 actor skeleton nodes, named IAD intermediary nodes, transforms, physics, effect shaders, lights, model groups, and state overrides are supported. |
| Skeleton matching and extra nodes | Implemented | IAD uses FO4 race/NPC/node/skeleton-path checks plus configured skeleton extensions. |
| Runtime profile and preset editor | Implemented | Shared editor/profile data model with copy, apply, merge, metadata, and configuration snapshots. |
| Background object cache | Deferred | FO4 weapon assembly depends on OMOD state and temporary references. Existing stable engine-owned clone handling takes priority over a cache rewrite. |
| Object sounds | Deferred | IAD intentionally strips sound and collision extras from display clones to prevent furniture/Havok interaction. A safe opt-in sound system needs its own lifetime model. |
| Outfit manager | Not portable | IED edits Skyrim outfits and equipment. IAD stays display-only and must not mutate FO4 NPC gameplay equipment or save state. |
| Fixed Skyrim item categories and arrow/shield logic | Not portable | Fallout 4 equipment and weapon types are selected through FO4 form types, keywords, item filters, and arbitrary slots. |
| Skyrim animation graph and effect APIs | Partial | IAD supports FO4-safe animation-event forwarding, effect shader configuration, lights, and model groups; Skyrim-specific graph behavior is excluded. |

## Near-Term Order

1. Finish regression coverage for dynamic profile references and profile lifecycle.
2. Audit node and model lifecycle against stable engine-owned-clone rules before adding render features.
3. Evaluate an optional FO4-safe display cache only after proving that assembled OMOD weapons remain isolated from inventory `ExtraDataList` instances.
