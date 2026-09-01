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
$configSource = Get-Content (Join-Path $ProjectRoot 'src\Data\ConfigManager.cpp') -Raw
$settingsSource = Get-Content (Join-Path $ProjectRoot 'src\UI\UISettingsWindow.cpp') -Raw
$holsterHeaderSource = Get-Content (Join-Path $ProjectRoot 'src\System\HolsterManager.h') -Raw
$holsterSource = Get-Content (Join-Path $ProjectRoot 'src\System\HolsterManager.cpp') -Raw
$modelManagerSource = Get-Content (Join-Path $ProjectRoot 'src\ModelManager.cpp') -Raw
$nodeManagerSource = Get-Content (Join-Path $ProjectRoot 'src\Engine\NodeManager.cpp') -Raw
$mainSource = Get-Content (Join-Path $ProjectRoot 'src\main.cpp') -Raw
$imguiSource = Get-Content (Join-Path $ProjectRoot 'src\UI\ImGuiManager.cpp') -Raw
$loadConfigMatch = [regex]::Match(
    $configSource,
    'void ConfigManager::LoadConfig\(\)\s*\{(?<body>.*?)std::string activeConfigPath',
    [Text.RegularExpressions.RegexOptions]::Singleline)
Require $loadConfigMatch.Success 'ConfigManager::LoadConfig initialization block was not found.'
$loadConfigInitialization = $loadConfigMatch.Groups['body'].Value

$uiMatch = [regex]::Match(
    $conditionUiSource,
    'const char\* condTypes\[\] = \{(?<items>.*?)\};',
    [Text.RegularExpressions.RegexOptions]::Singleline)
Require $uiMatch.Success 'Condition editor list was not found.'

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

Require $settingsSource.Contains('enableModelLights') 'Model light global setting is not exposed in the settings UI.'
Require $holsterSource.Contains('if (!runtimeSettings.enableModelLights) effectiveLight.enabled = false;') 'Main display light global gate is missing.'
Require $holsterSource.Contains('if (!runtimeSettings.enableModelEffects) {') 'Model group effect global gate is missing.'
Require $holsterSource.Contains('groupCleanupPolicy.removeLights = !activeModelGroups[i].light.enabled;') 'Model group light cleanup does not follow the effective light state.'
Require $holsterSource.Contains('npcEvaluationIntervalTicks = std::clamp') 'NPC evaluation interval is not clamped in the update loop.'
Require $holsterSource.Contains('const bool processNPCDisplays = runtimeSettings.enableNPCDisplays;') 'NPC display processing gate is missing.'
Require $holsterSource.Contains('if (actor != player && !processNPCDisplays) continue;') 'Disabled NPC displays still enter the actor update path.'
Require $holsterSource.Contains('config->IsActorDisplayBlocked(a_actor)') 'Actor display block list is not enforced by the display runtime.'
Require $holsterSource.Contains('const bool suppressDisplays =') 'Actor display block list is not enforced by the transform pass.'
Require $settingsSource.Contains('屏蔽玩家展示 (IED Player Toggle)') 'Player display block toggle is not exposed in the settings UI.'
Require $settingsSource.Contains('玩家展示切换键') 'Player display block hotkey is not exposed in the settings UI.'
Require $configSource.Contains('"PlayerBlockToggleKey"') 'Player display block hotkey is not persisted to INI.'
Require ((Get-Content (Join-Path $ProjectRoot 'src\UI\ImGuiManager.cpp') -Raw).Contains('playerBlockHotkey != 0')) 'Player display block hotkey is not handled by the window input hook.'
Require ((Get-Content (Join-Path $ProjectRoot 'src\UI\ImGuiManager.cpp') -Raw).Contains('SetPlayerDisplaysBlocked(!runtimeConfig->IsPlayerDisplaysBlocked())')) 'Player display block hotkey does not toggle state at task execution time.'
Require $holsterSource.Contains('cull(state.oldModels);') 'Disabled NPC displays do not cull deferred main models.'
Require $holsterSource.Contains('cull(state.oldHolsters);') 'Disabled NPC displays do not cull deferred holsters.'
Require $holsterSource.Contains('cull(state.oldModelGroups);') 'Disabled NPC displays do not cull deferred model groups.'
Require $holsterSource.Contains('if (nodeStatesIt == _actorNodeStates.end() && slotStatesIt == _actorDisplaySlots.end()) return;') 'Transform pass creates empty actor state for actors without IAD data.'
Require $holsterSource.Contains('std::unordered_set<RE::TESFormID> activeActorIDs;') 'Active actor list is not deduplicated.'
Require $holsterSource.Contains('if (activeActorIDs.emplace(a_actor->GetFormID()).second)') 'Active actor deduplication does not use FormID identity.'
Require $modelManagerSource.Contains('auto req = _asyncLoadQueue.front();') 'Async model queue no longer uses FIFO request processing.'
Require $modelManagerSource.Contains('_asyncLoadQueue.pop();') 'Async model queue no longer consumes exactly one queued request per update.'
Require $modelManagerSource.Contains('if (req.sceneGeneration != GetSceneGeneration())') 'Async model queue no longer rejects stale scene-generation requests.'
Require $modelManagerSource.Contains('g_sceneGeneration.fetch_add(1, std::memory_order_acq_rel);') 'Pre-load no longer invalidates asynchronous model requests.'
Require $nodeManagerSource.Contains('if (stateChanged || cache.root3D != root)') 'Actor 3D-root replacement is no longer detected.'
Require $nodeManagerSource.Contains('HolsterManager::GetSingleton()->ClearActorSlots(actorID, true);') '3D-root replacement no longer clears actor display state without a scene detach.'
Require $nodeManagerSource.Contains('HolsterManager::GetSingleton()->RequestEvaluate(actorID);') '3D-root replacement does not queue actor display reconstruction.'
Require $configSource.Contains('j.value("UseProfile", false)') 'Form filter profile references are no longer deserialized.'
Require $configSource.Contains('j["ProfileName"] = f.profileName;') 'Form filter profile references are no longer serialized.'
Require $holsterSource.Contains('ResolveSlotFormFilter') 'Slot evaluation no longer resolves form filter profile references.'
Require $holsterSource.Contains('profileManager.FormFilters().Find(slotDef.itemFilter.profileName)') 'Slot evaluation does not resolve the configured named form filter profile.'
Require $mainSource.Contains('GlobalProfileManager::GetSingleton().LoadAll();') 'Profiles are not loaded before runtime selection begins.'
Require $configSource.Contains('GetActiveEffectConditionFormIDsSnapshot') 'Active-effect condition form IDs are not collected from the active configuration.'
Require $holsterSource.Contains('GetActiveEffectSignature(RE::Actor* a_actor, const std::vector<std::uint32_t>& a_watchedFormIDs)') 'Active-effect signature is no longer scoped to watched form IDs.'
Require $holsterSource.Contains('std::binary_search(a_watchedFormIDs.begin(), a_watchedFormIDs.end(), formID)') 'Active-effect signature does not ignore unrelated effects.'
Require $configSource.Contains('GetKeyBindDefinitionsSnapshot') 'Named keybind definitions are not exposed to the runtime.'
Require $configSource.Contains('jMaster["RuntimeSelection"]["Keybinds"]') 'Named keybind definitions are not persisted in ActiveConfig.'
Require $holsterSource.Contains('KeyBindStateManager::GetSingleton()->Update(keyBindDefinitions)') 'Named keybind states are not updated by the runtime.'
Require $conditionSource.Contains('KeyBindStateManager::GetSingleton()->GetState(a_key, state)') 'KeyBindState conditions do not read named multi-state bindings.'
Require $mainSource.Contains("serialization->SetUniqueID('IAD3')") 'Named keybind state serialization is not registered.'
Require ((Get-Content (Join-Path $ProjectRoot 'src\UI\ImGuiManager.cpp') -Raw).Contains('KeyBindStateManager::GetSingleton()->ProcessKeyEvent')) 'Named keybinds are not fed from physical window key events.'
Require $conditionUiSource.Contains("out += filter.useProfile ? '1' : '0';") 'Form filter profile-reference mode is omitted from the editor dirty signature.'
Require $conditionUiSource.Contains('AppendSignatureString(out, filter.profileName);') 'Form filter profile name is omitted from the editor dirty signature.'
Require $conditionUiSource.Contains('managedProfiles.ReloadProfile(m_selectedProf);') 'Legacy form filter editor does not synchronize runtime profile data after save or reload.'
Require $configSource.Contains('bool ConfigManager::RenameFormFilterReferences') 'Form filter profile rename does not repair slot references.'
Require $configSource.Contains('bool ConfigManager::ClearFormFilterReferences') 'Form filter profile deletion does not clear slot references.'
Require $conditionUiSource.Contains('config->RenameFormFilterReferences(oldName, nameBuffer)') 'Form filter profile rename is not wired to slot-reference maintenance.'
Require $conditionUiSource.Contains('config->ClearFormFilterReferences(deletedName)') 'Form filter profile deletion is not wired to slot-reference maintenance.'
Require $holsterSource.Contains('a_event.itemCount == 0') 'Container changes still discard removal events before refreshing the source actor.'
Require $holsterSource.Contains('a_event.oldContainerFormID') 'Container changes do not inspect the source container actor.'
Require $holsterSource.Contains('s_lastInventorySignature') 'Player inventory polling does not detect same-stack count changes.'
Require $configSource.Contains('resetActiveConfigState') 'Config candidates do not have an isolated reset boundary.'
Require $configSource.Contains('MoveFileExA') 'ActiveConfig is not replaced through an atomic Windows file move.'
Require $configSource.Contains('TryParsePoint3') 'Vector config parsing does not validate array shape before indexing.'
Require $configSource.Contains('value.at(0).get<float>()') 'Vector config parsing does not use checked JSON array access.'
Require $configSource.Contains('GetRuntimeSettingsSnapshot') 'Runtime configuration reads do not use a synchronized snapshot.'
Require $holsterSource.Contains('runtimeSettings.enableNPCDisplays') 'Update loop still reads mutable runtime settings directly.'
Require $imguiSource.Contains('InstallCursorHooks') 'ImGui manager does not install cursor API hooks.'
Require $imguiSource.Contains('ClipCursor_Hook') 'ImGui manager does not intercept game cursor clipping.'
Require $imguiSource.Contains('SetCursorPos_Hook') 'ImGui manager does not suppress game cursor recentering.'
Require $imguiSource.Contains('GetWindowRect') 'ImGui manager does not refresh the full game-window cursor bounds.'
Require $nodeManagerSource.Contains('bool a_absolute)') 'Transform helper still carries an unsupported adjustment contract.'
Require (-not $nodeManagerSource.Contains('bool /*a_weightAdjust*/')) 'Transform weight-adjust parameter remains an explicit no-op.'
Require (-not $nodeManagerSource.Contains('bool /*a_weaponAdjust*/')) 'Transform weapon-adjust parameter remains an explicit no-op.'
Require (-not $holsterSource.Contains('(void)a_animation.attachSubGraphs')) 'Animation SubGraphs option remains an explicit no-op.'
Require $holsterSource.Contains('BSAutoReadLock inventoryLock') 'Inventory signature is not protected by the inventory read lock.'
Require $holsterHeaderSource.Contains('hideWeaponWhenDrawn') 'Holster slot state does not retain the effective draw-hide policy.'
Require $holsterSource.Contains('ShouldHideWeaponDisplay') 'Async and per-frame weapon visibility do not share a live weapon-state gate.'
Require $holsterSource.Contains('ShouldHideHolsterDisplay') 'Async holster visibility does not share the live weapon-state gate.'
Require $holsterSource.Contains('ShouldHideWeaponDisplay(act, state)') 'Async model callbacks can attach a weapon without rechecking the current weapon state.'
Require $holsterSource.Contains('ShouldHideWeaponDisplay(a_actor, sState)') 'Per-frame transforms do not recheck the current weapon state.'
Require $holsterSource.Contains('g_playerEquipTransitionUntil') 'Player weapon-switch transition state is not retained across transient sheathed states.'
Require $holsterSource.Contains('ArmPlayerEquipTransition') 'Player equip changes do not arm the transient weapon-switch visibility guard.'
Require $holsterSource.Contains('IsPlayerEquipTransitionPending') 'Visibility helpers do not account for transient weapon-switch state.'
Require $holsterSource.Contains('g_playerEquipTransitionUntil.store(0') 'Weapon-switch transition lock is not cleared when the weapon draw/sheath state starts.'
Require $holsterSource.Contains('ClearActorSlots(formID, false)') 'Death cleanup still abandons actor display nodes without detaching them from the live scene.'
Require $holsterSource.Contains('ClearActorHistory(formID)') 'Death cleanup does not discard actor-specific display history before a reload.'
Require $holsterSource.Contains('IsDead(false) || actor->IsDeleted() || actor->IsDisabled()') 'Deferred death cleanup does not revalidate the actor before clearing restored scene state.'
Require $modelManagerSource.Contains('ClearAllActorSlots(false)') 'In-game load refresh does not detach stale display nodes from the restored scene.'
Require $modelManagerSource.Contains('ClearAllActorSlots(true)') 'MainMenu scene reset no longer preserves the no-detach teardown path.'
Require (-not $holsterSource.Contains('modelGroupAttachSubGraphs')) 'Runtime model-group state still carries unsupported SubGraphs data.'
Require (-not $holsterSource.Contains('group.disableHavok')) 'Runtime model-group signatures still include unsupported Havok policy data.'
foreach ($name in @('prioritizeEquippedCandidates', 'useRecentDisplaySlotMemory', 'reserveEquippedForPositivePrioritySlots')) {
    Require $loadConfigInitialization.Contains($name + ' = true;') "LoadConfig does not reset the ActiveConfig-owned $name default."
}

Write-Output "[PASS] Static invariants: $($uiTypes.Count) editor condition types, $($legacyTypes.Count) legacy compatibility types, and RuntimeSelection settings verified."
