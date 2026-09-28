param(
    [string]$ProjectRoot = (Split-Path -Parent $PSScriptRoot)
)

$ErrorActionPreference = 'Stop'

function Require([bool]$Condition, [string]$Message) {
    if (-not $Condition) {
        throw $Message
    }
}

$conditionSource = Get-Content (Join-Path $ProjectRoot 'src\Engine\ConditionSystem.cpp') -Raw
$conditionUiSource = Get-Content (Join-Path $ProjectRoot 'src\UI\UIConfigWindows.cpp') -Raw
$configHeaderSource = Get-Content (Join-Path $ProjectRoot 'src\Data\ConfigManager.h') -Raw
$configSource = Get-Content (Join-Path $ProjectRoot 'src\Data\ConfigManager.cpp') -Raw
$settingsSource = Get-Content (Join-Path $ProjectRoot 'src\UI\UISettingsWindow.cpp') -Raw
$holsterHeaderSource = Get-Content (Join-Path $ProjectRoot 'src\System\HolsterManager.h') -Raw
$holsterSource = Get-Content (Join-Path $ProjectRoot 'src\System\HolsterManager.cpp') -Raw
$modelManagerHeaderSource = Get-Content (Join-Path $ProjectRoot 'src\ModelManager.h') -Raw
$modelManagerSource = Get-Content (Join-Path $ProjectRoot 'src\ModelManager.cpp') -Raw
$nodeManagerSource = Get-Content (Join-Path $ProjectRoot 'src\Engine\NodeManager.cpp') -Raw
$nodeManagerHeaderSource = Get-Content (Join-Path $ProjectRoot 'src\Engine\NodeManager.h') -Raw
$mainSource = Get-Content (Join-Path $ProjectRoot 'src\main.cpp') -Raw
$imguiSource = Get-Content (Join-Path $ProjectRoot 'src\UI\ImGuiManager.cpp') -Raw
$imguiHeaderSource = Get-Content (Join-Path $ProjectRoot 'src\UI\ImGuiManager.h') -Raw
$windowShellSource = Get-Content (Join-Path $ProjectRoot 'src\UI\UIWindowShell.cpp') -Raw
$windowShellHeaderSource = Get-Content (Join-Path $ProjectRoot 'src\UI\UIWindowShell.h') -Raw
$editorContextSource = Get-Content (Join-Path $ProjectRoot 'src\UI\UIEditorContextStore.cpp') -Raw
$editorContextHeaderSource = Get-Content (Join-Path $ProjectRoot 'src\UI\UIEditorContext.h') -Raw
$editorContextStoreHeaderSource = Get-Content (Join-Path $ProjectRoot 'src\UI\UIEditorContextStore.h') -Raw
$inspectorNavigationSource = Get-Content (Join-Path $ProjectRoot 'src\UI\UIInspectorNavigation.cpp') -Raw
$inspectorNavigationHeaderSource = Get-Content (Join-Path $ProjectRoot 'src\UI\UIInspectorNavigation.h') -Raw
$recordSelectionSource = Get-Content (Join-Path $ProjectRoot 'src\UI\UIRecordSelectionState.cpp') -Raw
$recordSelectionHeaderSource = Get-Content (Join-Path $ProjectRoot 'src\UI\UIRecordSelectionState.h') -Raw
$editorInteractionSource = Get-Content (Join-Path $ProjectRoot 'src\UI\UIEditorInteraction.cpp') -Raw
$editorInteractionHeaderSource = Get-Content (Join-Path $ProjectRoot 'src\UI\UIEditorInteraction.h') -Raw
$profileEditorStateSource = Get-Content (Join-Path $ProjectRoot 'src\UI\UIProfileEditorState.cpp') -Raw
$profileEditorStateHeaderSource = Get-Content (Join-Path $ProjectRoot 'src\UI\UIProfileEditorState.h') -Raw
$conditionTreeSignatureSource = Get-Content (Join-Path $ProjectRoot 'src\UI\UIConditionTreeSignature.cpp') -Raw
$conditionTreeSignatureHeaderSource = Get-Content (Join-Path $ProjectRoot 'src\UI\UIConditionTreeSignature.h') -Raw
$conditionCatalogSource = Get-Content (Join-Path $ProjectRoot 'src\UI\UIConditionCatalog.cpp') -Raw
$conditionCatalogHeaderSource = Get-Content (Join-Path $ProjectRoot 'src\UI\UIConditionCatalog.h') -Raw
$conditionTreeEditorSource = Get-Content (Join-Path $ProjectRoot 'src\UI\UIConditionTreeEditor.cpp') -Raw
$conditionTreeEditorHeaderSource = Get-Content (Join-Path $ProjectRoot 'src\UI\UIConditionTreeEditor.h') -Raw
$formEditorControlsSource = Get-Content (Join-Path $ProjectRoot 'src\UI\UIFormEditorControls.cpp') -Raw
$formEditorControlsHeaderSource = Get-Content (Join-Path $ProjectRoot 'src\UI\UIFormEditorControls.h') -Raw
$transformEditorControlsSource = Get-Content (Join-Path $ProjectRoot 'src\UI\UITransformEditorControls.cpp') -Raw
$transformEditorControlsHeaderSource = Get-Content (Join-Path $ProjectRoot 'src\UI\UITransformEditorControls.h') -Raw
$modelEditorControlsSource = Get-Content (Join-Path $ProjectRoot 'src\UI\UIModelEditorControls.cpp') -Raw
$modelEditorControlsHeaderSource = Get-Content (Join-Path $ProjectRoot 'src\UI\UIModelEditorControls.h') -Raw
$managedProfileControlsHeaderSource = Get-Content (Join-Path $ProjectRoot 'src\UI\UIManagedProfileControls.h') -Raw
$profileWorkflowHeaderSource = Get-Content (Join-Path $ProjectRoot 'src\UI\UIProfileWorkflow.h') -Raw
$profileWorkflowSource = Get-Content (Join-Path $ProjectRoot 'src\UI\UIProfileWorkflow.cpp') -Raw
$commitSource = Get-Content (Join-Path $ProjectRoot 'src\UI\UICommitManager.cpp') -Raw
$editCoordinatorHeaderSource = Get-Content (Join-Path $ProjectRoot 'src\UI\UIEditCoordinator.h') -Raw
$editCoordinatorSource = Get-Content (Join-Path $ProjectRoot 'src\UI\UIEditCoordinator.cpp') -Raw
$previewEditorSource = Get-Content (Join-Path $ProjectRoot 'src\UI\UIWorldPreviewEditor.cpp') -Raw
$previewCameraSource = Get-Content (Join-Path $ProjectRoot 'src\UI\UIWorldPreviewCamera.cpp') -Raw
$previewSceneSource = Get-Content (Join-Path $ProjectRoot 'src\UI\PreviewScene.cpp') -Raw
$previewSceneHeaderSource = Get-Content (Join-Path $ProjectRoot 'src\UI\PreviewScene.h') -Raw
$detachedPreviewSource = Get-Content (Join-Path $ProjectRoot 'src\UI\DetachedPreviewScene.cpp') -Raw
$detachedPreviewHeaderSource = Get-Content (Join-Path $ProjectRoot 'src\UI\DetachedPreviewScene.h') -Raw
$displayContextSource = Get-Content (Join-Path $ProjectRoot 'src\System\ActorDisplayContext.cpp') -Raw
$displayContextHeaderSource = Get-Content (Join-Path $ProjectRoot 'src\System\ActorDisplayContext.h') -Raw
$conditionRefreshPolicySource = Get-Content (Join-Path $ProjectRoot 'src\System\ConditionRefreshPolicy.cpp') -Raw
$conditionRefreshPolicyHeaderSource = Get-Content (Join-Path $ProjectRoot 'src\System\ConditionRefreshPolicy.h') -Raw
$profileManagerSource = Get-Content (Join-Path $ProjectRoot 'src\Profile\ProfileManager.h') -Raw
$globalProfileManagerSource = Get-Content (Join-Path $ProjectRoot 'src\Profile\GlobalProfileManager.cpp') -Raw
$globalProfileManagerHeaderSource = Get-Content (Join-Path $ProjectRoot 'src\Profile\GlobalProfileManager.h') -Raw
$profileRuntimeContextSource = Get-Content (Join-Path $ProjectRoot 'src\System\ProfileRuntimeContext.cpp') -Raw
$profileRuntimeContextHeaderSource = Get-Content (Join-Path $ProjectRoot 'src\System\ProfileRuntimeContext.h') -Raw
$actorRuntimeSource = Get-Content (Join-Path $ProjectRoot 'src\System\ActorRuntimeContext.cpp') -Raw
$actorRuntimeHeaderSource = Get-Content (Join-Path $ProjectRoot 'src\System\ActorRuntimeContext.h') -Raw
$assignmentResolverSource = Get-Content (Join-Path $ProjectRoot 'src\System\AssignmentResolver.cpp') -Raw
$assignmentResolverHeaderSource = Get-Content (Join-Path $ProjectRoot 'src\System\AssignmentResolver.h') -Raw
$displayLifecycleSource = Get-Content (Join-Path $ProjectRoot 'src\System\ActorDisplayLifecycle.cpp') -Raw
$displayLifecycleHeaderSource = Get-Content (Join-Path $ProjectRoot 'src\System\ActorDisplayLifecycle.h') -Raw
$refreshSchedulerSource = Get-Content (Join-Path $ProjectRoot 'src\System\ActorRefreshScheduler.cpp') -Raw
$refreshSchedulerHeaderSource = Get-Content (Join-Path $ProjectRoot 'src\System\ActorRefreshScheduler.h') -Raw
$renderCapabilitiesSource = Get-Content (Join-Path $ProjectRoot 'src\UI\PreviewRenderCapabilities.cpp') -Raw
$renderCapabilitiesHeaderSource = Get-Content (Join-Path $ProjectRoot 'src\UI\PreviewRenderCapabilities.h') -Raw
$defaultConfigPath = Join-Path $ProjectRoot 'data\F4SE\Plugins\ImmersiveArsenalDisplays\DefaultConfig.json'
Require (Test-Path -LiteralPath $defaultConfigPath -PathType Leaf) 'Source-controlled DefaultConfig.json is missing.'
$defaultConfigSource = Get-Content $defaultConfigPath -Raw -Encoding UTF8
Require ($defaultConfigSource.Contains('动力甲：专用骨骼节点')) 'DefaultConfig has no power-armor node state overrides.'
Require ($defaultConfigSource.Contains('动力甲：专用模型变换')) 'DefaultConfig has no independent power-armor model transforms.'
foreach ($name in @('Pelvis_Armor', 'Back_Armor', 'LLeg_Thigh_Armor', 'RLeg_Thigh_Armor')) {
    Require ($defaultConfigSource.Contains('"TargetNode":  "' + $name + '"') -or $defaultConfigSource.Contains('"TargetNode": "' + $name + '"')) "DefaultConfig is missing power-armor target node $name."
}
$previewSource = Get-Content (Join-Path $ProjectRoot 'src\UI\UIWorldPreviewSession.cpp') -Raw
$loadConfigMatch = [regex]::Match(
    $configSource,
    'void ConfigManager::LoadConfig\(\)\s*\{(?<body>.*?)std::string activeConfigPath',
    [Text.RegularExpressions.RegexOptions]::Singleline)
Require $loadConfigMatch.Success 'ConfigManager::LoadConfig initialization block was not found.'
$loadConfigInitialization = $loadConfigMatch.Groups['body'].Value

$uiMatch = [regex]::Match(
    $conditionCatalogSource,
    'kConditionTypes = \{(?<items>.*?)\};',
    [Text.RegularExpressions.RegexOptions]::Singleline)
Require $uiMatch.Success 'Condition editor catalog was not found.'

$uiTypes = [regex]::Matches($uiMatch.Groups['items'].Value, '"([^"]+)"') |
    ForEach-Object { $_.Groups[1].Value } |
    Sort-Object -Unique
$engineTypes = [regex]::Matches($conditionSource, 'node\.type\s*==\s*"([^"]+)"') |
    ForEach-Object { $_.Groups[1].Value } |
    Sort-Object -Unique
$legacyMatch = [regex]::Match(
    $conditionSource,
    'bool IsSkyrimOnlyConditionType\(.*?\n\t\t\}',
    [Text.RegularExpressions.RegexOptions]::Singleline)
Require $legacyMatch.Success 'Legacy condition compatibility guard was not found.'
$legacyTypes = [regex]::Matches($legacyMatch.Value, '"([^"]+)"') |
    ForEach-Object { $_.Groups[1].Value } |
    Sort-Object -Unique

$missingEvaluators = $uiTypes | Where-Object { $_ -notin $engineTypes -and $_ -notin $legacyTypes }
$hiddenEvaluatorTypes = $engineTypes | Where-Object { $_ -notin $uiTypes -and $_ -notin $legacyTypes }
Require ($missingEvaluators.Count -eq 0) ('Condition editor types without evaluators: ' + ($missingEvaluators -join ', '))
Require ($hiddenEvaluatorTypes.Count -eq 0) ('Evaluator types hidden from the editor: ' + ($hiddenEvaluatorTypes -join ', '))

foreach ($name in @('EnableEquipmentPhysics', 'EnableModelEffects', 'EnableModelLights', 'EnableNPCDisplays', 'NPCEvaluationIntervalTicks')) {
    Require ($configSource.Contains('"' + $name + '"')) "RuntimeSelection does not serialize $name."
}

foreach ($name in @('BlockPlayerDisplays', 'BlockedActorFormIDs')) {
    Require ($configSource.Contains('"' + $name + '"')) "RuntimeSelection does not serialize $name."
}

Require ($settingsSource.Contains('enableModelLights')) 'Model light global setting is not exposed in the settings UI.'
Require ($holsterSource.Contains('if (!runtimeSettings.enableModelLights) effectiveLight.enabled = false;')) 'Main display light global gate is missing.'
Require ($holsterSource.Contains('if (!runtimeSettings.enableModelEffects) {')) 'Model group effect global gate is missing.'
Require ($holsterSource.Contains('groupCleanupPolicy.removeLights = !activeModelGroups[i].light.enabled;')) 'Model group light cleanup does not follow the effective light state.'
Require ($holsterSource.Contains('npcEvaluationIntervalTicks = std::clamp')) 'NPC evaluation interval is not clamped in the update loop.'
Require ($holsterSource.Contains('const bool processNPCDisplays = runtimeSettings.enableNPCDisplays;')) 'NPC display processing gate is missing.'
Require ($holsterSource.Contains('if (actor != player && !processNPCDisplays) continue;')) 'Disabled NPC displays still enter the actor update path.'
Require ($holsterSource.Contains('config->IsActorDisplayBlocked(a_actor)')) 'Actor display block list is not enforced by the display runtime.'
Require ($holsterSource.Contains('const bool suppressDisplays =')) 'Actor display block list is not enforced by the transform pass.'
Require ($settingsSource.Contains('屏蔽玩家展示 (IED Player Toggle)')) 'Player display block toggle is not exposed in the settings UI.'
Require ($settingsSource.Contains('玩家展示切换键')) 'Player display block hotkey is not exposed in the settings UI.'
Require ($configSource.Contains('"PlayerBlockToggleKey"')) 'Player display block hotkey is not persisted to INI.'
Require ((Get-Content (Join-Path $ProjectRoot 'src\UI\ImGuiManager.cpp') -Raw).Contains('playerBlockHotkey != 0')) 'Player display block hotkey is not handled by the window input hook.'
Require ((Get-Content (Join-Path $ProjectRoot 'src\UI\ImGuiManager.cpp') -Raw).Contains('SetPlayerDisplaysBlocked(!runtimeConfig->IsPlayerDisplaysBlocked())')) 'Player display block hotkey does not toggle state at task execution time.'
Require ($holsterSource.Contains('cull(state.oldModels);')) 'Disabled NPC displays do not cull deferred main models.'
Require ($holsterSource.Contains('cull(state.oldHolsters);')) 'Disabled NPC displays do not cull deferred holsters.'
Require ($holsterSource.Contains('cull(state.oldModelGroups);')) 'Disabled NPC displays do not cull deferred model groups.'
Require ($holsterSource.Contains('if (!a_actor || a_actor->IsDead(false) || a_actor->IsDeleted() || a_actor->IsDisabled()) {')) 'Global actor refresh can revive retired dead or disabled actors.'
Require ($holsterSource.Contains('if (nodeStatesIt == _actorNodeStates.end() && slotStatesIt == _actorDisplaySlots.end()) return;')) 'Transform pass creates empty actor state for actors without IAD data.'
Require ($holsterSource.Contains('std::unordered_set<RE::TESFormID> activeActorIDs;')) 'Active actor list is not deduplicated.'
Require ($holsterSource.Contains('if (activeActorIDs.emplace(a_actor->GetFormID()).second)')) 'Active actor deduplication does not use FormID identity.'
Require ($modelManagerHeaderSource.Contains('std::deque<AsyncModelRequest>')) 'Async model queue no longer supports latest-player request selection.'
Require ($modelManagerSource.Contains('if (it->actorFormID == playerFormID)')) 'Async model queue does not prioritize the newest player request.'
Require ($modelManagerSource.Contains('_asyncLoadQueue.erase(selected);')) 'Async model queue does not remove the selected request safely.'
Require ($modelManagerSource.Contains('req.actorFormID = a_actor->GetFormID();')) 'Actor-owned model requests do not carry their owner identity.'
Require ($holsterSource.Contains('RequestModelByPath(actorID, currentHolsterPath')) 'Holster path requests do not carry their owner identity.'
Require ($modelManagerSource.Contains('if (req.sceneGeneration != GetSceneGeneration())')) 'Async model queue no longer rejects stale scene-generation requests.'
Require ($modelManagerSource.Contains('g_sceneGeneration.fetch_add(1, std::memory_order_acq_rel);')) 'Pre-load no longer invalidates asynchronous model requests.'
Require ($nodeManagerSource.Contains('if (stateChanged || cache.root3D != root)')) 'Actor 3D-root replacement is no longer detected.'
Require ($nodeManagerSource.Contains('HolsterManager::GetSingleton()->ClearActorSlots(actorID, true);')) '3D-root replacement no longer clears actor display state without a scene detach.'
Require ($nodeManagerSource.Contains('HolsterManager::GetSingleton()->RequestEvaluate(actorID);')) '3D-root replacement does not queue actor display reconstruction.'
Require ($configSource.Contains('j.value("UseProfile", false)')) 'Form filter profile references are no longer deserialized.'
Require ($configSource.Contains('j["ProfileName"] = f.profileName;')) 'Form filter profile references are no longer serialized.'
Require ($holsterSource.Contains('ResolveSlotFormFilter')) 'Slot evaluation no longer resolves form filter profile references.'
Require ($holsterSource.Contains('ResolveRuntimeFormFilter(slotDef.itemFilter.profileName)')) 'Slot evaluation does not resolve the configured named form filter profile.'
Require ($mainSource.Contains('GlobalProfileManager::GetSingleton().LoadAll();')) 'Profiles are not loaded before runtime selection begins.'
Require ($profileManagerSource.Contains('using ChangeFn = std::function<void()>;')) 'Profile manager has no change notification contract.'
Require ($profileManagerSource.Contains('void SetChangedCallback(ChangeFn a_callback)')) 'Profile manager does not expose its change notification seam.'
Require ($profileManagerSource.Contains('NotifyChanged();')) 'Profile manager mutations do not notify the owning profile boundary.'
Require ($profileRuntimeContextHeaderSource.Contains('struct ProfileRuntimeSnapshot')) 'Runtime profile data has no copied snapshot contract.'
Require ($profileRuntimeContextHeaderSource.Contains('class ProfileRuntimeContext')) 'Runtime profile data has no explicit context Module.'
Require ($profileRuntimeContextSource.Contains('_snapshot = std::move(a_snapshot);')) 'Runtime profile context does not take ownership of a published snapshot.'
Require ($globalProfileManagerHeaderSource.Contains('void RefreshRuntimeSnapshot();')) 'Global profile manager does not expose runtime snapshot publication.'
foreach ($method in @('ResolveRuntimeTransform', 'ResolveRuntimePhysics', 'ResolveRuntimeFormFilter')) {
    $type = if ($method -eq 'ResolveRuntimeTransform') { 'TransformData' } elseif ($method -eq 'ResolveRuntimePhysics') { 'PhysicsValues' } else { 'FormFilter' }
    Require ($globalProfileManagerHeaderSource.Contains('std::optional<' + $type + '> ' + $method)) "Global profile runtime Interface is missing $method."
    Require ($globalProfileManagerSource.Contains('GlobalProfileManager::' + $method)) "Global profile runtime Implementation is missing $method."
}
Require ($globalProfileManagerSource.Contains('ProfileRuntimeContext::GetSingleton().Publish(std::move(snapshot));')) 'Global profile manager does not publish the copied runtime snapshot.'
Require ($globalProfileManagerSource.Contains('!record.parserErrors')) 'Malformed FormFilter profiles are not excluded from the runtime snapshot.'
Require ($globalProfileManagerSource.Contains('[IAD Profile] runtime snapshot published')) 'Runtime profile snapshot publication has no diagnostic record.'
Require ($holsterSource.Contains('ResolveRuntimeTransform(a_state.targetTransformPreset)')) 'Runtime transform preset resolution bypasses the profile runtime seam.'
Require ($holsterSource.Contains('ResolveRuntimePhysics(a_state.targetPhysicsPreset)')) 'Runtime physics preset resolution bypasses the profile runtime seam.'
Require (-not $holsterSource.Contains('manager.Load();')) 'HolsterManager still lazy-loads mutable profile storage during runtime evaluation.'
Require (-not $holsterSource.Contains('FormFilters().Find')) 'HolsterManager still reads mutable FormFilter records during runtime evaluation.'
Require ($configSource.Contains('GetActiveEffectConditionFormIDsSnapshot')) 'Active-effect condition form IDs are not collected from the active configuration.'
Require ($conditionRefreshPolicyHeaderSource.Contains('struct ConditionRefreshResult')) 'Condition refresh policy has no result contract.'
Require ($conditionRefreshPolicyHeaderSource.Contains('class ConditionRefreshPolicy')) 'Condition refresh policy has no explicit policy Module.'
Require ($conditionRefreshPolicySource.Contains('ConditionRefreshPolicy::GetActiveEffectSignature(')) 'Active-effect signature is no longer owned by the condition refresh policy.'
Require ($conditionRefreshPolicySource.Contains('std::binary_search(a_watchedFormIDs.begin(), a_watchedFormIDs.end(), a_formID)')) 'Active-effect signature does not ignore unrelated effects.'
Require ($configSource.Contains('GetKeyBindDefinitionsSnapshot')) 'Named keybind definitions are not exposed to the runtime.'
Require ($configSource.Contains('jMaster["RuntimeSelection"]["Keybinds"]')) 'Named keybind definitions are not persisted in ActiveConfig.'
Require ($conditionRefreshPolicySource.Contains('KeyBindStateManager::GetSingleton()->Update(keyBindDefinitions)')) 'Named keybind states are not updated by the condition refresh policy.'
Require ($holsterSource.Contains('_conditionRefreshPolicy.Update(_currentUpdateTick, player)')) 'HolsterManager does not consume the condition refresh policy result.'
Require (-not $holsterSource.Contains('GetKeyBindConditionKeysSnapshot')) 'HolsterManager still owns named keybind condition sampling.'
Require (-not $holsterSource.Contains('GetQuestStageConditionFormIDsSnapshot')) 'HolsterManager still owns quest-stage condition sampling.'
Require ($conditionSource.Contains('KeyBindStateManager::GetSingleton()->GetState(a_key, state)')) 'KeyBindState conditions do not read named multi-state bindings.'
Require ($mainSource.Contains("serialization->SetUniqueID('IAD3')")) 'Named keybind state serialization is not registered.'
Require ((Get-Content (Join-Path $ProjectRoot 'src\UI\ImGuiManager.cpp') -Raw).Contains('KeyBindStateManager::GetSingleton()->ProcessKeyEvent')) 'Named keybinds are not fed from physical window key events.'
Require ($conditionUiSource.Contains("out += filter.useProfile ? '1' : '0';")) 'Form filter profile-reference mode is omitted from the editor dirty signature.'
Require ($conditionUiSource.Contains('AppendSignatureString(out, filter.profileName);')) 'Form filter profile name is omitted from the editor dirty signature.'
Require ($conditionUiSource.Contains('UIProfileWorkflow::ReloadFormFilter(managedProfiles, m_selectedProf, m_filterData)')) 'Legacy form filter editor does not synchronize runtime profile data after save or reload.'
Require ($configSource.Contains('bool ConfigManager::RenameFormFilterReferences')) 'Form filter profile rename does not repair slot references.'
Require ($configSource.Contains('bool ConfigManager::ClearFormFilterReferences')) 'Form filter profile deletion does not clear slot references.'
Require ($profileWorkflowSource.Contains('config->RenameFormFilterReferences(a_oldName, a_newName)')) 'Form filter profile rename is not wired to slot-reference maintenance.'
Require ($profileWorkflowSource.Contains('config->ClearFormFilterReferences(a_name)')) 'Form filter profile deletion is not wired to slot-reference maintenance.'
Require ($holsterSource.Contains('a_event.itemCount == 0')) 'Container changes still discard removal events before refreshing the source actor.'
Require ($holsterSource.Contains('a_event.oldContainerFormID')) 'Container changes do not inspect the source container actor.'
Require ($holsterSource.Contains('s_lastInventorySignature')) 'Player inventory polling does not detect same-stack count changes.'
Require ($configSource.Contains('resetActiveConfigState')) 'Config candidates do not have an isolated reset boundary.'
Require ($configSource.Contains('MoveFileExA')) 'ActiveConfig is not replaced through an atomic Windows file move.'
Require ($configSource.Contains('TryParsePoint3')) 'Vector config parsing does not validate array shape before indexing.'
Require ($configSource.Contains('value.at(0).get<float>()')) 'Vector config parsing does not use checked JSON array access.'
Require ($configSource.Contains('GetRuntimeSettingsSnapshot')) 'Runtime configuration reads do not use a synchronized snapshot.'
Require ($configHeaderSource.Contains('struct RuntimeConfigSnapshot')) 'Scope-resolved runtime configuration has no copied snapshot contract.'
Require ($configSource.Contains('RuntimeConfigSnapshot ConfigManager::GetRuntimeConfigSnapshot(RE::Actor* a_actor)')) 'ConfigManager has no coherent scoped runtime configuration resolver.'
Require ($holsterSource.Contains('config->GetRuntimeConfigSnapshot(a_actor)')) 'HolsterManager resolves scoped configuration through separate mutable reads.'
Require (-not $holsterSource.Contains('config->ResolveNodesWithScope(a_actor)')) 'HolsterManager still resolves Node configuration through the legacy path.'
Require (-not $holsterSource.Contains('config->ResolveSlotsWithScope(a_actor)')) 'HolsterManager still resolves Slot configuration through the legacy path.'
Require (-not $holsterSource.Contains('config->ResolveCustomsWithScope(a_actor)')) 'HolsterManager still resolves Custom configuration through the legacy path.'
Require ($conditionSource.Contains('RE::PowerArmor::ActorInPowerArmor')) 'Power-armor condition does not use the live game state helper.'
Require ($holsterSource.Contains('runtimeSettings.enableNPCDisplays')) 'Update loop still reads mutable runtime settings directly.'
Require ($imguiSource.Contains('InstallCursorHooks')) 'ImGui manager does not install cursor API hooks.'
Require ($imguiSource.Contains('ClipCursor_Hook')) 'ImGui manager does not intercept game cursor clipping.'
Require ($imguiSource.Contains('SetCursorPos_Hook')) 'ImGui manager does not suppress game cursor recentering.'
Require ($imguiSource.Contains('GetWindowRect')) 'ImGui manager does not refresh the full game-window cursor bounds.'
Require ($imguiSource.Contains('UICommitManager::GetSingleton().FlushIfDue();')) 'ImGui frame does not flush deferred UI configuration commits.'
Require ($imguiSource.Contains('UICommitManager::GetSingleton().DrawStatus();')) 'ImGui menu does not expose pending configuration status.'
Require ($commitSource.Contains('kCommitDebounce')) 'UI configuration commit debounce interval is missing.'
Require ($commitSource.Contains('config->SaveConfig();')) 'UI commit manager does not own ActiveConfig persistence.'
Require ($commitSource.Contains('config->SaveINISettings();')) 'UI commit manager does not own INI persistence.'
Require ($editCoordinatorHeaderSource.Contains('class UIEditCoordinator')) 'UI edit propagation has no explicit Module Interface.'
foreach ($method in @('RequestConfigChange', 'RequestConfigSave', 'RequestINIChange', 'RequestINISettingsSave', 'RequestRuntimeRefresh', 'CommitNow')) {
    Require ($editCoordinatorHeaderSource.Contains('static void ' + $method)) "UI edit coordinator Interface is missing $method."
    Require ($editCoordinatorSource.Contains('UIEditCoordinator::' + $method)) "UI edit coordinator Implementation is missing $method."
}
Require ($editCoordinatorSource.Contains('UICommitManager::RequestConfigSave();')) 'UI edit coordinator does not route config dirty state through UICommitManager.'
Require ($editCoordinatorSource.Contains('UICommitManager::RequestINISettingsSave();')) 'UI edit coordinator does not route INI dirty state through UICommitManager.'
Require ($editCoordinatorSource.Contains('HolsterManager::GetSingleton()->ForceRefreshAll();')) 'UI edit coordinator does not route runtime refresh through HolsterManager.'
foreach ($source in @($conditionUiSource, $settingsSource, $previewEditorSource, $imguiSource)) {
    Require (-not $source.Contains('HolsterManager::GetSingleton()->ForceRefreshAll();')) 'A UI module bypasses UIEditCoordinator for runtime refresh.'
    Require (-not $source.Contains('UICommitManager::RequestConfigSave();')) 'A UI module bypasses UIEditCoordinator for config dirty state.'
    Require (-not $source.Contains('UICommitManager::RequestINISettingsSave();')) 'A UI module bypasses UIEditCoordinator for INI dirty state.'
}
Require ($previewSource.Contains('void UIWorldPreviewSession::ProcessGameThread()')) 'World preview has no game-thread command consumer.'
Require ($holsterSource.Contains('UI::UIWorldPreviewSession::GetSingleton().ProcessGameThread();')) 'Holster update loop does not consume world preview commands.'
Require ($previewSource.Contains('void UIWorldPreviewSession::ProcessPendingCameraEnter()')) 'World preview has no game-thread camera takeover.'
Require ($previewCameraSource.Contains('camera->ToggleFreeCameraMode(false);')) 'The Fallout 4 camera Adapter does not use the game free-camera state.'
Require ($previewCameraSource.Contains('freeState->translation = a_cameraPosition;')) 'The Fallout 4 camera Adapter does not apply FreeCameraState translation.'
Require ($previewCameraSource.Contains('m_frame.basis.right = Normalize({ rotation.entry[2][0], rotation.entry[2][1], rotation.entry[2][2] }')) 'World preview horizontal pan has no copied camera-right basis.'
Require ($previewSource.Contains('Scale(forward, distance - nextDistance)')) 'World preview zoom is not constrained to the camera forward axis.'
Require ($previewCameraSource.Contains('return camera->world.translate;')) 'The Fallout 4 camera Adapter does not snapshot the rendered camera world position.'
Require (-not $previewSource.Contains('SetViewportAnchor')) 'World preview still accepts an automatic camera anchor from the UI layout.'
Require (-not $previewSource.Contains('ApplyViewportAnchor')) 'World preview still repositions the camera when the UI layout changes.'
Require (-not $previewSource.Contains('player->SetPosition(')) 'World preview pan/zoom still moves the player actor.'
Require (-not $previewSource.Contains('player->data.location')) 'World preview pan/zoom still writes the player reference location.'
Require ($imguiSource.Contains('m_isVisible.load(std::memory_order_acquire) && debugSettings.worldPreviewEdit')) 'World preview can restart after the ImGui menu closes.'
Require ($imguiSource.Contains('const bool menuVisible = m_isVisible.load(std::memory_order_acquire);')) 'Preview rendering does not track the ImGui menu visibility state.'
Require ($imguiSource.Contains('if (menuVisible && (debugSettings.worldPreviewEdit')) 'Preview rendering is not hidden while the ImGui menu is closed.'
Require ($imguiSource.Contains('const ImVec2 viewportMin{ 0.0f, 0.0f };')) 'World preview projection does not start at the full display origin.'
Require ($imguiSource.Contains('const ImVec2 viewportMax = screenSize;')) 'World preview projection does not use the full ImGui display size.'
Require (-not $imguiSource.Contains('GetPreviewViewport(')) 'ImGui still derives projection from an editor-window viewport.'
Require (-not $imguiSource.Contains('RenderViewport(')) 'ImGui still renders the removed side-pane preview frame.'
Require (-not $previewSource.Contains('F4SE::GetTaskInterface()')) 'World preview submits tasks from the ImGui/render path.'
Require (-not $previewSource.Contains('->AddTask')) 'World preview submits tasks from the ImGui/render path.'
$previewEditorSource = Get-Content (Join-Path $ProjectRoot 'src\UI\UIWorldPreviewEditor.cpp') -Raw
$previewEditorHeaderSource = Get-Content (Join-Path $ProjectRoot 'src\UI\UIWorldPreviewEditor.h') -Raw
Require ($previewEditorSource.Contains('void UIWorldPreviewEditor::BeginViewportFrame')) 'World preview editor does not reset its viewport each ImGui frame.'
Require ($previewEditorSource.Contains('void UIWorldPreviewEditor::NotifyEditorWindow')) 'World preview editor does not retain focused editor-window state.'
Require (-not $previewEditorSource.Contains('ObserveEditorWindow')) 'World preview editor still exposes layout-derived window observation.'
Require (-not $previewEditorSource.Contains('const bool preserveViewport')) 'World preview editor still preserves a layout-derived side viewport.'
Require (-not $previewEditorSource.Contains('PushClipRect(m_viewport.min, m_viewport.max, true)')) 'World preview markers are still clipped to a layout-derived viewport.'
Require (-not $previewEditorSource.Contains('m_viewport.source')) 'World preview editor still couples projection to an editor-window source.'
Require ($previewEditorSource.Contains('!IsInPreviewViewport(mouse)')) 'World preview gizmo input is not restricted to the display viewport.'
Require ($previewEditorHeaderSource.Contains('kVisualizer')) 'Visualizer window is not represented in the world preview window state.'
Require ($imguiSource.Contains('BeginViewportFrame(ImGui::GetIO().DisplaySize)')) 'ImGui frame does not initialize the world preview viewport.'
Require (-not $previewEditorSource.Contains('uiShowSlots = false;')) 'CME selection still closes the Slots editor.'
Require (-not $previewEditorSource.Contains('uiShowNodes = false;')) 'MOV selection still closes the Nodes editor.'
Require ($previewEditorSource.Contains('GetNodeEditorContext().selection.Select(a_node.name);')) 'CME selection does not preserve the managed node key for UI synchronization.'
Require ($previewEditorSource.Contains('GetSlotEditorContext().selection.Select(a_node.name);')) 'MOV selection does not preserve the managed slot key for UI synchronization.'
Require ($previewEditorHeaderSource.Contains('const std::vector<DebugBoundSphere>& a_modelBounds')) 'World preview editor has no model-bound input snapshot.'
Require ($previewEditorSource.Contains('EnsureLocalSlotOverride')) 'MOV gizmo drag cannot materialize an editable local override.'
Require ($previewEditorSource.Contains('ImGuiKey_Z')) 'World preview editor has no Ctrl+Z shortcut.'
Require ($previewEditorSource.Contains('FindModelAt')) 'World preview editor has no model-outline hit testing.'
Require ($previewEditorSource.Contains('AddPolyline')) 'World preview editor does not draw the projected model contour.'
Require ($conditionUiSource.Contains('IsSameManagedName')) 'Configuration windows do not normalize managed runtime names during world selection.'
Require ($holsterHeaderSource.Contains('std::string slotName;')) 'Model bound snapshots do not retain their owning MOV slot.'
Require ($holsterHeaderSource.Contains('worldVertices')) 'Model bound snapshots do not retain sampled mesh geometry.'
Require ($displayContextHeaderSource.Contains('void Publish(ActorDisplaySnapshot a_snapshot);')) 'Actor display context does not own a value-based snapshot publication Interface.'
Require ($displayContextSource.Contains('m_snapshot = std::move(a_snapshot);')) 'Actor display context does not take ownership of the published snapshot.'
Require ($holsterHeaderSource.Contains('std::mutex debugSettingsMutex;')) 'HolsterManager does not expose a dedicated debug-settings lock.'
Require ($holsterSource.Contains('displaySnapshot.boxes = std::move(newBoxes);')) 'HolsterManager does not transfer debug boxes into the display snapshot.'
Require ($holsterSource.Contains('displaySnapshot.nodes = std::move(newNodes);')) 'HolsterManager does not transfer debug nodes into the display snapshot.'
Require ($holsterSource.Contains('displaySnapshot.modelBounds = std::move(newBoundSpheres);')) 'HolsterManager does not transfer model bounds into the display snapshot.'
Require (-not $holsterHeaderSource.Contains('activeDebugBoxes')) 'HolsterManager still owns a duplicate published debug-box cache.'
Require (-not $holsterHeaderSource.Contains('activeDebugNodes')) 'HolsterManager still owns a duplicate published debug-node cache.'
Require (-not $holsterHeaderSource.Contains('activeDebugBoundSpheres')) 'HolsterManager still owns a duplicate published model-bound cache.'
Require (-not $holsterHeaderSource.Contains('nativeDebugBoundSpheres')) 'HolsterManager still exposes an obsolete native model-bound cache.'
Require (-not $holsterSource.Contains('debugBoxMutex')) 'HolsterManager still uses the obsolete debug-box mutex name.'
Require ($holsterSource.Contains('a_object->IsTriShape()')) 'Model bound snapshots do not collect render mesh vertices.'
Require ($holsterSource.Contains('DecodePackedHalf')) 'Model bound snapshots do not decode packed FO4 vertex positions locally.'
Require ($holsterSource.Contains('ReadPackedPosition')) 'Model bound snapshots do not bounds-check packed vertex reads.'
Require (-not $holsterSource.Contains('RE::BSGraphics::Utility::UnpackVertexData')) 'Model bound snapshots must not call the engine vertex decoder.'
Require ($holsterSource.Contains('kPreviewContourSettleTicks')) 'Model contour sampling has no post-load/root-replacement settle window.'
Require (-not $holsterSource.Contains('a_model->UpdateWorldBound();')) 'Model bound snapshots must not mutate scene bounds during UI collection.'
Require ($previewEditorHeaderSource.Contains('struct EditTransaction')) 'World preview editor has no explicit edit transaction state.'
Require ($previewEditorSource.Contains('m_drag.before')) 'World preview drag does not capture a pre-edit transaction baseline.'
Require ($previewEditorSource.Contains('void UIWorldPreviewEditor::CancelCurrentEdit()')) 'World preview editor has no current-edit cancel path.'
Require ($previewEditorSource.Contains('void UIWorldPreviewEditor::UndoLastEdit()')) 'World preview editor has no last-edit undo path.'
Require ($previewEditorSource.Contains('void UIWorldPreviewEditor::DrawTransactionControls()')) 'World preview editor does not expose transaction controls.'
Require ($previewSceneHeaderSource.Contains('struct PreviewCameraBasis')) 'Preview camera basis is not represented by an explicit data type.'
Require ($previewCameraSource.Contains('bool UIWorldPreviewCamera::CaptureFrame')) 'Preview camera has no copied game-camera frame boundary.'
Require ($previewCameraSource.Contains('bool UIWorldPreviewCamera::WorldToScreen')) 'Preview projection is still embedded in the ImGui draw loop.'
Require ($previewSceneHeaderSource.Contains('class IPreviewSceneAdapter')) 'Preview scene adapter interface is not represented explicitly.'
Require ($previewSceneHeaderSource.Contains('struct PreviewSceneSnapshot')) 'Preview scene adapter does not accept an actor display snapshot.'
Require ($previewSceneSource.Contains('UIWorldPreviewCamera::GetSingleton()')) 'Preview scene does not provide the live-camera Fallout 4 adapter.'
Require ($imguiSource.Contains('PreviewScene::GetSingleton().GetActive()')) 'ImGui does not consume the preview scene adapter seam.'
Require ($previewSource.Contains('PreviewScene::GetSingleton().GetActive()')) 'Preview session does not consume the preview scene adapter seam.'
Require ($windowShellHeaderSource.Contains('class UIWindowShell')) 'ImGui window shell has no explicit Module Interface.'
foreach ($method in @('Register', 'Initialize', 'Reset', 'Draw', 'OpenWindowCount', 'GetTopLevelWindowTitle')) {
    Require ($windowShellHeaderSource.Contains($method)) "ImGui window shell Interface is missing $method."
}
foreach ($method in @('UIWindowShell::Register', 'UIWindowShell::Initialize', 'UIWindowShell::Reset', 'UIWindowShell::Draw', 'UIWindowShell::OpenWindowCount')) {
    Require ($windowShellSource.Contains($method)) "ImGui window shell Implementation is missing $method."
}
Require ($imguiHeaderSource.Contains('UIWindowShell m_windowShell')) 'ImGui manager does not delegate window ownership to the shell.'
Require ($imguiSource.Contains('m_windowShell.Draw()')) 'ImGui manager does not draw through the window shell.'
Require ($imguiSource.Contains('m_windowShell.OpenWindowCount()')) 'ImGui manager does not obtain open-window count from the shell.'
Require (-not $imguiSource.Contains('m_windows.push_back')) 'ImGui manager still owns direct window registration.'
Require ($windowShellSource.Contains('entry.window->OnOpen()')) 'ImGui window shell does not dispatch open lifecycle events.'
Require ($windowShellSource.Contains('entry.window->OnClose()')) 'ImGui window shell does not dispatch close lifecycle events.'
Require ($editorContextHeaderSource.Contains('struct UIEditorContext')) 'Editor context has no explicit state Interface.'
Require ($editorContextHeaderSource.Contains('InspectorNavigationState inspectorNavigation')) 'Editor context does not own inspector navigation state.'
Require ($editorContextHeaderSource.Contains('UIRecordSelectionState selection')) 'Editor context does not own record selection state.'
Require ($editorContextStoreHeaderSource.Contains('class UIEditorContextStore')) 'Editor context store has no explicit Module Interface.'
foreach ($method in @('Slot', 'Node', 'Custom', 'ClearSelections', 'ResetTransientState')) {
    Require ($editorContextStoreHeaderSource.Contains($method)) "Editor context store Interface is missing $method."
}
Require ($editorContextSource.Contains('UIEditorContextStore::ClearSelections')) 'Editor context store does not centralize selection reset.'
Require ($editorContextSource.Contains('UIEditorContextStore::ResetTransientState')) 'Editor context store does not centralize transient-state reset.'
Require ($imguiHeaderSource.Contains('static WindowState& s_slotState')) 'ImGui manager no longer exposes the compatibility slot context reference.'
Require (-not $imguiHeaderSource.Contains('static WindowState s_slotState')) 'ImGui manager still owns the slot context value.'
Require ($detachedPreviewHeaderSource.Contains('class DetachedPreviewScene')) 'Detached preview scene Adapter is not represented explicitly.'
Require ($detachedPreviewSource.Contains('void DetachedPreviewScene::ProcessGameThread')) 'Detached preview scene has no game-thread lifecycle entry point.'
Require ($detachedPreviewSource.Contains('RE::Interface3D::Renderer::Create')) 'Detached preview scene does not create an IAD-owned Interface3D renderer.'
Require ($detachedPreviewSource.Contains('m_renderer->Offscreen_Set3D(nullptr);')) 'Detached preview scene does not clear its offscreen scene before teardown.'
Require ($detachedPreviewSource.Contains('m_renderer->MainScreen_SetScreenAttached3D(nullptr);')) 'Detached preview scene does not detach its display root before renderer release.'
Require ($detachedPreviewSource.Contains('m_renderer->Release();')) 'Detached preview scene does not release its owned renderer.'
if ($previewSceneSource.Contains('kDetachedPreviewEnabled = false')) {
    Require ($detachedPreviewSource.Contains('BuildFreshPlayerPreview')) 'Disabled detached preview branch has no isolated preview-tree builder.'
} else {
    Require ($detachedPreviewSource.Contains('ModelManager::GetSingleton()->CloneRenderOnly')) 'Detached preview scene does not build an owned render-only clone.'
}
Require ($detachedPreviewSource.Contains('m_ready.store(false, std::memory_order_release);')) 'Detached preview scene does not invalidate the render frame before retirement.'
Require ($detachedPreviewSource.Contains('void DetachedPreviewScene::SynchronizeClonePoseGameThread()')) 'Detached preview scene does not refresh the controller-free clone pose from the live actor.'
Require ($detachedPreviewSource.Contains('SynchronizeClonePoseGameThread();')) 'Detached preview frame update does not synchronize the clone pose before framing.'
Require ($detachedPreviewSource.Contains('CopyFlattenedPose(*m_previewRoot, m_sourceRoot);')) 'Detached preview pose synchronization does not use the flattened bone snapshot.'
Require ($previewSceneSource.Contains('GetDetached().ProcessGameThread')) 'Preview scene does not route detached lifecycle work through the scene owner.'
Require ($previewSceneSource.Contains('GetDetached().RetireGameThread')) 'Preview scene does not retire the detached Adapter when it is not requested.'
Require ($previewSceneSource.Contains('return UIWorldPreviewCamera::GetSingleton();')) 'Live-camera fallback is no longer the default preview Adapter.'
Require (-not $imguiSource.Contains('niCamera->worldToCam')) 'ImGui draw code still retains a live NiCamera projection pointer.'
Require ($displayContextHeaderSource.Contains('struct ActorDisplaySnapshot')) 'Actor display context has no UI snapshot contract.'
Require ($displayContextHeaderSource.Contains('using ActorDisplayIdentity = ActorRuntimeIdentity;')) 'Actor display context does not reuse the runtime scene identity contract.'
Require ($actorRuntimeHeaderSource.Contains('actor3DGeneration')) 'Actor runtime identity does not carry actor 3D generation.'
Require ($displayContextSource.Contains('void ActorDisplayContext::Publish')) 'Actor display context does not publish game-thread state.'
Require ($imguiSource.Contains('ActorDisplayContext::GetSingleton().Acquire')) 'ImGui does not consume the actor display snapshot context.'
Require ($imguiSource.Contains('previewEditor.InvalidateSceneSnapshot();')) 'ImGui does not invalidate stale preview state after a scene identity change.'
Require ($holsterSource.Contains('ActorDisplayContext::GetSingleton().Publish')) 'HolsterManager does not publish the actor display snapshot context.'
Require ($holsterSource.Contains('NodeManager::GetActor3DGeneration(player->GetFormID())')) 'HolsterManager does not publish the player 3D generation.'
Require ($nodeManagerSource.Contains('GetActor3DGeneration')) 'NodeManager does not expose actor 3D generation.'
Require ($nodeManagerSource.Contains('cache.actor3DGeneration = ++_actor3DGenerations[actorID]')) 'NodeManager does not advance actor 3D generation on root replacement.'
Require ($nodeManagerHeaderSource.Contains('InvalidateForConfigRefresh')) 'NodeManager has no configuration-refresh cache invalidation seam.'
Require ($nodeManagerSource.Contains('void NodeManager::InvalidateForConfigRefresh()')) 'NodeManager configuration-refresh cache invalidation is not implemented.'
Require ($nodeManagerSource.Contains('cache.activeNodes.clear();')) 'Configuration refresh does not rebuild cached CME/MOV bindings.'
Require (-not $conditionUiSource.Contains('NodeManager::ClearAllCaches()')) 'Configuration UI still uses full node-cache clearing and can reset the scene identity.'
Require (-not $settingsSource.Contains('NodeManager::ClearAllCaches()')) 'Settings UI still uses full node-cache clearing and can reset the scene identity.'
Require ($actorRuntimeHeaderSource.Contains('struct ActorRuntimeIdentity')) 'Actor runtime context has no scene identity contract.'
Require ($actorRuntimeHeaderSource.Contains('struct ActorRuntimeSnapshot')) 'Actor runtime context has no copied snapshot contract.'
Require ($actorRuntimeHeaderSource.Contains('std::vector<ActorRuntimeItemSnapshot> candidateItems')) 'Actor runtime context does not expose copied candidate identities.'
Require ($actorRuntimeHeaderSource.Contains('std::vector<ScopedData<SlotDefinition>> scopedSlots')) 'Actor runtime context does not expose copied slot configuration.'
Require ($actorRuntimeHeaderSource.Contains('bool AcquireFor(const ActorRuntimeIdentity&')) 'Actor runtime context has no scene-aware acquisition entry point.'
Require ($actorRuntimeHeaderSource.Contains('bool SameScene(const ActorRuntimeIdentity&')) 'Actor runtime context does not compare scene identities.'
Require ($actorRuntimeSource.Contains('void ActorRuntimeContext::Publish')) 'Actor runtime context does not publish a coherent snapshot.'
Require ($actorRuntimeSource.Contains('std::move(a_snapshot)')) 'Actor runtime context does not publish snapshots by value.'
Require ($actorRuntimeSource.Contains('m_snapshot.identity.SameScene(a_identity)')) 'Actor runtime context does not reject stale scene snapshots.'
Require ($holsterSource.Contains('ActorRuntimeContext::GetSingleton().Publish')) 'HolsterManager does not publish the runtime actor context snapshot.'
Require ($holsterSource.Contains('runtimeSnapshot.identity.actor3DGeneration')) 'Runtime actor context snapshot does not capture actor 3D generation.'
Require ($holsterSource.Contains('ActorRuntimeContext::GetSingleton().Invalidate')) 'Runtime actor context does not invalidate retired actor snapshots.'
Require ($assignmentResolverHeaderSource.Contains('class AssignmentResolver')) 'Assignment policy has no explicit resolver module.'
Require ($assignmentResolverSource.Contains('std::stable_sort')) 'Assignment resolver does not preserve deterministic priority ordering.'
Require ($assignmentResolverHeaderSource.Contains('struct SlotAssignmentDecision')) 'Assignment resolver has no data-only slot decision contract.'
Require ($assignmentResolverHeaderSource.Contains('struct SlotEligibilityContext')) 'Assignment resolver has no slot eligibility context contract.'
Require ($assignmentResolverHeaderSource.Contains('IsSlotCandidateEligible')) 'Assignment resolver has no shared slot eligibility entry point.'
Require ($assignmentResolverSource.Contains('ResolveSlotAssignment')) 'Assignment resolver has no slot decision implementation.'
Require ($assignmentResolverSource.Contains('PassesFormFilter')) 'Assignment resolver does not own form-filter eligibility.'
Require ($assignmentResolverSource.Contains('PassesLegacyFilters')) 'Assignment resolver does not own legacy filter eligibility.'
Require ($assignmentResolverHeaderSource.Contains('struct AssignmentResult')) 'Assignment resolver has no aggregate assignment result contract.'
Require ($assignmentResolverSource.Contains('AssignmentResult::TryAdd')) 'Assignment resolver has no aggregate assignment result implementation.'
Require ($assignmentResolverSource.Contains('AssignmentResult::CountForCustom')) 'Assignment result does not own Custom assignment counting.'
Require ($holsterSource.Contains('AssignmentResolver::ResolveSlotAssignment')) 'HolsterManager still owns the ordinary slot selection policy.'
Require ($holsterSource.Contains('AssignmentResolver::IsSlotCandidateEligible')) 'HolsterManager does not use the shared slot eligibility seam.'
Require ($holsterSource.Contains('AssignmentResolver::AssignmentResult finalAssignments')) 'HolsterManager does not use the aggregate resolver result.'
Require (-not $holsterSource.Contains('std::map<std::string, AssignmentData>')) 'HolsterManager still owns the retired local assignment result map.'
Require (-not $holsterSource.Contains('auto CheckSlotFilters')) 'HolsterManager still contains the retired inline slot filter module.'
Require ($holsterSource.Contains('AssignmentResolver::BuildSlotOrder(scopedSlots)')) 'HolsterManager still owns inline slot priority ordering.'
Require ($displayLifecycleHeaderSource.Contains('class ActorDisplayLifecycle')) 'Display clone lifecycle has no explicit module interface.'
Require ($displayLifecycleHeaderSource.Contains('BeginModelReplacement')) 'Display lifecycle interface has no deferred replacement boundary.'
Require ($displayLifecycleHeaderSource.Contains('RetireDeferredModels')) 'Display lifecycle interface has no deferred retirement boundary.'
Require ($displayLifecycleHeaderSource.Contains('BeginHolsterReplacement')) 'Holster replacement has no lifecycle interface.'
Require ($displayLifecycleHeaderSource.Contains('BeginModelGroupReplacement')) 'Model-group replacement has no lifecycle interface.'
Require ($displayLifecycleSource.Contains('ModelManager::IsGameLoading()')) 'Display lifecycle does not guard scene detach during load.'
Require ($displayLifecycleSource.Contains('a_slot.oldHolsters')) 'Holster retirement is not owned by the lifecycle implementation.'
Require ($displayLifecycleSource.Contains('a_slot.oldModelGroups')) 'Model-group retirement is not owned by the lifecycle implementation.'
Require ($holsterSource.Contains('ActorDisplayLifecycle::ClearSlotModels')) 'HolsterManager does not route slot teardown through the lifecycle module.'
Require ($holsterSource.Contains('ActorDisplayLifecycle::BeginModelReplacement')) 'HolsterManager does not route model replacement through the lifecycle module.'
Require ($holsterSource.Contains('ActorDisplayLifecycle::BeginHolsterReplacement(sState, _currentUpdateTick)')) 'HolsterManager still owns holster replacement bookkeeping.'
Require ($holsterSource.Contains('ActorDisplayLifecycle::BeginModelGroupReplacement(sState, _currentUpdateTick)')) 'HolsterManager still owns model-group replacement bookkeeping.'
Require (-not $holsterSource.Contains('for (auto& h : sState.currentHolsters) if (h) sState.oldHolsters.push_back(h);')) 'HolsterManager still moves holsters into the deferred queue itself.'
Require (-not $holsterSource.Contains('ActorDisplayLifecycle::AbandonModelArray(sState.currentModelGroups);')) 'HolsterManager still directly abandons current model groups during replacement.'
Require (-not $holsterSource.Contains('ActorDisplayLifecycle::AbandonModelArray(state.oldHolsters);')) 'Holster callbacks still retire old holsters outside the lifecycle module.'
Require (-not $holsterSource.Contains('ActorDisplayLifecycle::AbandonModelArray(state.oldModelGroups);')) 'Model-group callbacks still retire old groups outside the lifecycle module.'
Require (-not $holsterSource.Contains('Safe_Abandon_Slot')) 'HolsterManager still owns the retired display clone release helper.'
Require ($refreshSchedulerHeaderSource.Contains('class ActorRefreshScheduler')) 'Refresh scheduling has no explicit module interface.'
Require ($refreshSchedulerHeaderSource.Contains('struct ActorRefreshObservation')) 'Refresh scheduler has no actor observation contract.'
Require ($refreshSchedulerHeaderSource.Contains('TryQueueGlobalRefreshTask')) 'Refresh scheduler has no global refresh coalescing contract.'
Require ($refreshSchedulerSource.Contains('state.pendingFlags |= kEvaluateFlag')) 'Refresh scheduler does not coalesce evaluation requests.'
Require ($refreshSchedulerSource.Contains('state.evaluationQueued = true')) 'Refresh scheduler does not claim evaluation work.'
Require ($refreshSchedulerSource.Contains('CollectStaleActors')) 'Refresh scheduler does not own stale actor retirement selection.'
Require ($holsterSource.Contains('_refreshScheduler.ObserveActor')) 'HolsterManager does not consume the refresh scheduler observation seam.'
Require ($holsterSource.Contains('_refreshScheduler.ConsumeGlobalRefreshRequest')) 'HolsterManager does not consume global refresh requests through the scheduler.'
Require (-not $holsterSource.Contains('_actorRefreshStates')) 'HolsterManager still owns the retired actor refresh-state map.'
Require (-not $holsterSource.Contains('_flagsMutex')) 'HolsterManager still owns the retired refresh-state mutex.'
Require ($renderCapabilitiesHeaderSource.Contains('struct PreviewRenderCapabilitySnapshot')) 'Preview render capability snapshot is not represented by an explicit data type.'
Require ($renderCapabilitiesSource.Contains('RE::BSGraphics::GetRendererData()')) 'Preview render capability probe does not inspect Fallout 4 renderer data.'
Require ($renderCapabilitiesSource.Contains('CreateTexture2D')) 'Preview render capability probe does not test independent texture creation.'
Require ($renderCapabilitiesSource.Contains('CreateRenderTargetView')) 'Preview render capability probe does not test independent render-target view creation.'
Require ($renderCapabilitiesSource.Contains('CreateShaderResourceView')) 'Preview render capability probe does not test independent shader-resource view creation.'
Require (-not $renderCapabilitiesSource.Contains('RenderTargetManager::GetSingleton')) 'Preview render capability probe must not call unverified raw render-target manager addresses.'
Require ($imguiSource.Contains('PreviewRenderCapabilities::GetSingleton().Refresh')) 'ImGui render thread does not refresh preview render capabilities.'
foreach ($source in @($imguiSource, $conditionUiSource, $settingsSource)) {
    Require (-not $source.Contains('config->SaveConfig();')) 'A UI module still writes ActiveConfig directly instead of using UICommitManager.'
    Require (-not $source.Contains('config->SaveINISettings();')) 'A UI module still writes INI settings directly instead of using UICommitManager.'
}
Require ($nodeManagerSource.Contains('bool a_absolute)')) 'Transform helper still carries an unsupported adjustment contract.'
Require (-not $nodeManagerSource.Contains('bool /*a_weightAdjust*/')) 'Transform weight-adjust parameter remains an explicit no-op.'
Require (-not $nodeManagerSource.Contains('bool /*a_weaponAdjust*/')) 'Transform weapon-adjust parameter remains an explicit no-op.'
Require (-not $holsterSource.Contains('(void)a_animation.attachSubGraphs')) 'Animation SubGraphs option remains an explicit no-op.'
Require ($holsterSource.Contains('BSAutoReadLock inventoryLock')) 'Inventory signature is not protected by the inventory read lock.'
Require ($holsterHeaderSource.Contains('hideWeaponWhenDrawn')) 'Holster slot state does not retain the effective draw-hide policy.'
Require ($holsterHeaderSource.Contains('modelRequestGeneration') -and $holsterHeaderSource.Contains('holsterRequestGeneration') -and $holsterHeaderSource.Contains('modelGroupRequestGeneration')) 'Holster slot state does not isolate async request identity by resource type.'
Require ($holsterHeaderSource.Contains('bool worldPreviewEdit = true;')) 'World preview editing is not enabled by default.'
Require ($holsterSource.Contains('ShouldHideWeaponDisplay')) 'Async and per-frame weapon visibility do not share a live weapon-state gate.'
Require ($holsterSource.Contains('ShouldHideHolsterDisplay')) 'Async holster visibility does not share the live weapon-state gate.'
Require ($holsterSource.Contains('reqGeneration = sState.modelRequestGeneration') -and $holsterSource.Contains('reqGeneration = sState.holsterRequestGeneration') -and $holsterSource.Contains('reqGeneration = sState.modelGroupRequestGeneration')) 'Async model callbacks do not capture their resource-specific request identities.'
Require ($holsterSource.Contains('state.modelRequestGeneration != reqGeneration') -and $holsterSource.Contains('state.holsterRequestGeneration != reqGeneration') -and $holsterSource.Contains('state.modelGroupRequestGeneration != reqGeneration')) 'Async model callbacks do not reject stale resource-specific requests.'
Require ($holsterSource.Contains('safeLoaded->SetAppCulled(true)')) 'Async model callbacks do not defer visibility to the per-frame transform pass.'
Require ($holsterSource.Contains('ShouldHideWeaponDisplay(a_actor, sState)')) 'Per-frame transforms do not recheck the current weapon state.'
Require (-not $holsterSource.Contains('safeLoaded->SetAppCulled(!shouldShowWp)')) 'Main model callbacks still publish visibility from a stale async state sample.'
Require ($holsterSource.Contains('a_actor->weaponState != RE::WEAPON_STATE::kSheathed')) 'Weapon visibility does not use the live weapon state directly.'
Require ($holsterSource.Contains('ClearActorSlots(formID, false)')) 'Death cleanup still abandons actor display nodes without detaching them from the live scene.'
Require ($holsterSource.Contains('ClearActorHistory(formID)')) 'Death cleanup does not discard actor-specific display history before a reload.'
Require ($holsterSource.Contains('IsDead(false) || actor->IsDeleted() || actor->IsDisabled()')) 'Deferred death cleanup does not revalidate the actor before clearing restored scene state.'
Require ($modelManagerSource.Contains('ClearAllActorSlots(false)')) 'In-game load refresh does not detach stale display nodes from the restored scene.'
Require ($modelManagerSource.Contains('ClearAllActorSlots(true)')) 'MainMenu scene reset no longer preserves the no-detach teardown path.'
Require ($inspectorNavigationHeaderSource.Contains('struct InspectorNavigationState')) 'Inspector navigation state has no explicit window-shell contract.'
Require ($inspectorNavigationHeaderSource.Contains('class UIInspectorNavigation')) 'Inspector navigation has no explicit module interface.'
Require ($inspectorNavigationSource.Contains('std::clamp')) 'Inspector navigation does not clamp persisted indices before activation.'
Require ($conditionUiSource.Contains('UIInspectorNavigation::InitializeIndex')) 'Inspector editors do not initialize persisted navigation through the navigation module.'
Require ($conditionUiSource.Contains('UIInspectorNavigation::Consume')) 'Inspector editors do not consume one-shot navigation requests through the navigation module.'
Require (-not $imguiHeaderSource.Contains('inspectorTabRequest')) 'Window state still exposes the retired direct inspector tab request field.'
Require (-not $imguiHeaderSource.Contains('inspectorTabInitialized')) 'Window state still exposes the retired direct inspector tab initialization field.'
Require ($recordSelectionHeaderSource.Contains('class UIRecordSelectionState')) 'Record selection has no explicit window-shell module interface.'
Require ($recordSelectionSource.Contains('StripManagedPrefix')) 'Record selection has no centralized managed-name normalization.'
Require ($recordSelectionSource.Contains('ClearIfSelected')) 'Record selection has no identity-aware clear operation.'
Require ($editorContextHeaderSource.Contains('UIRecordSelectionState selection;')) 'Editor context does not own record selection.'
Require (-not $imguiHeaderSource.Contains('s_selectedSlot')) 'ImGui manager still exposes the retired slot selection string.'
Require (-not $imguiHeaderSource.Contains('s_selectedNode')) 'ImGui manager still exposes the retired node selection string.'
Require (-not $imguiHeaderSource.Contains('s_selectedCustom')) 'ImGui manager still exposes the retired Custom selection string.'
Require ($conditionUiSource.Contains('selection.IsSelected')) 'Config editor list selection does not use the record selection module.'
Require ($editorInteractionHeaderSource.Contains('class UIEditorInteraction')) 'Editor interaction has no explicit shared module interface.'
Require ($editorInteractionSource.Contains('UIEditorInteraction::DrawPaneSplitter')) 'Pane splitter behavior is not owned by the editor interaction module.'
Require ($editorInteractionSource.Contains('UIEditorInteraction::TrackLiveConfigEdits')) 'Live edit commit tracking is not owned by the editor interaction module.'
Require (-not $conditionUiSource.Contains('static bool DrawPaneSplitter')) 'Configuration UI still contains a local pane splitter implementation.'
Require (-not $conditionUiSource.Contains('static void ClampPaneWidth')) 'Configuration UI still contains a local pane-width implementation.'
Require (-not $conditionUiSource.Contains('static void TrackLiveConfigEdits')) 'Configuration UI still contains a local live-edit tracking implementation.'
Require ($profileEditorStateHeaderSource.Contains('struct UIProfileEditorState')) 'Profile editor state has no explicit shared state contract.'
Require ($profileEditorStateHeaderSource.Contains('class UIProfileEditorStateStore')) 'Profile editor state has no explicit store interface.'
Require ($profileEditorStateSource.Contains('static std::map<std::string, UIProfileEditorState>')) 'Profile editor state storage is not centralized.'
Require ($conditionUiSource.Contains('UIProfileEditorStateStore::Get(id)')) 'Profile editors do not consume the shared profile editor state module.'
Require ($conditionTreeSignatureHeaderSource.Contains('class UIConditionTreeSignature')) 'Condition tree signature Module is missing its Interface.'
Require ($conditionTreeSignatureSource.Contains('std::string UIConditionTreeSignature::Build')) 'Condition tree signature Module is missing its Implementation.'
Require (-not $conditionUiSource.Contains('static std::string BuildConditionTreeSignature')) 'Configuration UI still contains a local condition tree signature implementation.'
Require ($conditionUiSource.Contains('UIConditionTreeSignature::Build')) 'Configuration UI does not consume the shared condition tree signature Module.'
Require ($conditionCatalogHeaderSource.Contains('class UIConditionCatalog')) 'Condition editor catalog has no explicit Module Interface.'
Require ($conditionCatalogSource.Contains('UIConditionCatalog::Types()')) 'Condition editor catalog is missing its Implementation.'
Require (-not $conditionUiSource.Contains('const char* condTypes[]')) 'Configuration UI still owns the condition type catalog.'
Require ($conditionTreeEditorSource.Contains('UIConditionCatalog::Types()')) 'Condition tree editor does not consume the shared condition catalog Module.'
Require ($conditionTreeEditorHeaderSource.Contains('class UIConditionTreeEditor')) 'Condition tree editor has no explicit Module Interface.'
Require ($conditionTreeEditorSource.Contains('UIConditionTreeEditor::Draw')) 'Condition tree editor is missing its primary Implementation.'
Require ($conditionTreeEditorSource.Contains('DrawConditionNodeRecursive')) 'Condition tree editor does not retain recursive tree editing.'
Require ($conditionTreeEditorSource.Contains('DrawKeywordScannerBoxInternal')) 'Condition tree editor does not own keyword scanning.'
Require (-not $conditionUiSource.Contains('static bool DrawConditionNodeRecursive')) 'Configuration UI still owns recursive condition tree rendering.'
Require (-not $conditionUiSource.Contains('static void DrawKeywordScannerBox')) 'Configuration UI still owns condition keyword scanning.'
Require ($conditionUiSource.Contains('UIConditionTreeEditor::Draw(rootNode')) 'Condition editor entry point does not consume the tree editor Module.'
Require ($formEditorControlsHeaderSource.Contains('class UIFormEditorControls')) 'Shared Fallout 4 form editor controls have no explicit Module Interface.'
foreach ($method in @('DrawFormSetUI', 'DrawFormVectorUI', 'DrawFormIDField', 'DrawStringVectorEditor', 'DrawFormTypeVectorEditor', 'DrawBipedSlotVectorEditor')) {
    Require ($formEditorControlsHeaderSource.Contains('static ' + $(if ($method -eq 'DrawFormSetUI') { 'void' } else { 'bool' }) + ' ' + $method)) "Shared form editor Interface is missing $method."
    Require ($formEditorControlsSource.Contains('UIFormEditorControls::' + $method)) "Shared form editor Implementation is missing $method."
}
Require ($formEditorControlsSource.Contains('NormalizeUIBipedSlot')) 'Shared Biped slot editor does not retain FO4 slot normalization.'
Require (-not $conditionUiSource.Contains('static void DrawFormSetUI')) 'Configuration UI still owns FormID set editing.'
Require (-not $conditionUiSource.Contains('static bool DrawFormVectorUI')) 'Configuration UI still owns FormID vector editing.'
Require (-not $conditionUiSource.Contains('static bool DrawFormIDField')) 'Configuration UI still owns FormID field editing.'
Require (-not $conditionUiSource.Contains('static bool DrawStringVectorEditor')) 'Configuration UI still owns string vector editing.'
Require (-not $conditionUiSource.Contains('static bool DrawFormTypeVectorEditor')) 'Configuration UI still owns form type vector editing.'
Require (-not $conditionUiSource.Contains('static bool DrawBipedSlotVectorEditor')) 'Configuration UI still owns Biped slot vector editing.'
Require ($conditionUiSource.Contains('UIFormEditorControls::DrawFormIDField')) 'Configuration UI does not consume the shared FormID editor Module.'
Require ($conditionUiSource.Contains('UIFormEditorControls::DrawFormVectorUI')) 'Configuration UI does not consume the shared FormID vector Module.'
Require ($conditionUiSource.Contains('UIFormEditorControls::DrawFormSetUI')) 'Configuration UI does not consume the shared FormID set Module.'
Require ($transformEditorControlsHeaderSource.Contains('class UITransformEditorControls')) 'Shared transform and color controls have no explicit Module Interface.'
Require ($transformEditorControlsSource.Contains('UITransformEditorControls::DrawTransformWidget')) 'Shared transform editor is missing its Implementation.'
Require ($transformEditorControlsSource.Contains('UITransformEditorControls::DrawColorRGBA')) 'Shared color editor is missing its Implementation.'
Require ($conditionUiSource.Contains('UITransformEditorControls::DrawTransformWidget')) 'Configuration UI does not consume the shared transform editor Module.'
Require ($conditionUiSource.Contains('UITransformEditorControls::DrawColorRGBA')) 'Configuration UI does not consume the shared color editor Module.'
Require (-not $conditionUiSource.Contains('static bool DrawIADTransformWidget')) 'Configuration UI still owns the shared transform editor.'
Require (-not $conditionUiSource.Contains('static bool DrawColorRGBA')) 'Configuration UI still owns the shared color editor.'
Require ($modelEditorControlsHeaderSource.Contains('class UIModelEditorControls')) 'Shared model editor controls have no explicit Module Interface.'
foreach ($method in @('DrawModelCleanupSettings', 'DrawModelAnimationSettings', 'DrawModelEffectShaderSettings', 'DrawModelLightSettings', 'DrawModelSwapVariableSource')) {
    Require ($modelEditorControlsSource.Contains('UIModelEditorControls::' + $method)) "Shared model editor Implementation is missing $method."
    Require ($conditionUiSource.Contains('UIModelEditorControls::' + $method)) "Configuration UI does not consume shared model editor control $method."
}
Require (-not $conditionUiSource.Contains('static bool DrawModelCleanupSettings')) 'Configuration UI still owns model cleanup settings.'
Require (-not $conditionUiSource.Contains('static bool DrawModelAnimationSettings')) 'Configuration UI still owns model animation settings.'
Require (-not $conditionUiSource.Contains('static bool DrawModelEffectShaderSettings')) 'Configuration UI still owns model effect shader settings.'
Require (-not $conditionUiSource.Contains('static bool DrawModelLightSettings')) 'Configuration UI still owns model light settings.'
Require (-not $conditionUiSource.Contains('static bool DrawModelSwapVariableSource')) 'Configuration UI still owns model variable source settings.'
Require ($managedProfileControlsHeaderSource.Contains('class UIManagedProfileControls')) 'Shared managed profile controls have no explicit Module Interface.'
Require ($managedProfileControlsHeaderSource.Contains('static bool DrawSelector')) 'Shared managed profile controls are missing their selector Interface.'
Require ($managedProfileControlsHeaderSource.Contains('ProfileManager<T>')) 'Shared managed profile controls do not accept the profile manager contract.'
Require ($conditionUiSource.Contains('UIManagedProfileControls::DrawSelector')) 'Configuration UI does not consume the shared managed profile controls Module.'
Require ($settingsSource.Contains('UIManagedProfileControls::DrawSelector')) 'Settings UI does not consume the shared managed profile controls Module.'
Require (-not $conditionUiSource.Contains('static bool DrawManagedProfileSelector')) 'Configuration UI still owns the managed profile selector implementation.'
Require ($profileWorkflowHeaderSource.Contains('class UIProfileWorkflow')) 'Profile workflow has no explicit Module Interface.'
foreach ($method in @('SaveFormFilter', 'ReloadFormFilter', 'RenameFormFilter', 'DeleteFormFilter')) {
    Require ($profileWorkflowHeaderSource.Contains('static bool ' + $method)) "Profile workflow Interface is missing $method."
    Require ($profileWorkflowSource.Contains('UIProfileWorkflow::' + $method)) "Profile workflow Implementation is missing $method."
}
Require ($conditionUiSource.Contains('UIProfileWorkflow::RenameFormFilter')) 'Managed FormFilter rename does not use the shared profile workflow.'
Require ($conditionUiSource.Contains('UIProfileWorkflow::DeleteFormFilter')) 'Managed FormFilter deletion does not use the shared profile workflow.'
Require ($conditionUiSource.Contains('UIProfileWorkflow::SaveFormFilter(managedProfiles')) 'Legacy FormFilter editor save does not use the shared profile workflow.'
Require ($conditionUiSource.Contains('UIProfileWorkflow::ReloadFormFilter(managedProfiles')) 'Legacy FormFilter editor reload does not use the shared profile workflow.'
Require (-not $holsterSource.Contains('modelGroupAttachSubGraphs')) 'Runtime model-group state still carries unsupported SubGraphs data.'
Require (-not $holsterSource.Contains('group.disableHavok')) 'Runtime model-group signatures still include unsupported Havok policy data.'
foreach ($name in @('prioritizeEquippedCandidates', 'useRecentDisplaySlotMemory', 'reserveEquippedForPositivePrioritySlots')) {
    Require ($loadConfigInitialization.Contains($name + ' = true;')) "LoadConfig does not reset the ActiveConfig-owned $name default."
}

function Convert-CppStringLiteral([string]$Value) {
    return $Value.Replace('\r', [string][char]13).Replace('\n', [string][char]10).Replace('\t', [string][char]9).Replace('\0', [string][char]0).Replace('\"', '"').Replace('\\', '\')
}

$localizationDirectory = Join-Path $ProjectRoot 'data\F4SE\Plugins\ImmersiveArsenalDisplays\Localization'
$localeDocuments = @{}
foreach ($languageID in @('en_US', 'zh_CN')) {
    $localePath = Join-Path $localizationDirectory ($languageID + '.json')
    Require (Test-Path -LiteralPath $localePath -PathType Leaf) "Localization file is missing: $languageID.json."
    try {
        $localeDocuments[$languageID] = Get-Content $localePath -Raw -Encoding UTF8 | ConvertFrom-Json
    }
    catch {
        throw "Localization file is invalid JSON: $languageID.json. $($_.Exception.Message)"
    }
    Require ($null -ne $localeDocuments[$languageID].strings -and $localeDocuments[$languageID].strings -is [pscustomobject]) "Localization strings object is missing: $languageID.json."
}

$uiLiteralKeys = @()
$uiNamedKeys = @()
$rawHanUiLiterals = @()
foreach ($uiFile in Get-ChildItem (Join-Path $ProjectRoot 'src\UI') -Recurse -File | Where-Object { $_.Extension -in @('.cpp', '.h') }) {
    $uiText = Get-Content $uiFile.FullName -Raw -Encoding UTF8
    $uiLiteralKeys += [regex]::Matches($uiText, 'TextLiteral\("((?:[^"\\]|\\.)*)"\)') | ForEach-Object {
        'literal.' + (Convert-CppStringLiteral $_.Groups[1].Value)
    }
    $uiNamedKeys += [regex]::Matches($uiText, '(?<![A-Za-z0-9_:.])Text\("((?:[^"\\]|\\.)*)"\)') | ForEach-Object {
        $_.Groups[1].Value
    }
    $rawHanUiLiterals += [regex]::Matches($uiText, '(?<!TextLiteral\()"[^"\r\n]*[\u4e00-\u9fff][^"\r\n]*"') | ForEach-Object {
        $uiFile.FullName + ': ' + $_.Value
    }
}
$uiLiteralKeys = @($uiLiteralKeys | Sort-Object -Unique)
$uiNamedKeys = @($uiNamedKeys | Sort-Object -Unique)
Require ($rawHanUiLiterals.Count -eq 0) ('Unlocalized Chinese UI string literals: ' + ($rawHanUiLiterals -join '; '))

foreach ($languageID in @('en_US', 'zh_CN')) {
    $strings = $localeDocuments[$languageID].strings
    $stringNames = @($strings.psobject.Properties.Name)
    $missingLiteralKeys = @($uiLiteralKeys | Where-Object { $_ -notin $stringNames })
    $missingNamedKeys = @($uiNamedKeys | Where-Object { $_ -notin $stringNames })
    $hanValues = @($strings.psobject.Properties | Where-Object { $_.Value -is [string] -and $_.Value -match '[\u4e00-\u9fff]' })
    Require ($missingLiteralKeys.Count -eq 0) "$languageID.json is missing UI literal keys: $($missingLiteralKeys -join ', ')"
    Require ($missingNamedKeys.Count -eq 0) "$languageID.json is missing UI named keys: $($missingNamedKeys -join ', ')"
    Require ($languageID -ne 'en_US' -or $hanValues.Count -eq 0) ('en_US.json contains Chinese translation values: ' + (($hanValues | ForEach-Object { $_.Name }) -join ', '))
}

Write-Output "[PASS] Static invariants: $($uiTypes.Count) editor condition types, $($legacyTypes.Count) legacy compatibility types, shared form/transform/model/profile editors, and RuntimeSelection settings verified."
