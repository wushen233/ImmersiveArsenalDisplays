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

## Assignment Resolver Boundary

`AssignmentResolver` is the policy Module for the first half of the IED-style two-step assignment flow. It owns deterministic slot ordering, global candidate ordering, per-slot form-type fallback ordering, `LastEquipped`/`Strongest`/`Random` candidate ordering, shared ordinary-slot eligibility, and the evaluation-local `AssignmentResult`. `AssignmentResult` enforces one assignment per slot and provides Custom assignment counting without retaining state beyond the current evaluation. `HolsterManager` supplies Fallout 4 runtime context such as recent-equip scores, named form-filter profile resolution, condition evaluation, and black-hole checks, then commits the result and performs all model or scene mutation.

The resolver returns pointers into the evaluation-local candidate vector only; it does not retain them across an evaluation and does not touch actor nodes. `SlotEligibilityContext` keeps filter policy in the resolver while injecting FO4-only runtime facts through callbacks. `AssignmentResult` contains only the selected item, optional Custom owner, equipped-instance flag, and assignment source; it does not decrement inventory counts, mark scene objects, or change actor nodes. This preserves the current FO4 identity rules (`ActiveItem::uid`, FormID, and stack data) while making the evaluate-then-commit boundary explicit.

## Actor Runtime Context Boundary

`ActorRuntimeContext` is the runtime-data Seam for one actor evaluation. `HolsterManager` publishes one coherent, value-copied `ActorRuntimeSnapshot` after collecting runtime settings, candidate identity data, scoped slot and Custom configuration, and the current node state. The snapshot exposes FormID, stack ID, UID, count, equipment/favorite flags, and rating for candidates; it does not publish `RE::TESForm*`, inventory-stack pointers, `Actor*`, or `NiNode*` ownership across the Seam.

The snapshot identity is the actor FormID plus the global scene generation and actor 3D generation. `AcquireFor` accepts a snapshot only when all three values still match, so a load, actor 3D replacement, or retired actor cannot feed stale assignment inputs into a later consumer. `ActorRuntimeContext` retains only the latest snapshot, and invalidation is explicit on blocked, disabled-NPC, and dead-actor exits. `HolsterManager` remains the Fallout 4 Adapter and owns inventory locks, native object resolution, history, task queues, and scene mutation.

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
- The asynchronous model queue consumes one request per update tick. The newest player-owned request is selected ahead of stale player entries so weapon switches are not delayed by obsolete scans; NPC and unowned requests retain FIFO order when no player request is pending. Every request carries a scene generation, and stale-generation requests are discarded before model assembly; attachment callbacks perform further actor, slot, item UID, and request-signature validation.
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
| High | Per update tick | Consume queued equip/container events, process player state, and attach completed model tasks. The newest player model request takes priority over stale queued player work. |
| Medium | Every 4 update ticks | Evaluate one nearby NPC inventory and slot assignment round-robin. |
| Low | Infrequent | Retire inactive actor state, clear stale caches, refresh global conditions. |

The update loop follows these phases without changing the validated display-selection behavior.

## IED-Derived IAD Module Seams

The current implementation keeps the IED-inspired function boundaries explicit while retaining Fallout 4-specific implementations:

- `UIWorldPreviewCamera` owns the copied projection frame, camera basis, and the Fallout 4 live-camera Adapter. ImGui marker drawing never retains a live `NiCamera` pointer.
- `ActorDisplayContext` owns the latest copied debug nodes, model bounds, boxes, and settings snapshot for ImGui. Its identity includes the owning actor FormID, the model scene generation, a monotonic actor 3D generation, and a publish sequence. `HolsterManager` assembles one `ActorDisplaySnapshot` on the game thread and moves it across the Seam, so it remains the scene owner without retaining duplicate published buffers.
- `ConditionRefreshPolicy` owns cadence/history for active-effect signatures, named and legacy keybind states, quest stages, and conditional variables. It returns value results to `HolsterManager`, which retains actor selection, task submission, and scene mutation.
- `ProfileRuntimeContext` owns copied Transform, Physics, and valid FormFilter runtime snapshots. `GlobalProfileManager` publishes them after profile lifecycle changes; `HolsterManager` resolves named presets through value-returning methods instead of touching mutable profile records during actor evaluation.
- `RuntimeConfigSnapshot` is the coherent scope-resolution Interface for one actor evaluation. `ConfigManager` resolves Slot, Node, and Custom entries under one configuration lock; `HolsterManager` consumes the copied result instead of opening three independent scope-resolution windows.
- `ActorDisplayLifecycle` owns the replacement and retirement boundaries for current/deferred weapon, holster, and model-group clones. Replacements cull before queueing, old arrays remain hidden until the shared grace window expires, and retirement is independent of MOV-node availability; dynamic arrays hide stale tail entries when their active count shrinks.
- `NodeManager` increments the actor 3D generation whenever it observes a replacement root. ImGui invalidates the selected node/slot, active drag, edit history, and projected pointer caches when that identity changes, so scene replacement cannot write through a stale editor selection.
- `NodeManager::InvalidateForConfigRefresh` is the UI-edit boundary for target, transform, visibility, and state changes. It rebuilds cached CME/MOV bindings while preserving the current actor 3D identity; full cache clearing remains reserved for load and scene teardown boundaries, so clicking a detail option does not cancel the selected record.
- `AssignmentResolver` owns deterministic slot-priority, candidate, form-type, selection-mode ordering, shared eligibility, and the evaluation-local `AssignmentResult`. `HolsterManager` supplies FO4 runtime callbacks, decrements consumed inventory instances, records display history, and commits model/scene mutation.
- `UIInspectorNavigation` owns the window-shell navigation state shared by slot, node, and custom editors. It initializes persisted indices once, applies one-shot context-menu requests, activates tab changes, and consumes requests centrally, so switching the selected record does not reset the active detail tab or section.
- `UIRecordSelectionState` owns the window-shell selection identity shared by slot, node, and custom list/detail paths. It centralizes IAD managed-prefix matching, rename continuity, and identity-aware clearing so preview picking, list rows, deletion, scene invalidation, and detail lookup agree on the active record.
- `UIEditorInteraction` owns shared ImGui pane interaction: splitter dragging, pane-width clamping, and deferred live-config commit tracking. The detail drawers consume this Module instead of carrying separate local implementations for Slots, Nodes, Customs, filters, and profile editors.
- `UIProfileEditorStateStore` owns the persistent per-editor state for managed profile selection, name/filter buffers, descriptions, status text, and selection continuity. The profile editor and inline profile selectors share this state Module while their record-specific drawing remains in `UIConfigWindows`.
- `UIManagedProfileControls` owns the shared managed-profile interaction Module: initialization, selection, refresh, create, save, apply, merge, parser status, and description presentation. Domain editors supply only their profile data and copy/merge/refresh policy; the settings page uses the same Interface for conditional-variable profiles.
- `UIProfileWorkflow` owns the FormFilter persistence and reference-repair Adapter. Save/reload/rename/delete operations update the managed cache, repair or clear Fallout 4 slot references, request the configuration commit, and trigger runtime refresh through one lifecycle path shared by both FormFilter editors.
- `UIEditCoordinator` owns the UI edit-propagation Module. Its Interface distinguishes config dirty recording, INI dirty recording, changes that also require a runtime refresh, and immediate commit, so UI editors do not couple each path directly to `UICommitManager` and `HolsterManager`.
- `UIConditionTreeSignature` owns deterministic condition-tree serialization for dirty detection. Slot, Custom, model-group, and profile editors use its Interface instead of maintaining recursive signature logic beside the ImGui renderer.
- `UIConditionTreeEditor` owns recursive condition-tree drawing and mutation, including group operations, condition-specific fields, recursive deletion, keyword scanning, and current Node/Slot shortcuts. `UIConfigWindows` supplies top-level profile controls and refresh policy through its small Interface.
- `UIFormEditorControls` owns shared Fallout 4 value editors: FormID lookup/display, FormID sets and ordered vectors, string vectors, form-type ordering, and FO4 Biped-slot normalization. Detail editors consume the Module while retaining their own domain-specific refresh and commit policy.
- `UITransformEditorControls` owns shared three-axis transform and RGBA color editing, including active-axis feedback consumed by the world preview. Callers retain the selected config object and refresh/commit policy.
- `UIModelEditorControls` owns shared model cleanup, animation, Effect Shader, Extra Light, and conditional model-swap variable-source controls. It composes the transform/color Module while Slot, Custom, and model-group drawers retain domain-specific signatures and runtime refresh policy.
- `Preview-Scene-Capability-Audit.md` records the current CommonLibF4 DX11/BSGraphics boundary. `DetachedPreviewScene` now provides an opt-in Fallout 4 detached-scene Adapter through `Interface3D::Renderer`; the live-camera Adapter remains the default until runtime render and pane-placement validation is complete. Both implementations stay behind the preview scene Seam, so renderer selection does not leak into editor input or assignment policy.

## Static Regression Check

Run the following from the IAD project root after changing the condition editor, condition evaluator, or `RuntimeSelection` settings:

```powershell
.\scripts\Test-StaticInvariants.ps1
```

The check verifies that every condition exposed in the editor has a runtime evaluator, legacy Skyrim-only names remain compatibility-only, all four global settings remain serialized, the model effect/light paths still obey their global switches, inspector navigation remains a window-shell Module rather than selected-record state, record selection has one centralized identity boundary, condition dirty detection uses `UIConditionTreeSignature`, shared FormID/vector editors are owned by `UIFormEditorControls`, transform/color input is owned by `UITransformEditorControls`, model-display controls are owned by `UIModelEditorControls`, managed profile selectors are owned by `UIManagedProfileControls`, FormFilter persistence and reference repair use `UIProfileWorkflow`, edit propagation uses `UIEditCoordinator`, and the configuration UI does not use full node-cache clearing.
