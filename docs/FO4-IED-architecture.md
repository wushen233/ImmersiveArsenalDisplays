# FO4 IED Architecture Baseline

This document defines the Fallout 4-specific boundary for evolving Immersive Arsenal Displays. Skyrim IED is a behavioral reference only; its game APIs and node assumptions are not copied.

## Confirmed Fallout 4 Runtime Interfaces

| Concern | Fallout 4 interface | IAD rule |
|---|---|---|
| Equip transition | `RE::ActorEquipManagerEvent::Event` | Queue the event form, then resolve it to an inventory instance on the next safe scan. |
| Event identity | `itemAffected->object`, `stackID` | Resolve the matching `ActiveItem::uid` by FormID plus its 0-based inventory stack index; a form ID alone is not a persistent instance identity. |
| Inventory instance | `InventoryList` stack data, `BGSObjectInstanceExtra`, `ActiveItem::uid` | Display and history are per instance where an instance exists. |
| Actor scene root | `RE::TESObjectREFR::Get3D()` / `GetCurrent3D()` | Never attach or detach when the actor 3D is unavailable. |
| Biped state | `RE::BipedAnim::GetRoot()` / `GetBipObject()` | Use for state inspection only until a FO4-specific biped attachment path is validated. |
| Scene mutation | `F4SE::GetTaskInterface()->AddTask()` | All node mutation and model attachment occurs on the game task queue. |

## Selection Model

The normal display selection order is explicit:

1. Configured preferred items, in their configured list order. This is an unconditional override.
2. The slot's `SelectionMode`, applied only to candidates that pass the slot filters:
   - `LastEquipped` (default): the latest eligible concrete inventory instance from the equip-event cache.
   - `Strongest`: the highest current FO4 instance rating (weapon damage or armor rating).
   - `Random`: a stable pseudo-random order derived from actor, slot, form, and instance UID.
3. The first regular candidate in configured form-type order when the selected mode yields no valid item.

`SelectionMode` follows the shape declared by IED's configuration data. The IED reference snapshot does not contain an active `Strongest` implementation, so IAD's strongest rating is a Fallout 4 extension rather than a claimed IED behavior. Legacy slot presets using `SelectInventoryStrongest: true` migrate to `SelectionMode: Strongest`; a missing mode defaults to `LastEquipped`.

Custom rules retain their independent strongest and random flags. Last Equipped Custom rules retain their own recent-equip, biped-slot, and recent-display rules; they never fall through to strongest ranking.

Custom rules may optionally set `TargetDisplaySlot`. When set, the rule can only assign its matching item to that exact display slot and still must pass that slot's filters. An empty value preserves automatic slot selection. This makes item-specific placement explicit without allowing a Custom to bypass a slot's weapon-category policy.

`DisplayFormWithoutInventory` is an explicit static Custom mode. If no eligible inventory instance exists, IAD resolves the Custom target Form and asynchronously loads a direct NIF through `BSModelDB`. It requires `TargetDisplaySlot`, still applies that slot's type, form, keyword, and condition filters, and deliberately does not apply instance OMODs. For FO4 modular `WEAP` forms, IAD first creates engine-managed default instance data and lets the game assemble the default weapon; it never reads a player-owned weapon's OMODs. `ModelSwapPath` remains available when a rule needs a specific prebuilt display mesh.

When multiple Last Equipped Custom rules can claim the same display slot, IAD resolves them deterministically: Custom priority first, then the newest matching equip record, then rule name as a stable tie-breaker. A repeated evaluation therefore cannot alternate two otherwise equal rules.

## Actor State Rules

- Equip events update last-equipped history after the following inventory scan resolves the actual instance.
- A repeated scan must not reorder last-equipped history just because Fallout 4 reports multiple stacks as equipped.
- Display history stores both `item UID -> slot name` and `item UID -> display sequence`.
- Actor state is retired only after the actor is no longer active; retirement must not touch scene nodes off the game task queue.
- Global physics, effect-shader, light, and NPC-display switches are independent. Disabling effects or lights applies equally to slot models, Customs, and model groups; disabling lights also strips existing light nodes from newly assembled display models.
- NPC inventory evaluation is round-robin and defaults to one eligible NPC every four update ticks. `NPCEvaluationIntervalTicks` is clamped to 1-60 and only changes NPC selection work; player refreshes, scene attachment, and physics continue every update tick.
- When NPC displays are disabled, IAD culls both current and deferred-retirement NPC models once, resets their IAD physics state, and skips subsequent active-effect polling, inventory evaluation, node work, and transform updates. Re-enabling queues a consolidated refresh for the active NPC set.
- The transform pass never creates actor caches. Actors without an existing IAD node or display-slot state return before scene lookup, physics, or transform work; evaluation remains the only path that creates actor display state.
- Active actor collection uses FormID identity and preserves player-first ordering, so duplicate process-list handles cannot run IAD state, transform, or physics work twice in one update tick.
- The asynchronous model queue intentionally consumes one FIFO request per update tick. Every request carries a scene generation, and stale-generation requests are discarded before model assembly; attachment callbacks perform further actor, slot, item UID, and request-signature validation.
- `RuntimeSelection` values are reset to their documented defaults before `ActiveConfig.json` is parsed. A missing field in a migrated or manually reduced configuration therefore cannot retain a previous reload's in-memory state. INI-owned editor and global equipment-mode values remain outside that reset boundary.
- Slot form filters support IED-style named profile references. `UseProfile: true` resolves `ProfileName` at candidate-evaluation time, so editing and saving the named form-filter profile updates every linked slot. A missing or malformed profile fails that slot's form-filter check instead of broadening its candidate set. Existing inline filters remain the default and profile Apply/Merge still copies data for a self-contained slot.
- Direct in-game loads increment the scene generation at `kPreLoadGame`. After the replacement player 3D is fully loaded, IAD clears all tracked display state with scene detachment enabled, clears the node cache, and evaluates the rebuilt skeleton on the following update. Main-menu teardown remains a separate path where the old actor tree is already owned by the engine and only non-owning state is reset.
- `TESSwitchRaceCompleteEvent` invalidates the actor cache and display state before the next evaluation, so reconstructed FO4 skeletons never reuse old node pointers.
- Async model completions validate actor ID, slot key, UID, and request signature before attaching a model.
- `KeyBindState` conditions are polled only for key combinations referenced by active configuration. A press or release queues one consolidated actor refresh, so conditional visibility and selection do not wait for an unrelated inventory event.
- `NodeMonitor` conditions inspect the actor's live FO4 3D tree. The selector supports object, node, geometry, and node-with-geometry-child tests, with optional recursive descent and hidden-object inclusion.
- Skyrim-only condition names (dual wield, arrest, horse/mount, and XP32 skeleton checks) are excluded from the editor. A legacy manual configuration using one of those names evaluates the condition as `false` deterministically. `IsInVertibird` remains supported as the Fallout 4-specific vehicle-state condition, even though its engine implementation uses mount-state fields internally.
- Active-effect conditions read FO4's `MagicTarget::GetActiveEffectList()` without mutating it. `HasActiveEffect` matches a currently active spell or source form, `HasMagicEffect` matches the active MGEF form, and `HasSpell` matches the active spell form. Inactive, removed, dispelled, and worn-off entries are ignored.
- `QuestStage` matches a FO4 `QUST` FormID against a numeric stage expression such as `>=20` or `==100`. IAD polls only quests referenced by active configuration and queues one consolidated refresh when a referenced stage changes.
- Runtime variables are typed: `RuntimeVariable` evaluates boolean state while `RuntimeNumberVariable` evaluates a float against the same comparison syntax used by other numeric conditions (`>=`, `>`, `<=`, `<`, `==`, `!=`). Papyrus setters queue a coalesced refresh.
- Conditional variables provide an IED-style automatic source for the existing runtime variables. They evaluate ordered player-context rules and write a boolean, number, FormID, or model-path value only when it changes; the resulting change queues one consolidated refresh. A matching rule stops evaluation by default; enabling `ContinueAfterMatch` permits a later matching rule to override its output.
- A FormID conditional variable may use a fixed FormID or the player's currently equipped `WEAP` form. The latter is a Fallout 4-safe equivalent for the common IED dynamic-form use case; it returns `0` when no weapon is equipped and does not consume or mutate inventory.

## Processing Stages

The intended FO4 equivalent of IED's processor phases is:

| Stage | Frequency | Work |
|---|---|---|
| High | Per update tick | Consume queued equip/container events, process player state, attach completed model tasks. Player selection requests are coalesced for 8 ticks. |
| Medium | Every 4 update ticks | Evaluate one nearby NPC inventory and slot assignment round-robin. |
| Low | Infrequent | Retire inactive actor state, clear stale caches, refresh global conditions. |

The update loop follows these phases without changing the validated display-selection behavior.

## Static Regression Check

Run the following from the IAD project root after changing the condition editor, condition evaluator, or `RuntimeSelection` settings:

```powershell
.\scripts\Test-StaticInvariants.ps1
```

The check verifies that every condition exposed in the editor has a runtime evaluator, legacy Skyrim-only names remain compatibility-only, all four global settings remain serialized, and the model effect/light paths still obey their global switches.
