# IAD Preview Scene Capability Audit

Last updated: 2026-09-05

## Scope

This audit records the Fallout 4/CommonLibF4 surfaces that are relevant to an
IED-style detached preview scene. The audit is an implementation boundary, not
a claim that an old Skyrim or raw F4SE address-based renderer can be reused.

## Available Building Blocks

- `RE::BSSceneGraph` and `RE::SceneGraph` expose a scene-graph owner with a
  `NiCamera`, culling state, and the standard `NiAVObject` hierarchy.
- `RE::NiCamera` exposes copied world-to-camera state, viewport data, cloning,
  and the CommonLibF4 world-to-screen helper needed for a projection adapter.
- `RE::BSGraphics::RendererData` exposes the selected Fallout 4 DX11 device,
  context, and render-target resource structures. These are useful for
  capability detection and a future adapter boundary.
- `RE::Interface3D::Renderer` exposes Fallout 4's supported offscreen 3D
  renderer lifecycle: renderer creation, screen-attached display geometry,
  offscreen scene attachment, render-target sizing, camera access, enable, and
  release. The existing workspace PIP-OS implementation is a field-proven
  reference for the ordering of these calls.
- IAD's `ModelManager` already creates temporary model nodes through
  `NiCloningProcess::kCopyExact`, strips runtime-only state, and associates
  asynchronous requests with a scene generation.
- `ModelManager::CloneRenderOnly` provides an owned render-only clone lifecycle
  for the detached preview. `DetachedPreviewScene` refreshes the clone's
  flattened bone locals from the live actor on each game-thread frame because
  the clone deliberately has no controllers.

## Current Boundary

The selected CommonLibF4 profile does provide the high-level
`Interface3D::Renderer` surface needed to own a Fallout 4 offscreen scene. It
is the supported IAD boundary for the detached Adapter. IAD still does not call
the older raw F4SE `BSGraphics`/`RenderTargetManager` examples found in
reference sources; those depend on runtime-specific addresses and expose no
complete scene/target teardown contract.

`Interface3D::Renderer` is sufficient to establish independent renderer and
clone ownership. It does not by itself prove that an offscreen texture can be
cropped into an arbitrary ImGui pane, nor that every actor skin/controller
variant clones correctly. Those remain explicit runtime gates.

The active path remains an explicit live-camera Adapter behind
`UIWorldPreviewCamera`. `UIWorldPreviewSession` owns temporary camera/input
lifecycle, while `ActorDisplayContext` supplies copied marker state to ImGui.
This preserves a replaceable seam without making the UI depend on live
`NiCamera` or model pointers.

## Implemented Capability Probe

`PreviewRenderCapabilities` now runs on the ImGui render thread after the
current device and context have been acquired. It records renderer
initialization, device/context availability, swap-chain dimensions, the
current feature level, and whether the renderer exposes a swap-chain target.
For the independent-target portion it creates a temporary 1x1
`R8G8B8A8_UNORM` texture with render-target and shader-resource binds, creates
an RTV and SRV, then releases all three objects immediately. This validates
the selected device's basic independent-resource ownership without replacing
the game's target, touching render state, or calling raw address-based
`RenderTargetManager` functions.

The result is cached per D3D11 device and logged only when capability state or
swap-chain dimensions change. It is diagnostic groundwork for the detached
scene Adapter; it is not yet a detached renderer and does not change the
current live-camera behavior.

## Implemented Preview Scene Interface

`PreviewScene` now owns the selection seam and exposes
`IPreviewSceneAdapter`. The Interface accepts a `PreviewSceneSnapshot` carrying
the copied `ActorDisplayIdentity` and actor position, captures a projection
frame, projects world points, provides camera basis data, and owns the
enter/apply/restore operations needed by the preview session. `ImGuiManager`
and `UIWorldPreviewSession` consume only `PreviewScene::GetActive()` and no
longer name or call the concrete camera implementation.

`UIWorldPreviewCamera` is currently the live-camera Fallout 4 Adapter behind
that seam. `PreviewScene::GetActive()` is intentionally the only place that
selects between the live and detached adapters, so the editor, input router,
and lifecycle restoration code remain renderer-independent. The Interface is
now real and build-verified.

## Implemented Detached Adapter Boundary

`DetachedPreviewScene` now owns an IAD-named `Interface3D::Renderer`, a cloned
screen display mesh, and a cloned/sanitized player scene. The lifecycle is
game-thread-only and is guarded by the copied actor/scene/3D identity. A scene
replacement invalidates the ready frame before the old offscreen scene is
cleared, the screen root is detached, the renderer is disabled/released, and
the clone pointers are dropped.

The clone path uses `NiCloningProcess::kCopyExact`, temporarily detaches live
controllers during cloning, strips runtime-only state through
`ModelManager::CloneRenderOnly`, re-owns shader fade-node links, and rebinds
available skin bone links to the clone. ImGui only copies the detached
renderer camera matrix after the engine has published the offscreen frame.

The detached Adapter is intentionally opt-in at this checkpoint. The live
camera remains the supported default until the detached render result and
screen placement are validated in-game; an unverified full-frame screen mesh
must not silently overlap the existing editor composition. The detached clone
is pose-synchronized on the game thread before its frame transform is
recomputed, while ImGui continues to consume only copied frame data.

## Implemented Lifecycle Guard

The display snapshot now carries:

- the actor FormID that owns the preview;
- the global model scene generation;
- a monotonic per-actor 3D generation that increments when `NodeManager`
  observes a replacement 3D root;
- a publish sequence for observing snapshot freshness.

When the identity changes, ImGui clears the selected node/slot, active drag,
edit history, projected pointer caches, and active gizmo axis. It does not issue
a write against the retired scene. The same guard covers detached clone
replacement and renderer retirement.

## Next Verification Gate

With the detached Adapter implemented, prove these facts on the selected AE
profile before making it the default:

1. The target texture and views can be created and retired on the correct render
   thread.
2. The cloned actor scene can render without mutating the actor's live scene,
   including skinning after a pose update.
3. Camera matrices and input coordinates remain independent of ImGui window
   movement and resizing.
4. Scene replacement, load, menu close, and device/resource teardown cannot
   leave a stale clone or target view behind.

Until that gate is met, the live-camera Adapter is the supported fallback.
