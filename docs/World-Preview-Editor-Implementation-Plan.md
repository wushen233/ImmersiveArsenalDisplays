# IAD World Preview Editor Implementation Plan

Status: Phase 1 through Phase 4 and the first editor-layout pass complete; full-display projection fix built and synced; user-confirmed runtime move/resize validation passed
Last updated: 2026-09-05

## Goal

Turn the existing in-world CME/MOV markers into a practical editing preview:

- the real player actor remains visible in the current game scene;
- the preview can be repositioned, rotated, and zoomed with the camera-style
  controls used by model editors;
- the Slots and Nodes windows automatically decide which marker family is shown;
- dragging a marker axis edits the selected local CME/MOV configuration;
- temporary preview movement is always restored when the session ends.

## Reference and Boundary

`LooksmenuPlayerRotation` is the behavioral reference for controlling the player
actor while LooksMenu is open:

- repository: https://github.com/P-K-0/LooksmenuPlayerRotation
- event/input reference: https://raw.githubusercontent.com/P-K-0/LooksmenuPlayerRotation/main/events.cpp
- lifecycle reference: https://raw.githubusercontent.com/P-K-0/LooksmenuPlayerRotation/main/events.h

IAD does not copy its runtime addresses or implementation. The reference is used
only to validate the interaction model: save actor state on entry, apply a
temporary transform while editing, and restore the exact state on exit.

## Architecture

### PreviewSession

Owns the temporary player heading, free-camera preview state, and lifecycle. Its
snapshot includes the player pointer, position, rotation, 3D root, camera
anchor, and IAD scene generation. It is the single owner of restoration.

### PreviewInputRouter

Routes input by priority:

1. ImGui widgets and movable windows.
2. CME/MOV gizmo drag, which writes configuration through the existing editor.
3. Empty preview viewport: rotate the actor, pan the free camera in screen
   space, and change camera-relative distance with the wheel.

The first implementation uses the existing ImGui input path. A dedicated F4SE
input handler is only needed if a later runtime test shows that raw mouse input
is required for a specific camera state.

### PreviewViewport / Anchor

The actor is a real game-world object, so the preview uses the live scene and
the game's free-camera state rather than a drawn clone. Marker projection uses
the complete ImGui display coordinate space; the focused Slots, Nodes, or
Visualizer window only selects the marker family and ImGui window hit testing
prevents controls from consuming preview input. Moving or resizing an editor
window therefore does not change the projection domain. The game camera at the
moment editing is enabled is the initial camera basis; the persistent CME/MOV
transform remains a separate state domain.

### PreviewScene Adapter

`IPreviewSceneAdapter` is the scene Interface consumed by ImGui and
`PreviewSession`. It accepts a copied `PreviewSceneSnapshot` with the
`ActorDisplayIdentity`, captures a projection frame, returns world-to-screen
results and camera basis data, and owns preview camera enter/apply/restore
operations. `PreviewScene` selects the active Adapter in one place.

The current `UIWorldPreviewCamera` is the live-camera Fallout 4 Adapter. A
`DetachedPreviewScene` Adapter now owns an IAD-named Fallout 4
`Interface3D::Renderer`, a screen display mesh, and a sanitized actor clone;
its create/attach/retire sequence is game-thread-only and identity-gated. The
live Adapter remains the default until the detached renderer's visual output
and pane placement are field-tested.

## State Invariants

- `previewTransform` is temporary and must never be serialized.
- `configTransform` is persistent and is changed only by the existing editor
  transaction/writeback path.
- Every completed axis drag records one pre-edit transaction; cancel restores
  the active drag and undo pops completed drags in reverse order, including the
  MOV Override Transform flag. The in-session history is bounded to 64
  transactions.
- Entry takes one complete snapshot; repeated frames never overwrite it.
- Exit is idempotent and restores only the actor captured by that session.
- A load, main-menu transition, death, player 3D replacement, or scene-generation
  change aborts the session before touching stale scene nodes.
- All actor mutation and restoration is consumed by the existing game-thread
  update loop. The ImGui/render thread only publishes the latest command and
  never calls `F4SE::TaskInterface::AddTask` for interactive samples.
- Right-drag changes only the temporary player heading. Middle-drag and the
  mouse wheel change `FreeCameraState::translation`; player world position is
  not used as the pan/zoom target and is never written by the preview session.
- The session owns the free-camera state only when it entered that state. On
  close it exits the state on the game thread, then restores the player heading
  and refreshes IAD displays.
- Input capture is released when the menu closes, even if a drag is active.
- The preview projection is refreshed every ImGui frame against the complete
  ImGui display; marker drawing and gizmo hit testing use the same coordinates.
  Editor-window movement changes focus and UI occlusion only, never the
  projection bounds or the game camera.
- The actor display snapshot carries actor FormID, global scene generation,
  monotonic actor 3D generation, and publish sequence. A replacement scene
  invalidates selected records, active drag state, edit history, and projected
  pointer caches before the editor accepts another scene write.
- Preview rendering and input are visible only while the IAD menu is open. The
  editor starts enabled by default and resumes when the menu is reopened.

## Lifecycle

```text
Inactive -> Entering -> Active -> Exiting -> Inactive
                |          |          ^
                +----------+----------+
             invalid scene / load / death / 3D replacement
```

`Entering` validates player, camera, 3D, and scene state. `Active` owns the
snapshot and consumes preview gestures. `Exiting` restores the actor and asks
IAD to refresh display transforms. Invalid scene state follows the same restore
path before becoming inactive.

## Input Semantics

- Left drag on a visible axis: edit that axis in the existing CME/MOV config.
- Right drag on empty preview space: rotate the actor around vertical axis.
- Middle drag: pan the actor relative to the camera.
- Mouse wheel: camera-relative zoom, clamped to a practical preview range.
- Shift: fine movement/rotation scale.
- Ctrl: coarse axis movement scale (4x).
- Ctrl+Shift: extra-fine axis movement scale (0.025x).
- Each continuous axis drag is one edit transaction. The editor exposes the
  current transaction state, can cancel the active drag, and can undo completed
  drags in reverse order including the MOV Override Transform state.
- Escape or closing the IAD menu: restore and end the session.

When the IAD menu is open, the right mouse button belongs to the preview
session. The old camera handoff path is not used by the preview editor, so
input context and cursor state are restored only by the normal menu lifecycle.

## Implementation Checklist

### Phase 0 - Design

- [x] Confirm real-actor preview rather than an offscreen clone.
- [x] Compare the interaction model with LooksmenuPlayerRotation.
- [x] Separate temporary actor transform from persistent CME/MOV transform.
- [x] Define lifecycle invalidation and restoration rules.

### Phase 1 - Session and Temporary Actor Transform

- [x] Add `UIWorldPreviewSession`.
- [x] Capture and restore player transform.
- [x] Queue actor mutations on the game thread.
- [x] Detect load, death, scene generation, and 3D-root invalidation.
- [x] Integrate open/close and preview checkbox lifecycle.

### Phase 2 - Preview Input

- [x] Add actor yaw, pan, and camera-relative zoom gestures.
- [x] Resolve input priority between UI, gizmo, and viewport.
- [x] Release all capture state on menu close and abnormal exit.

### Phase 3 - Window and Viewport Coordination

- [x] Add a full-display marker layer behind the Visualizer and editor windows.
- [x] Keep the game camera unchanged when the active Slots/Nodes window moves or changes.
- [x] Keep the configuration panel opaque and the viewport unobstructed.

### Phase 4 - Editor Integration

- [x] Auto-select CME/MOV marker family from the focused editor window.
- [x] Select a marker and open the corresponding editor window.
- [x] Switch the active marker family without closing the other editor window;
  preserve complete managed keys so list selection stays synchronized.
- [x] Drag XYZ axes into the existing local config writeback path.
- [x] Add explicit transaction status and undo/cancel for multi-gesture edits,
  including automatic local MOV Override creation and rollback.
- [x] Publish game-thread snapshots of visible MOV model bounds for selectable
  equipment outlines; prefer sampled projected mesh contours and keep CME nodes
  as point/gizmo markers.
- [x] Show transaction actions at the bottom of the Visualizer and add Ctrl+Z.
- [x] Keep full managed runtime keys compatible with unprefixed configuration
  names so a world click selects the same item in the Slots/Nodes editor.
- [x] Make model contour snapshots read-only and avoid `UpdateWorldBound()` in
  the UI collection path.
- [x] Apply CME/MOV preview transforms through a coalesced game-thread request;
  slot physics cannot overwrite an active MOV drag.
- [x] Avoid per-sample global evaluation/refresh/save requests during a drag and
  reuse projected screen geometry once per ImGui frame.
- [x] Add Shift/Ctrl/Ctrl+Shift fine, coarse, and extra-fine movement modifiers.
- [x] Invalidate stale preview selections and edit transactions when the actor
  3D root or model scene generation changes.

### Phase 5 - Verification and Release

- [x] Build all configured CommonLibF4 profiles.
- [x] Static-check lifecycle and input invariants.
- [x] Sync the staged data image to the MO2 overlay.
- [x] User-confirmed marker projection remains aligned after editor window
  movement/resizing.
- [x] Define the `IPreviewSceneAdapter` seam and route ImGui/session calls
  through the active `PreviewScene` Adapter.
- [x] Verify basic independent DX11 target creation on the selected AE device
  without changing the active game render target.
- [x] Add an opt-in detached Adapter with owned Interface3D renderer, display
  mesh, cloned scene, identity gate, and main-thread retirement.
- [x] Clear the offscreen scene and screen-attached root before releasing the
  detached renderer.
- [ ] Runtime-test selection and drag-state invalidation after a player 3D
  replacement or load.
- [ ] Runtime-test detached clone visibility, skinning, lights, and target
  retirement across menu close, load, death, and 3D replacement.
- [ ] Validate the display-mesh crop/placement against a moved or resized
  ImGui editor window before selecting the detached Adapter by default.
- [ ] Test menu close, Escape, load, death, teleport, first/third person,
  window movement, actor yaw, pan, zoom, and CME/MOV writeback in-game.
- [ ] Update user-facing documentation after runtime acceptance.

### Phase 6 - Editor Workspace Layout

- [x] Collapse scope, target, and configured-target controls by default while
  keeping a one-line context summary visible.
- [x] Move slot, node, and custom preset/batch operations into disclosure
  sections so the object list and inspector stay near the top of the window.
- [x] Reorganize the Visualizer into a compact current-selection bar, quick
  editor actions, preview controls, visual layers, node monitor, and advanced
  coordinate sections.
- [x] Keep transaction controls at the bottom of the Visualizer and preserve
  existing Ctrl+Z/cancel behavior.
- [ ] Add a shared inspector surface that can host Slot and Node details without
  opening separate editor windows.
- [ ] Replace the remaining debug-oriented visualizer options with a dedicated
  presentation/style section.

## Progress Log

| Date | Change | Result |
| --- | --- | --- |
| 2026-09-02 | Design recorded; CommonLibF4 APIs and IAD lifecycle seams reviewed | Ready for Phase 1 implementation |
| 2026-09-02 | Added `UIWorldPreviewSession`, game-thread transform queue, lifecycle abort/restore, and preview gestures | Build and MO2 sync passed; in-game validation pending |
| 2026-09-02 | Changed rotation to right-drag and removed per-sample `Update3DPosition(true)` | Freeze-risk path reduced; runtime regression test pending |
| 2026-09-02 | Moved preview command consumption into `HolsterManager::Update()` and coalesced position/angle writes | Render-thread task submission removed; first-publish/first-apply log markers added; runtime regression test pending |
| 2026-09-02 | Routed preview position apply and restore through game-thread `Actor::SetPosition(..., true)` | Prevents the actor controller from overwriting pan/zoom changes; runtime regression test pending |
| 2026-09-02 | Replaced actor-position pan/zoom with game `FreeCameraState` takeover and gated updates on visible ImGui state | Player position is no longer moved; menu close cannot restart the preview session; runtime regression test pending |
| 2026-09-02 | Corrected free-camera basis mapping and changed zoom to camera-forward movement with distance clamping | Horizontal pan no longer uses depth axis; zoom no longer orbits toward the actor's feet; runtime regression test pending |
| 2026-09-02 | Added focused-window viewport calculation, marker clipping, and preview guide | Phase 3 build and static checks passed; window move/focus runtime validation pending |
| 2026-09-02 | Fixed preview camera initialization to use the rendered `NiCamera::world.translate`; removed automatic viewport-to-camera re-anchoring so the game camera remains the editing baseline | Build, static checks, and MO2 sync passed; runtime validation pending |
| 2026-09-02 | Enabled world preview editing by default, hid preview rendering while the menu is closed, and made CME/MOV selection switch the corresponding editor window | Build, static checks, and MO2 sync passed; runtime validation pending |
| 2026-09-02 | Added per-drag edit transactions with explicit status, active-drag cancel, and bounded multi-step undo history; the pre-edit baseline includes MOV Override Transform | Static checks, releasedbg build, and MO2 sync passed; runtime validation pending |
| 2026-09-02 | Kept Slots and Nodes windows open during world selection, restored complete managed selection keys, materialized inherited MOV definitions into the current scope on drag, and made rollback remove those temporary overrides | Static checks, releasedbg build, and MO2 sync passed; runtime validation pending |
| 2026-09-02 | Added game-thread model-bound snapshots, selectable MOV model outlines, bottom transaction controls, and Ctrl+Z | Static checks, releasedbg build, and MO2 sync passed; runtime validation pending |
| 2026-09-02 | Fixed full managed-key selection matching in live Slots/Nodes panels; replaced scene-mutating bound refresh with sampled world-space mesh vertices, projected convex contours, and polygon hit testing | Static checks, releasedbg build, and MO2 sync passed; runtime validation pending |
| 2026-09-02 | Corrected FO4 packed-half vertex decoding with a local bounds-checked decoder; removed the engine `UnpackVertexData` call after the runtime crash path reached `FindIntersectionsTriShapeFastPath`. Added a 120-tick grace window after save loads and 3D-root replacement before reading clone renderer buffers | Static checks, releasedbg build, and MO2 sync passed; runtime validation pending |
| 2026-09-02 | Routed live CME/MOV drag values through a coalesced game-thread preview request; MOV slot physics is suspended only for the active preview session. Removed per-mouse-sample global refresh/evaluation/save calls, cached screen projection results per ImGui frame, and added fine/coarse drag modifiers | Static checks, releasedbg build, and MO2 sync passed; runtime validation pending |
| 2026-09-03 | Replaced single-entry undo state with a bounded 64-entry transaction history; Ctrl+Z now undoes completed drags in reverse order | All three configured CommonLibF4 profiles built; static checks, releasedbg build, and MO2 sync passed; in-game validation pending |
| 2026-09-03 | Added allow-list Nexus release packaging with clean default INI/settings and an archive manifest | Candidate packaging is repeatable; final release remains gated by the remaining in-game checklist |
| 2026-09-03 | Promoted the first-launch display snapshot to source-controlled `DefaultConfig.json`; added live PA detection and dedicated `*_Armor` node/model states | Ordinary-body and power-armor defaults are separated; runtime validation in power armor is pending |
| 2026-09-03 | Started Phase 6 editor workspace pass: compact scope context, collapsible preset/batch controls, Visualizer quick actions, grouped display layers, and bottom transaction actions | Build, static checks, and MO2 sync passed; in-game layout and interaction validation pending |
| 2026-09-03 | Fixed the first layout-pass regressions: removed the nested scope child scroll area, restored configured-target selection to a single row, and gave Visualizer layer controls unique ImGui IDs | Static ID review and build validation pending |
| 2026-09-05 | Removed the layout-derived side-pane projection and clipping after runtime screenshots showed markers moving with editor-window position and width; focus observation remains only for automatic CME/MOV marker-family selection | Static checks, releasedbg build, and MO2 sync passed; in-game move/resize retest pending |
| 2026-09-05 | Added `PreviewScene`/`IPreviewSceneAdapter` and routed ImGui/session preview calls through the live-camera Adapter; added the AE DX11 capability probe | Static checks, releasedbg build, and MO2 sync passed; detached scene Adapter remains pending |
| 2026-09-05 | Added the opt-in `DetachedPreviewScene` Adapter using Fallout 4 `Interface3D::Renderer`; added owned display mesh/actor clone lifecycle, flattened-bone pose synchronization, and identity-gated main-thread retirement | Static checks and releasedbg build passed; live-camera Adapter remains the default pending in-game visibility and pane-placement validation |

## Risks and Rollback

- The actor is a live game object; only its temporary heading and free-camera
  runtime state are changed. No Fallout 4 installation `Data` file is written.
- The actor heading and camera ownership snapshot are the rollback boundary for
  every session; the camera is restored only when IAD entered free-camera mode.
- Runtime testing must start from a clean menu open and verify the player returns
  to the exact original position and heading after closing the editor.
- If a camera state does not expose a stable anchor, keep actor movement disabled
  for that state while retaining marker/config editing.
