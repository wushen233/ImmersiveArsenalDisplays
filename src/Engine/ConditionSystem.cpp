#include "pch.h"
#include "ConditionSystem.h"
#include "Data/ConfigManager.h" 
#include "Engine/NodeManager.h"
#include "System/KeyBindStateManager.h"

#include <RE/T/TESObjectREFR.h>
#include <RE/A/Actor.h>
#include <RE/A/ActorValue.h>
#include <RE/A/ActorValueInfo.h>
#include <RE/T/TESNPC.h>               
#include <RE/E/ENUM_FORM_ID.h>         
#include <RE/S/SIT_SLEEP_STATE.h>      
#include <RE/P/PlayerCharacter.h>
#include <RE/P/PowerArmor.h>
#include <RE/T/TESForm.h>
#include <RE/T/TESBoundObject.h>
#include <RE/T/TESObjectWEAP.h>
#include <RE/T/TESObjectARMO.h>
#include <RE/B/BGSDirectionalAmbientLightingColors.h>
#include <RE/B/BGSDefaultObjectManager.h>
#include <RE/B/BGSLightingTemplate.h>
#include <RE/D/DEFAULT_OBJECT.h>
#include <RE/I/INTERIOR_DATA.h>
#include <RE/T/TESClass.h>
#include <RE/T/TESCombatStyle.h>
#include <RE/T/TESValueForm.h>
#include <RE/B/BGSKeywordForm.h>
#include <RE/B/BGSBipedObjectForm.h>
#include <RE/B/BGSLocation.h>
#include <RE/B/BGSMod.h>
#include <RE/B/BGSObjectInstanceExtra.h>
#include <RE/B/BGSPerk.h>
#include <RE/T/TESFaction.h>
#include <RE/T/TESGlobal.h>
#include <RE/T/TESObjectCELL.h>
#include <RE/T/TESRace.h>
#include <RE/S/Sky.h>
#include <RE/B/BSContainer.h>
#include <RE/N/NiPoint3.h>
#include <RE/T/TESClimate.h>
#include <RE/T/TESWeather.h>
#include <RE/T/TESWorldSpace.h>
#include <RE/U/UI.h>
#include <RE/C/Calendar.h>
#include <RE/B/BGSInventoryItem.h>
#include <RE/B/BGSInventoryList.h>
#include <RE/E/ExtraDataList.h>
#include <RE/A/ActiveEffect.h>
#include <RE/A/ActiveEffectList.h>
#include <RE/E/EffectItem.h>
#include <RE/T/TESQuest.h>

#include <algorithm>
#include <initializer_list>

namespace IAD
{
	namespace
	{
		bool IsSkyrimOnlyConditionType(const std::string& a_type)
		{
			return a_type == "CanDualWield" ||
				a_type == "IsArrested" ||
				a_type == "IsMount" ||
				a_type == "IsMountRidden" ||
				a_type == "IsHorse" ||
				a_type == "IsMountPointClear" ||
				a_type == "IsRidingMount" ||
				a_type == "IsGettingOnOffMount" ||
				a_type == "IsPlayerLastRiddenMount" ||
				a_type == "IsPlayerLastRiddenMountAttachedToCell" ||
				a_type == "IsLastMountAttachedToCell" ||
				a_type == "HasMountMutualReference" ||
				a_type == "MountMutalReference" ||
				a_type == "XP32Skeleton";
		}

		bool EquippedDrawnWeaponHasAnyKeyword(RE::Actor* a_actor, std::initializer_list<const char*> a_keywords)
		{
			if (!a_actor || !a_actor->inventoryList) return false;
			if (a_actor->weaponState < RE::WEAPON_STATE::kWantToDraw || a_actor->weaponState > RE::WEAPON_STATE::kSheathing) return false;

			for (auto& invItem : a_actor->inventoryList->data) {
				if (!invItem.object || invItem.object->GetFormType() != RE::ENUM_FORM_ID::kWEAP) continue;

				bool isEquipped = false;
				auto stack = invItem.stackData.get();
				while (stack) {
					if (stack->IsEquipped()) {
						isEquipped = true;
						break;
					}
					stack = stack->nextStack.get();
				}
				if (!isEquipped) continue;

				auto kwdForm = invItem.object->As<RE::BGSKeywordForm>();
				if (!kwdForm) continue;

				for (auto keyword : a_keywords) {
					if (kwdForm->HasKeywordString(keyword)) {
						return true;
					}
				}
			}

			return false;
		}

		std::uint32_t ParseFormIDParam(const std::string& a_value)
		{
			if (a_value.empty()) return 0;
			try {
				auto text = a_value;
				if (text.rfind("0x", 0) == 0 || text.rfind("0X", 0) == 0) {
					text = text.substr(2);
				}
				return static_cast<std::uint32_t>(std::stoul(text, nullptr, 16));
			}
			catch (...) {
				return 0;
			}
		}

		std::uint8_t ParseFormTypeParam(const std::string& a_value)
		{
			if (a_value.empty()) return 0;
			auto named = ConfigManager::StringToFormType(a_value);
			if (named != 0) return named;
			try {
				return static_cast<std::uint8_t>(std::stoul(a_value, nullptr, 0));
			}
			catch (...) {
				return 0;
			}
		}

		bool CandidateHasKeyword(const ActiveItem* a_item, const char* a_keywordEditorID)
		{
			if (!a_item || !a_item->object || !a_keywordEditorID || a_keywordEditorID[0] == '\0') return false;
			auto kwdForm = a_item->object->As<RE::BGSKeywordForm>();
			return kwdForm && kwdForm->HasKeywordString(a_keywordEditorID);
		}

		bool CandidateHasOMOD(const ActiveItem* a_item, std::uint32_t a_omodID)
		{
			if (!a_item || !a_item->stack || !a_item->stack->extra || a_omodID == 0) return false;
			auto* requiredMod = RE::TESForm::GetFormByID<RE::BGSMod::Attachment::Mod>(a_omodID);
			if (!requiredMod) return false;
			auto* instExtra = a_item->stack->extra->GetByType<RE::BGSObjectInstanceExtra>();
			return instExtra && instExtra->HasMod(*requiredMod);
		}

		bool CandidateMatchesFormID(const ActiveItem* a_item, std::uint32_t a_formID)
		{
			return a_item && a_item->object && a_formID != 0 && a_item->object->GetFormID() == a_formID;
		}

		bool CandidateMatchesFormType(const ActiveItem* a_item, std::uint8_t a_formType)
		{
			return a_item && a_item->object && a_formType != 0 && static_cast<std::uint8_t>(a_item->object->GetFormType()) == a_formType;
		}

		float GetInstanceAwareItemRating(RE::TESBoundObject* a_object, RE::BGSInventoryItem::Stack* a_stack, bool& a_usesInstanceData)
		{
			a_usesInstanceData = false;
			if (!a_object) return 0.0f;

			RE::BSTSmartPointer<RE::TBO_InstanceData> instanceData;
			if (a_stack && a_stack->extra) {
				// CreateInstanceData is a builder, not a const query. Give it a private
				// ExtraDataList copy so OMOD application never targets the live stack.
				auto rawExtra = new RE::ExtraDataList();
				rawExtra->CopyList(a_stack->extra.get());
				RE::BSTSmartPointer<RE::ExtraDataList> isolatedExtra(rawExtra);
				if (auto* created = isolatedExtra->CreateInstanceData(a_object, false)) {
					instanceData.reset(created);
					a_usesInstanceData = true;
				}
			}

			float rating = 0.0f;
			if (auto* weapon = a_object->As<RE::TESObjectWEAP>()) {
				auto* weaponData = instanceData ? static_cast<RE::TESObjectWEAP::InstanceData*>(instanceData.get()) : &weapon->weaponData;
				rating = static_cast<float>(weaponData->attackDamage);
			}
			else if (auto* armor = a_object->As<RE::TESObjectARMO>()) {
				auto* armorData = instanceData ? static_cast<RE::TESObjectARMO::InstanceData*>(instanceData.get()) : &armor->armorData;
				rating = static_cast<float>(armorData->rating);
			}

			if (auto* valueForm = a_object->As<RE::TESValueForm>()) {
				const auto value = instanceData ? RE::TESValueForm::GetFormValue(a_object, instanceData.get()) : valueForm->value;
				rating += static_cast<float>(value) * 0.01f;
			}

			return rating;
		}

		std::string StripManagedNodePrefix(const std::string& a_name)
		{
			if (a_name.rfind("IAD_CME_", 0) == 0 || a_name.rfind("IAD_MOV_", 0) == 0) {
				return a_name.substr(8);
			}
			return a_name;
		}

		void AddUniqueNodeCandidate(std::vector<std::string>& a_candidates, const std::string& a_name)
		{
			if (a_name.empty()) return;
			if (std::find(a_candidates.begin(), a_candidates.end(), a_name) == a_candidates.end()) {
				a_candidates.push_back(a_name);
			}
		}

		std::vector<std::string> BuildManagedNodeNameCandidates(const std::string& a_name)
		{
			std::vector<std::string> candidates;
			const auto stripped = StripManagedNodePrefix(a_name);
			AddUniqueNodeCandidate(candidates, a_name);
			AddUniqueNodeCandidate(candidates, stripped);
			AddUniqueNodeCandidate(candidates, "IAD_CME_" + stripped);
			AddUniqueNodeCandidate(candidates, "IAD_MOV_" + stripped);
			return candidates;
		}

		bool ParseGlobalComparison(const std::string& a_expression, std::string& a_operator, float& a_value)
		{
			auto expr = a_expression;
			expr.erase(expr.begin(), std::find_if(expr.begin(), expr.end(), [](unsigned char c) { return !std::isspace(c); }));
			expr.erase(std::find_if(expr.rbegin(), expr.rend(), [](unsigned char c) { return !std::isspace(c); }).base(), expr.end());
			if (expr.empty()) {
				a_operator = "!=";
				a_value = 0.0f;
				return true;
			}

			const char* ops[] = { ">=", "<=", "==", "!=", ">", "<", "=" };
			for (auto op : ops) {
				const std::string opText = op;
				if (expr.rfind(opText, 0) == 0) {
					a_operator = opText == "=" ? "==" : opText;
					try {
						a_value = std::stof(expr.substr(opText.size()));
						return true;
					}
					catch (...) {
						return false;
					}
				}
			}

			a_operator = "==";
			try {
				a_value = std::stof(expr);
				return true;
			}
			catch (...) {
				return false;
			}
		}

		bool CompareFloat(float a_lhs, const std::string& a_operator, float a_rhs)
		{
			if (a_operator == ">") return a_lhs > a_rhs;
			if (a_operator == ">=") return a_lhs >= a_rhs;
			if (a_operator == "<") return a_lhs < a_rhs;
			if (a_operator == "<=") return a_lhs <= a_rhs;
			if (a_operator == "!=") return std::fabs(a_lhs - a_rhs) > 0.0001f;
			return std::fabs(a_lhs - a_rhs) <= 0.0001f;
		}

		constexpr float kInteriorAmbientLightThreshold = 0.425f;
		constexpr float kPi = 3.14159265358979323846f;
		constexpr float kHalfPi = kPi * 0.5f;
		constexpr std::uint32_t kLightingTemplateInheritAmbientColor = 1u << 0;

		bool IsSkyDaytime(const RE::Sky* a_sky);

		std::uint32_t PackedColorChannel(std::uint32_t a_color, std::uint32_t a_shift)
		{
			return (a_color >> a_shift) & 0xFFu;
		}

		float PackedColorIntensity(std::uint32_t a_color)
		{
			const auto r = PackedColorChannel(a_color, 0);
			const auto g = PackedColorChannel(a_color, 8);
			const auto b = PackedColorChannel(a_color, 16);
			return static_cast<float>(r + g + b) * (1.0f / (255.0f * 3.0f));
		}

		float AmbientAxisIntensity(std::uint32_t a_positive, std::uint32_t a_negative)
		{
			const auto r = std::max(PackedColorChannel(a_positive, 0), PackedColorChannel(a_negative, 0));
			const auto g = std::max(PackedColorChannel(a_positive, 8), PackedColorChannel(a_negative, 8));
			const auto b = std::max(PackedColorChannel(a_positive, 16), PackedColorChannel(a_negative, 16));
			return static_cast<float>(r + g + b) * (1.0f / (255.0f * 3.0f));
		}

		float DirectionalAmbientIntensity(const RE::BGSDirectionalAmbientLightingColors& a_colors)
		{
			return AmbientAxisIntensity(
					   a_colors.colorValues[static_cast<std::uint32_t>(RE::BGSDirectionalAmbientLightingColors::ColorIndex::kXPos)],
					   a_colors.colorValues[static_cast<std::uint32_t>(RE::BGSDirectionalAmbientLightingColors::ColorIndex::kXNeg)]) +
			       AmbientAxisIntensity(
					   a_colors.colorValues[static_cast<std::uint32_t>(RE::BGSDirectionalAmbientLightingColors::ColorIndex::kYPos)],
					   a_colors.colorValues[static_cast<std::uint32_t>(RE::BGSDirectionalAmbientLightingColors::ColorIndex::kYNeg)]) +
			       AmbientAxisIntensity(
					   a_colors.colorValues[static_cast<std::uint32_t>(RE::BGSDirectionalAmbientLightingColors::ColorIndex::kZPos)],
					   a_colors.colorValues[static_cast<std::uint32_t>(RE::BGSDirectionalAmbientLightingColors::ColorIndex::kZNeg)]);
		}

		float InteriorAmbientLevel(const RE::INTERIOR_DATA* a_data)
		{
			if (!a_data) return -1.0f;
			return std::max(
				DirectionalAmbientIntensity(a_data->directionalAmbientLightingColors),
				PackedColorIntensity(a_data->ambient));
		}

		float LightingTemplateAmbientLevel(const RE::BGSLightingTemplate* a_template)
		{
			if (!a_template) return -1.0f;
			return std::max(
				DirectionalAmbientIntensity(a_template->directionalAmbientLightingColors),
				PackedColorIntensity(a_template->data.ambient));
		}

		float GetInteriorAmbientLightLevel(RE::Actor* a_actor)
		{
			if (!a_actor) return -1.0f;
			auto cell = a_actor->GetParentCell();
			if (!cell || !cell->IsInterior()) return -1.0f;

			const auto* interiorData = cell->cellData.interior;
			if (!interiorData || (interiorData->lightingTemplateInheritanceFlags & kLightingTemplateInheritAmbientColor) != 0) {
				if (const auto templateLevel = LightingTemplateAmbientLevel(cell->lightingTemplate); templateLevel >= 0.0f) {
					return templateLevel;
				}
			}

			return InteriorAmbientLevel(interiorData);
		}

		float GetExteriorAmbientLightLevel()
		{
			auto sky = RE::Sky::GetSingleton();
			if (!sky) return -1.0f;

			float result = 0.0f;
			for (const auto& axis : sky->directionalAmbientColorsA) {
				float axisLevel = 0.0f;
				for (const auto& color : axis) {
					axisLevel = std::max(axisLevel, (color.r + color.g + color.b) / 3.0f);
				}
				result += axisLevel;
			}
			return result;
		}

		bool IsExteriorDark()
		{
			const auto exteriorAmbient = GetExteriorAmbientLightLevel();
			if (exteriorAmbient >= 0.0f && exteriorAmbient != 0.0f) {
				return exteriorAmbient < 0.6f;
			}
			return !IsSkyDaytime(RE::Sky::GetSingleton());
		}

		RE::TESNPC* GetActorNPCBase(RE::Actor* a_actor)
		{
			if (!a_actor || !a_actor->data.objectReference) return nullptr;
			return a_actor->data.objectReference->As<RE::TESNPC>();
		}

		struct ClimateTimingData {
			float sunriseBegin = 6.0f;
			float sunriseEnd = 10.5f;
			float sunsetBegin = 15.5f;
			float sunsetEnd = 20.5f;
		};

		enum class TimeOfDayPhase {
			kNight,
			kSunrise,
			kDay,
			kSunset
		};

		float NormalizeGameHour(float a_hour)
		{
			auto hour = std::fmod(a_hour, 24.0f);
			return hour < 0.0f ? hour + 24.0f : hour;
		}

		std::string ToLowerCopy(std::string a_value)
		{
			std::transform(a_value.begin(), a_value.end(), a_value.begin(), [](unsigned char c) {
				return static_cast<char>(std::tolower(c));
			});
			return a_value;
		}

		bool ContainsInsensitive(const std::string& a_haystack, const std::string& a_needle)
		{
			if (a_needle.empty()) return false;
			return ToLowerCopy(a_haystack).find(ToLowerCopy(a_needle)) != std::string::npos;
		}

		std::string TrimCopy(std::string a_value)
		{
			a_value.erase(a_value.begin(), std::find_if(a_value.begin(), a_value.end(), [](unsigned char c) { return !std::isspace(c); }));
			a_value.erase(std::find_if(a_value.rbegin(), a_value.rend(), [](unsigned char c) { return !std::isspace(c); }).base(), a_value.end());
			return a_value;
		}

		std::string NormalizeKeyName(std::string a_value)
		{
			a_value = ToLowerCopy(TrimCopy(a_value));
			a_value.erase(std::remove_if(a_value.begin(), a_value.end(), [](unsigned char c) {
				return c == ' ' || c == '_' || c == '-';
			}), a_value.end());
			if (a_value.rfind("vk", 0) == 0) {
				a_value = a_value.substr(2);
			}
			return a_value;
		}

		std::uint32_t ParseVirtualKeyParam(const std::string& a_value)
		{
			auto key = NormalizeKeyName(a_value);
			if (key.empty()) return 0;

			if (key.size() == 1) {
				const auto c = static_cast<unsigned char>(key.front());
				if (std::isalnum(c)) return static_cast<std::uint32_t>(std::toupper(c));
			}

			try {
				if (key.rfind("0x", 0) == 0 || std::isdigit(static_cast<unsigned char>(key.front()))) {
					return static_cast<std::uint32_t>(std::stoul(key, nullptr, 0));
				}
			}
			catch (...) {
				return 0;
			}

			if (key.size() >= 2 && key.front() == 'f' && std::isdigit(static_cast<unsigned char>(key[1]))) {
				try {
					const auto index = std::stoul(key.substr(1), nullptr, 10);
					if (index >= 1 && index <= 24) return 0x70u + static_cast<std::uint32_t>(index - 1);
				}
				catch (...) {
					return 0;
				}
			}

			if (key == "space") return VK_SPACE;
			if (key == "tab") return VK_TAB;
			if (key == "enter" || key == "return") return VK_RETURN;
			if (key == "esc" || key == "escape") return VK_ESCAPE;
			if (key == "shift") return VK_SHIFT;
			if (key == "lshift" || key == "leftshift") return VK_LSHIFT;
			if (key == "rshift" || key == "rightshift") return VK_RSHIFT;
			if (key == "ctrl" || key == "control") return VK_CONTROL;
			if (key == "lctrl" || key == "leftctrl" || key == "leftcontrol") return VK_LCONTROL;
			if (key == "rctrl" || key == "rightctrl" || key == "rightcontrol") return VK_RCONTROL;
			if (key == "alt" || key == "menu") return VK_MENU;
			if (key == "lalt" || key == "leftalt") return VK_LMENU;
			if (key == "ralt" || key == "rightalt") return VK_RMENU;
			if (key == "backspace") return VK_BACK;
			if (key == "capslock" || key == "caps") return VK_CAPITAL;
			if (key == "insert" || key == "ins") return VK_INSERT;
			if (key == "delete" || key == "del") return VK_DELETE;
			if (key == "home") return VK_HOME;
			if (key == "end") return VK_END;
			if (key == "pageup" || key == "pgup") return VK_PRIOR;
			if (key == "pagedown" || key == "pgdn") return VK_NEXT;
			if (key == "left") return VK_LEFT;
			if (key == "right") return VK_RIGHT;
			if (key == "up") return VK_UP;
			if (key == "down") return VK_DOWN;
			if (key == "tilde" || key == "grave" || key == "`" || key == "~" || key == "oem3") return VK_OEM_3;
			if (key == "mouse1" || key == "leftmouse" || key == "lbutton") return VK_LBUTTON;
			if (key == "mouse2" || key == "rightmouse" || key == "rbutton") return VK_RBUTTON;
			if (key == "mouse3" || key == "middlemouse" || key == "mbutton") return VK_MBUTTON;
			if (key == "mouse4" || key == "xbutton1") return VK_XBUTTON1;
			if (key == "mouse5" || key == "xbutton2") return VK_XBUTTON2;

			return 0;
		}

		bool IsVirtualKeyDown(std::uint32_t a_vk)
		{
			return a_vk != 0 && (::GetAsyncKeyState(static_cast<int>(a_vk)) & 0x8000) != 0;
		}

		bool IsVirtualKeyComboDown(const std::string& a_combo)
		{
			bool hasKey = false;
			std::size_t start = 0;
			while (start <= a_combo.size()) {
				const auto end = a_combo.find('+', start);
				const auto token = TrimCopy(a_combo.substr(start, end == std::string::npos ? std::string::npos : end - start));
				if (!token.empty()) {
					const auto vk = ParseVirtualKeyParam(token);
					if (vk == 0 || !IsVirtualKeyDown(vk)) return false;
					hasKey = true;
				}
				if (end == std::string::npos) break;
				start = end + 1;
			}
			return hasKey;
		}

		float ParseFloatParam(const std::string& a_value, float a_default)
		{
			try {
				return a_value.empty() ? a_default : std::stof(a_value);
			}
			catch (...) {
				return a_default;
			}
		}

		std::string GetActorSkeletonPath(RE::Actor* a_actor)
		{
			if (!a_actor || !a_actor->race) return "";
			auto* base = GetActorNPCBase(a_actor);
			const auto sex = (base && base->GetSex() == RE::SEX::kFemale) ? 1 : 0;
			return a_actor->race->skeletonModel[sex].model.c_str() ? a_actor->race->skeletonModel[sex].model.c_str() : "";
		}

		bool ActorHas3DNode(RE::Actor* a_actor, const std::string& a_nodeName)
		{
			if (!a_actor || a_nodeName.empty()) return false;
			auto root = static_cast<RE::NiNode*>(a_actor->Get3D(false));
			return root && NodeManager::GetNodeByName(root, a_nodeName) != nullptr;
		}

		bool ActorHasAll3DNodes(RE::Actor* a_actor, std::initializer_list<const char*> a_nodeNames)
		{
			if (!a_actor) return false;
			for (const auto* nodeName : a_nodeNames) {
				if (!ActorHas3DNode(a_actor, nodeName)) return false;
			}
			return true;
		}

		bool ActorHasAny3DNode(RE::Actor* a_actor, std::initializer_list<const char*> a_nodeNames)
		{
			if (!a_actor) return false;
			for (const auto* nodeName : a_nodeNames) {
				if (ActorHas3DNode(a_actor, nodeName)) return true;
			}
			return false;
		}

		ClimateTimingData GetClimateTimingData(const RE::Sky* a_sky)
		{
			ClimateTimingData result;
			if (a_sky && a_sky->currentClimate) {
				const auto* timing = a_sky->currentClimate->data;
				result.sunriseBegin = static_cast<float>(static_cast<std::uint8_t>(timing[static_cast<std::size_t>(RE::TESClimate::TransTime::kSunriseBegin)])) / 6.0f;
				result.sunriseEnd = static_cast<float>(static_cast<std::uint8_t>(timing[static_cast<std::size_t>(RE::TESClimate::TransTime::kSunriseEnd)])) / 6.0f;
				result.sunsetBegin = static_cast<float>(static_cast<std::uint8_t>(timing[static_cast<std::size_t>(RE::TESClimate::TransTime::kSunsetBegin)])) / 6.0f;
				result.sunsetEnd = static_cast<float>(static_cast<std::uint8_t>(timing[static_cast<std::size_t>(RE::TESClimate::TransTime::kSunsetEnd)])) / 6.0f;
			}
			return result;
		}

		TimeOfDayPhase GetTimeOfDayPhase(const RE::Sky* a_sky)
		{
			if (!a_sky) return TimeOfDayPhase::kDay;
			const auto timing = GetClimateTimingData(a_sky);
			const auto hour = NormalizeGameHour(a_sky->currentGameHour);
			if (hour < timing.sunriseBegin) return TimeOfDayPhase::kNight;
			if (hour < timing.sunriseEnd) return TimeOfDayPhase::kSunrise;
			if (hour < timing.sunsetBegin) return TimeOfDayPhase::kDay;
			if (hour < timing.sunsetEnd) return TimeOfDayPhase::kSunset;
			return TimeOfDayPhase::kNight;
		}

		bool IsSkyDaytime(const RE::Sky* a_sky)
		{
			if (!a_sky) return true;
			const auto timing = GetClimateTimingData(a_sky);
			const auto hour = NormalizeGameHour(a_sky->currentGameHour);
			const auto sunriseMid = timing.sunriseBegin + ((timing.sunriseEnd - timing.sunriseBegin) / 2.0f - 0.25f);
			const auto sunsetMid = timing.sunsetBegin + ((timing.sunsetEnd - timing.sunsetBegin) / 2.0f + 0.25f);
			return hour >= sunriseMid && hour < sunsetMid;
		}

		float GetSunAngleRadians()
		{
			auto sky = RE::Sky::GetSingleton();
			if (!sky) return 0.0f;

			const auto timing = GetClimateTimingData(sky);
			const auto hour = NormalizeGameHour(sky->currentGameHour);
			const auto sunriseMid = NormalizeGameHour(timing.sunriseBegin + (timing.sunriseEnd - timing.sunriseBegin) * 0.5f);
			const auto sunsetMid = NormalizeGameHour(timing.sunsetBegin + (timing.sunsetEnd - timing.sunsetBegin) * 0.5f);

			if (sunriseMid < sunsetMid && hour >= sunriseMid && hour <= sunsetMid) {
				const auto dayProgress = (hour - sunriseMid) / std::max(sunsetMid - sunriseMid, 0.01f);
				return -kHalfPi + dayProgress * kPi;
			}

			const auto nightLength = (sunriseMid + 24.0f) - sunsetMid;
			const auto nightProgress = (hour >= sunsetMid ? hour - sunsetMid : hour + 24.0f - sunsetMid) / std::max(nightLength, 0.01f);
			auto angle = kHalfPi + nightProgress * kPi;
			if (angle > kPi) angle -= kPi * 2.0f;
			return angle;
		}

		bool MatchTimeOfDayPhase(TimeOfDayPhase a_phase, const std::string& a_match)
		{
			const auto value = ToLowerCopy(a_match);
			if (value == "night" || value == "0") return a_phase == TimeOfDayPhase::kNight;
			if (value == "sunrise" || value == "1") return a_phase == TimeOfDayPhase::kSunrise;
			if (value == "day" || value == "2") return a_phase == TimeOfDayPhase::kDay;
			if (value == "sunset" || value == "3") return a_phase == TimeOfDayPhase::kSunset;
			return false;
		}

		RE::TESWeather* GetCurrentWeather()
		{
			auto sky = RE::Sky::GetSingleton();
			if (!sky) return nullptr;
			if (sky->currentWeather) return sky->currentWeather;
			if (sky->overrideWeather) return sky->overrideWeather;
			return sky->defaultWeather;
		}

		std::uint8_t GetWeatherDataFlags(RE::TESWeather* a_weather)
		{
			if (!a_weather) return 0;
			return static_cast<std::uint8_t>(a_weather->weatherData[static_cast<int>(RE::TESWeather::WeatherData::kFlags)]);
		}

		bool FactionHasVendorData(RE::TESFaction* a_faction)
		{
			if (!a_faction) return false;
			const auto& vendorData = a_faction->vendorData;
			if (vendorData.vendorSellBuyList || vendorData.merchantContainer || vendorData.vendorLocation || vendorData.vendorConditions) {
				return true;
			}
			return vendorData.vendorValues.buysNonStolen || vendorData.vendorValues.buysStolen;
		}

		template <class TFunc>
		bool ActorBaseFactionAny(RE::TESNPC* a_base, TFunc&& a_func)
		{
			if (!a_base) return false;
			for (const auto& factionRank : a_base->factions) {
				if (factionRank.faction && a_func(factionRank.faction)) {
					return true;
				}
			}
			return false;
		}

		std::uint32_t ParseDayOfWeekParam(const std::string& a_value)
		{
			if (a_value == "Sunday") return 0;
			if (a_value == "Monday") return 1;
			if (a_value == "Tuesday") return 2;
			if (a_value == "Wednesday") return 3;
			if (a_value == "Thursday") return 4;
			if (a_value == "Friday") return 5;
			if (a_value == "Saturday") return 6;
			try {
				return static_cast<std::uint32_t>(std::stoul(a_value, nullptr, 0)) % 7u;
			}
			catch (...) {
				return 0;
			}
		}

		bool ParseLifeStateParam(const std::string& a_value, std::uint32_t& a_state)
		{
			if (a_value == "Alive") { a_state = static_cast<std::uint32_t>(RE::ACTOR_LIFE_STATE::kAlive); return true; }
			if (a_value == "Dying") { a_state = static_cast<std::uint32_t>(RE::ACTOR_LIFE_STATE::kDying); return true; }
			if (a_value == "Dead") { a_state = static_cast<std::uint32_t>(RE::ACTOR_LIFE_STATE::kDead); return true; }
			if (a_value == "Unconscious") { a_state = static_cast<std::uint32_t>(RE::ACTOR_LIFE_STATE::kUnconscious); return true; }
			if (a_value == "Reanimate") { a_state = static_cast<std::uint32_t>(RE::ACTOR_LIFE_STATE::kReanimate); return true; }
			if (a_value == "Recycle") { a_state = static_cast<std::uint32_t>(RE::ACTOR_LIFE_STATE::kRecycle); return true; }
			if (a_value == "Restrained") { a_state = static_cast<std::uint32_t>(RE::ACTOR_LIFE_STATE::kRestrained); return true; }
			if (a_value == "EssentialDown") { a_state = static_cast<std::uint32_t>(RE::ACTOR_LIFE_STATE::kEssentialDown); return true; }
			if (a_value == "Bleedout") { a_state = static_cast<std::uint32_t>(RE::ACTOR_LIFE_STATE::kBleedout); return true; }
			try {
				a_state = static_cast<std::uint32_t>(std::stoul(a_value, nullptr, 0));
				return true;
			}
			catch (...) {
				return false;
			}
		}

		void HashCombine(std::uint64_t& a_hash, std::uint64_t a_value)
		{
			a_hash ^= a_value;
			a_hash *= 1099511628211ull;
		}

		std::uint64_t StableStringHash(const std::string& a_text)
		{
			std::uint64_t hash = 1469598103934665603ull;
			for (auto c : a_text) {
				HashCombine(hash, static_cast<unsigned char>(c));
			}
			return hash;
		}
	}

	std::vector<ActiveItem> Scanner::GetActiveItems(RE::Actor* a_actor, bool a_favoritesOnly)
	{
		std::vector<ActiveItem> results;
		if (!a_actor || !a_actor->inventoryList) return results;

		for (auto& invItem : a_actor->inventoryList->data) {
			if (!invItem.object) continue;

			auto formType = invItem.object->GetFormType();
			if (formType != RE::ENUM_FORM_ID::kWEAP &&
				formType != RE::ENUM_FORM_ID::kARMO &&
				formType != RE::ENUM_FORM_ID::kALCH &&
				formType != RE::ENUM_FORM_ID::kMISC &&
				formType != RE::ENUM_FORM_ID::kAMMO) continue;

			auto* currentStack = invItem.stackData.get();
			std::uint32_t totalCount = 0;
			bool baseEquipped = false;
			bool baseFavorited = false;

			auto tempStack = currentStack;
			while (tempStack) {
				if (tempStack->IsEquipped()) baseEquipped = true;
				if (tempStack->extra && tempStack->extra->IsFavorite()) baseFavorited = true;
				totalCount += tempStack->count;
				tempStack = tempStack->nextStack.get();
			}

			if (a_favoritesOnly && !baseEquipped && !baseFavorited) continue;

			std::uint32_t stackID = 0;
			while (currentStack && totalCount > 0) {
				ActiveItem split;
				split.object = invItem.object;
				split.stack = currentStack;
				split.stackID = stackID++;
				split.count = currentStack->count;
				if (split.count > totalCount) split.count = totalCount;
				split.isEquipped = currentStack->IsEquipped();

				split.isFavorited = false;
				if (currentStack->extra && currentStack->extra->IsFavorite()) {
					split.isFavorited = true;
				}

				split.rating = GetInstanceAwareItemRating(invItem.object, currentStack, split.ratingUsesInstanceData);

				std::uint64_t fID = static_cast<std::uint64_t>(split.object->GetFormID());
				std::uint64_t extraAddr = 0;
				if (currentStack->extra) {
					auto* instExtra = currentStack->extra->GetByType<RE::BGSObjectInstanceExtra>();
					if (instExtra) extraAddr = reinterpret_cast<std::uint64_t>(instExtra->values);
					else extraAddr = reinterpret_cast<std::uint64_t>(currentStack->extra.get());
				}
				split.uid = fID ^ (extraAddr << 16);

				results.push_back(split);
				totalCount -= split.count;
				currentStack = currentStack->nextStack.get();
			}

			if (!invItem.stackData.get() && totalCount > 0) {
				ActiveItem dummy;
				dummy.object = invItem.object;
				dummy.stack = nullptr;
				dummy.count = totalCount;
				dummy.isEquipped = false;
				dummy.isFavorited = false;
				dummy.rating = static_cast<float>(invItem.object->GetFormID());
				dummy.uid = static_cast<std::uint64_t>(invItem.object->GetFormID());
				results.push_back(dummy);
			}
		}

		std::sort(results.begin(), results.end(), [](const ActiveItem& a, const ActiveItem& b) {
			if (a.isEquipped != b.isEquipped) return a.isEquipped > b.isEquipped;
			if (a.isFavorited != b.isFavorited) return a.isFavorited > b.isFavorited;
			return a.uid < b.uid;
			});

		return results;
	}

	std::uint32_t Scanner::GetItemCount(RE::Actor* a_actor, std::uint32_t a_formID) {
		if (!a_actor || !a_actor->inventoryList) return 0;
		for (auto& invItem : a_actor->inventoryList->data) {
			if (invItem.object && invItem.object->GetFormID() == a_formID) {
				std::uint32_t totalCount = 0;
				auto stack = invItem.stackData.get();
				while (stack) {
					totalCount += stack->count;
					stack = stack->nextStack.get();
				}
				return totalCount;
			}
		}
		return 0;
	}

	bool ConditionEvaluator::PassesFormFilter(std::uint32_t a_formID, const FormFilter& a_filter) {
		if (a_filter.denyAll) return false;
		if (a_filter.denyList.find(a_formID) != a_filter.denyList.end()) return false;
		if (!a_filter.allowList.empty() && a_filter.allowList.find(a_formID) == a_filter.allowList.end()) return false;
		return true;
	}

	bool ConditionEvaluator::PassesLegacyFilters(RE::TESBoundObject* obj, const AdvancedItemFilters& af, KeywordFilterMode kwMode, const std::vector<KeywordGroup>& kwGroups) {
		auto formType = obj->GetFormType();

		if (af.useBaseFilters) {
			bool basePassed = false;

			if (formType == RE::ENUM_FORM_ID::kWEAP) {
				auto kwdForm = obj->As<RE::BGSKeywordForm>();
				bool isMelee = false, isThrown = false, isGun = false, isOneHanded = false, isTwoHanded = false;

				if (kwdForm) {
					if (kwdForm->HasKeywordString("WeaponTypeMelee1H") || kwdForm->HasKeywordString("WeaponTypeMelee2H") || kwdForm->HasKeywordString("WeaponTypeUnarmed")) isMelee = true;
					else if (kwdForm->HasKeywordString("WeaponTypeExplosive") || kwdForm->HasKeywordString("WeaponTypeGrenade") || kwdForm->HasKeywordString("WeaponTypeMine") || kwdForm->HasKeywordString("WeaponTypeThrown")) isThrown = true;
					else isGun = true;

					if (kwdForm->HasKeywordString("WeaponTypeMelee1H") || kwdForm->HasKeywordString("WeaponTypePistol") || kwdForm->HasKeywordString("WeaponTypeUnarmed") || kwdForm->HasKeywordString("WeaponTypeAlienBlaster") || isThrown) isOneHanded = true;
					else if (kwdForm->HasKeywordString("WeaponTypeMelee2H") || kwdForm->HasKeywordString("WeaponTypeRifle") || kwdForm->HasKeywordString("WeaponTypeHeavyGun") || kwdForm->HasKeywordString("WeaponTypeShotgun") || kwdForm->HasKeywordString("WeaponTypeSniper")) isTwoHanded = true;
				}
				else { isGun = true; isOneHanded = true; }

				if (isMelee && af.allowMelee) {
					bool handMatch = true;
					if (!af.allowOneHanded && isOneHanded) handMatch = false;
					if (!af.allowTwoHanded && isTwoHanded) handMatch = false;
					if (handMatch) basePassed = true;
				}
				else if (isGun && af.allowGun) {
					bool handMatch = true;
					if (!af.allowOneHanded && isOneHanded) handMatch = false;
					if (!af.allowTwoHanded && isTwoHanded) handMatch = false;
					if (handMatch) basePassed = true;
				}
				else if (isThrown && af.allowThrown) basePassed = true;
			}
			else if (formType == RE::ENUM_FORM_ID::kARMO && (af.allowArmor || af.allowShield)) basePassed = true;
			else if (formType == RE::ENUM_FORM_ID::kALCH && (af.allowFood || af.allowMedicine)) basePassed = true;
			else if (formType == RE::ENUM_FORM_ID::kAMMO && af.allowAmmo) basePassed = true;
			else if (formType == RE::ENUM_FORM_ID::kMISC && af.allowKeys) basePassed = true;

			if (!basePassed) return false;
		}

		if (kwMode != KeywordFilterMode::kNone) {
			bool hasKeywords = false;
			for (const auto& g : kwGroups) { if (!g.keywords.empty()) { hasKeywords = true; break; } }

			if (!hasKeywords) {
				if (kwMode == KeywordFilterMode::kWhitelist) return false;
			}
			else {
				auto kwdForm = obj->As<RE::BGSKeywordForm>();
				bool matchedAnyGroup = false;
				if (kwdForm) {
					for (const auto& group : kwGroups) {
						if (group.keywords.empty()) continue;
						bool groupPass = group.isAnd ? true : false;
						for (const auto& kw : group.keywords) {
							bool hasKw = kwdForm->HasKeywordString(kw.c_str());
							if (group.isAnd) { groupPass = groupPass && hasKw; if (!groupPass) break; }
							else { groupPass = groupPass || hasKw; if (groupPass) break; }
						}
						if (groupPass) { matchedAnyGroup = true; break; }
					}
				}
				if (kwMode == KeywordFilterMode::kWhitelist && !matchedAnyGroup) return false;
				if (kwMode == KeywordFilterMode::kBlacklist && matchedAnyGroup) return false;
			}
		}
		return true;
	}

	bool ConditionEvaluator::IsUsingPipboy(RE::Actor* a_actor) {
		if (a_actor != RE::PlayerCharacter::GetSingleton()) return false;
		auto ui = RE::UI::GetSingleton();
		return ui && ui->GetMenuOpen("PipboyMenu");
	}

	bool ConditionEvaluator::IsTimeOfDay(float a_startTime, float a_endTime) {
		auto cal = RE::Calendar::GetSingleton();
		if (!cal) return false;
		float currentHour = cal->GetHoursPassed();
		float timeOfDay = std::fmod(currentHour, 24.0f);
		if (a_startTime < a_endTime) return (timeOfDay >= a_startTime && timeOfDay <= a_endTime);
		else return (timeOfDay >= a_startTime || timeOfDay <= a_endTime);
	}

	bool ConditionEvaluator::IsSneaking(RE::Actor* a_actor) { return a_actor && a_actor->IsSneaking(); }
	bool ConditionEvaluator::IsInCombat(RE::Actor* a_actor) { return a_actor && a_actor->IsInCombat(); }

	bool ConditionEvaluator::IsSprinting(RE::Actor* a_actor) {
		if (!a_actor) return false;
		if (a_actor == RE::PlayerCharacter::GetSingleton()) return static_cast<RE::PlayerCharacter*>(a_actor)->sprintToggled;
		return a_actor->GetDesiredSpeed() > 200.0f;
	}

	bool ConditionEvaluator::IsSitting(RE::Actor* a_actor) {
		if (!a_actor) return false;
		auto state = a_actor->DoGetSitSleepState();
		return state == RE::SIT_SLEEP_STATE::kIsSitting;
	}

	bool ConditionEvaluator::IsSleeping(RE::Actor* a_actor) {
		return a_actor ? a_actor->DoGetSitSleepState() == RE::SIT_SLEEP_STATE::kIsSleeping : false;
	}

	bool ConditionEvaluator::IsWeaponDrawn(RE::Actor* a_actor) {
		return a_actor && a_actor->weaponState >= RE::WEAPON_STATE::kWantToDraw && a_actor->weaponState <= RE::WEAPON_STATE::kSheathing;
	}

	bool ConditionEvaluator::IsWeaponDrawing(RE::Actor* a_actor) {
		return a_actor && (a_actor->weaponState == RE::WEAPON_STATE::kDrawing || a_actor->weaponState == RE::WEAPON_STATE::kWantToDraw);
	}

	bool ConditionEvaluator::IsWeaponDrawnStrict(RE::Actor* a_actor) {
		return a_actor && a_actor->weaponState == RE::WEAPON_STATE::kDrawn;
	}

	bool ConditionEvaluator::IsWeaponSheathing(RE::Actor* a_actor) {
		return a_actor && (a_actor->weaponState == RE::WEAPON_STATE::kSheathing || a_actor->weaponState == RE::WEAPON_STATE::kWantToSheathe);
	}

	bool ConditionEvaluator::IsWeaponSheathed(RE::Actor* a_actor) {
		return a_actor && a_actor->weaponState == RE::WEAPON_STATE::kSheathed;
	}

	bool ConditionEvaluator::IsDrawnPistol(RE::Actor* a_actor) {
		return EquippedDrawnWeaponHasAnyKeyword(a_actor, { "WeaponTypePistol", "WeaponTypeAlienBlaster" });
	}

	bool ConditionEvaluator::IsDrawnRifle(RE::Actor* a_actor) {
		return EquippedDrawnWeaponHasAnyKeyword(a_actor, { "WeaponTypeRifle", "WeaponTypeShotgun", "WeaponTypeSniper", "WeaponTypeHeavyGun" });
	}

	bool ConditionEvaluator::IsDrawnMelee(RE::Actor* a_actor) {
		return EquippedDrawnWeaponHasAnyKeyword(a_actor, { "WeaponTypeMelee1H", "WeaponTypeMelee2H", "WeaponTypeUnarmed" });
	}

	bool ConditionEvaluator::IsBleedingOut(RE::Actor* a_actor) {
		return a_actor && (
			a_actor->lifeState == static_cast<std::uint32_t>(RE::ACTOR_LIFE_STATE::kBleedout) ||
			a_actor->boolFlags.all(RE::Actor::BOOL_FLAGS::kInBleedoutAnimation));
	}

	bool ConditionEvaluator::IsFirstPerson(RE::Actor* a_actor) {
		if (!a_actor || !a_actor->IsPlayerRef()) return false;
		auto thirdPersonNode = a_actor->Get3D(false);
		return thirdPersonNode == nullptr || thirdPersonNode->GetAppCulled();
	}

	bool ConditionEvaluator::IsInPowerArmor(RE::Actor* a_actor) {
		if (!a_actor) return false;
		// ActorHasKeyword() inspects the actor base and can remain true/false
		// across a power-armor race switch. Use the game helper for the live
		// state so configuration state overrides follow the actual PA session.
		__try {
			return RE::PowerArmor::ActorInPowerArmor(*a_actor);
		}
		__except (1) {
			return false;
		}
	}

	bool ConditionEvaluator::IsInInterior(RE::Actor* a_actor) {
		if (!a_actor) return false;
		auto cell = a_actor->GetParentCell();
		return cell && cell->IsInterior();
	}

	bool ConditionEvaluator::IsInPublicCell(RE::Actor* a_actor) {
		if (!a_actor) return false;
		auto cell = a_actor->GetParentCell();
		return cell && cell->GetOwner() == nullptr;
	}

	bool ConditionEvaluator::IsInOwnedCell(RE::Actor* a_actor) {
		if (!a_actor) return false;
		auto cell = a_actor->GetParentCell();
		return cell && cell->GetOwner() != nullptr;
	}

	bool ConditionEvaluator::IsCellOwner(RE::Actor* a_actor) {
		if (!a_actor) return false;
		auto cell = a_actor->GetParentCell();
		auto owner = cell ? cell->GetOwner() : nullptr;
		if (!owner) return false;
		if (owner->GetFormID() == a_actor->GetFormID()) return true;

		auto base = GetActorNPCBase(a_actor);
		if (base && owner->GetFormID() == base->GetFormID()) return true;

		auto faction = owner->As<RE::TESFaction>();
		return faction && a_actor->IsInFaction(faction);
	}

	bool ConditionEvaluator::IsNPCCellOwner(RE::Actor* a_actor) {
		if (!a_actor) return false;
		auto cell = a_actor->GetParentCell();
		auto owner = cell ? cell->GetOwner() : nullptr;
		auto ownerNpc = owner ? owner->As<RE::TESNPC>() : nullptr;
		auto base = GetActorNPCBase(a_actor);
		return ownerNpc && base && ownerNpc->GetFormID() == base->GetFormID();
	}

	bool ConditionEvaluator::IsInWater(RE::Actor* a_actor) {
		return a_actor && a_actor->IsInWater();
	}

	bool ConditionEvaluator::IsUnderwater(RE::Actor* a_actor) {
		return a_actor && a_actor->boolFlags.all(RE::Actor::BOOL_FLAGS::kUnderwater);
	}

	bool ConditionEvaluator::IsSwimming(RE::Actor* a_actor) { return a_actor && a_actor->ActorState::IsSwimming(); }
	bool ConditionEvaluator::IsInvisible(RE::Actor* a_actor) {
		auto av = RE::ActorValue::GetSingleton();
		return a_actor && av && av->invisibility && a_actor->GetActorValue(*av->invisibility) > 0.0f;
	}
	bool ConditionEvaluator::IsInVertibird(RE::Actor* a_actor) {
		return a_actor && (static_cast<bool>(a_actor->GetMountHandle()) ||
			a_actor->boolFlags.all(RE::Actor::BOOL_FLAGS::kIsGettingOnOffMount));
	}
	bool ConditionEvaluator::IsVisible(RE::Actor* a_actor) {
		return a_actor && a_actor->IsVisible();
	}
	bool ConditionEvaluator::IsFollowing(RE::Actor* a_actor) {
		return a_actor && a_actor->IsFollowing();
	}
	bool ConditionEvaluator::IsPathing(RE::Actor* a_actor) {
		return a_actor && a_actor->IsPathing();
	}
	bool ConditionEvaluator::IsPathingComplete(RE::Actor* a_actor) {
		return a_actor && a_actor->IsPathingComplete();
	}
	bool ConditionEvaluator::IsQuadruped(RE::Actor* a_actor) {
		return a_actor && a_actor->IsQuadruped();
	}
	bool ConditionEvaluator::IsFlying(RE::Actor* a_actor) {
		return a_actor && a_actor->flyState != 0;
	}
	bool ConditionEvaluator::IsFlightBlocked(RE::Actor* a_actor) {
		return a_actor && a_actor->flightBlocked != 0;
	}
	bool ConditionEvaluator::CanFly(RE::Actor* a_actor) {
		return a_actor && a_actor->allowFlying != 0;
	}
	bool ConditionEvaluator::IsForceRun(RE::Actor* a_actor) {
		return a_actor && a_actor->forceRun != 0;
	}
	bool ConditionEvaluator::IsForceSneak(RE::Actor* a_actor) {
		return a_actor && a_actor->forceSneak != 0;
	}
	bool ConditionEvaluator::IsHeadTracking(RE::Actor* a_actor) {
		return a_actor && a_actor->headTracking != 0;
	}
	bool ConditionEvaluator::WantsBlocking(RE::Actor* a_actor) {
		return a_actor && a_actor->wantBlocking != 0;
	}
	bool ConditionEvaluator::IsStaggered(RE::Actor* a_actor) {
		return a_actor && a_actor->staggered != 0;
	}
	bool ConditionEvaluator::IsInSyncAnim(RE::Actor* a_actor) {
		return a_actor && a_actor->inSyncAnim != 0;
	}
	bool ConditionEvaluator::IsReanimating(RE::Actor* a_actor) {
		return a_actor && a_actor->reanimating != 0;
	}
	bool ConditionEvaluator::IsScenePackage(RE::Actor* a_actor) {
		return a_actor && a_actor->boolFlags.all(RE::Actor::BOOL_FLAGS::kScenePackage);
	}
	bool ConditionEvaluator::IsInRandomScene(RE::Actor* a_actor) {
		return a_actor && a_actor->boolFlags.all(RE::Actor::BOOL_FLAGS::kInRandomScene);
	}
	bool ConditionEvaluator::CanSpeak(RE::Actor* a_actor) {
		return a_actor && a_actor->boolFlags.all(RE::Actor::BOOL_FLAGS::kCanSpeak);
	}
	bool ConditionEvaluator::CanDoFavor(RE::Actor* a_actor) {
		return a_actor && a_actor->boolFlags.all(RE::Actor::BOOL_FLAGS::kCanDoFavor);
	}
	bool ConditionEvaluator::CanSpeakToEssentialDown(RE::Actor* a_actor) {
		return a_actor && a_actor->boolFlags.all(RE::Actor::BOOL_FLAGS::kCanSpeakToEssentialDown);
	}
	bool ConditionEvaluator::IsAttackOnSight(RE::Actor* a_actor) {
		return a_actor && a_actor->boolFlags.all(RE::Actor::BOOL_FLAGS::kAttackOnSight);
	}
	bool ConditionEvaluator::IsAttackingDisabled(RE::Actor* a_actor) {
		return a_actor && a_actor->boolFlags.all(RE::Actor::BOOL_FLAGS::kAttackingDisabled);
	}
	bool ConditionEvaluator::IsCastingDisabled(RE::Actor* a_actor) {
		return a_actor && a_actor->boolFlags.all(RE::Actor::BOOL_FLAGS::kCastingDisabled);
	}
	bool ConditionEvaluator::IsMovementBlocked(RE::Actor* a_actor) {
		return a_actor && a_actor->boolFlags.all(RE::Actor::BOOL_FLAGS::kMovementBlocked);
	}
	bool ConditionEvaluator::DoNotShowOnStealthMeter(RE::Actor* a_actor) {
		return a_actor && a_actor->boolFlags.all(RE::Actor::BOOL_FLAGS::kDoNotShowOnStealthMeter);
	}
	bool ConditionEvaluator::IsInBleedoutAnimation(RE::Actor* a_actor) {
		return a_actor && a_actor->boolFlags.all(RE::Actor::BOOL_FLAGS::kInBleedoutAnimation);
	}
	bool ConditionEvaluator::IsGuard(RE::Actor* a_actor) {
		if (!a_actor) return false;
		auto manager = RE::BGSDefaultObjectManager::GetSingleton();
		auto guardFaction = manager ? manager->GetDefaultObject<RE::TESFaction>(RE::DEFAULT_OBJECT::kGuardFaction) : nullptr;
		return guardFaction && a_actor->IsInFaction(guardFaction);
	}
	bool ConditionEvaluator::IsInMerchantFaction(RE::Actor* a_actor) {
		if (!a_actor) return false;
		if (a_actor->vendorFaction) return true;
		auto base = GetActorNPCBase(a_actor);
		return ActorBaseFactionAny(base, [](RE::TESFaction* faction) {
			return FactionHasVendorData(faction);
		});
	}
	bool ConditionEvaluator::IsPlayerTeammate(RE::Actor* a_actor) {
		if (!a_actor || a_actor->IsPlayerRef()) return false;
		if (a_actor->IsFollowing() || IsCommanded(a_actor)) return true;

		auto manager = RE::BGSDefaultObjectManager::GetSingleton();
		auto playerFaction = manager ? manager->GetDefaultObject<RE::TESFaction>(RE::DEFAULT_OBJECT::kPlayerFaction) : nullptr;
		return playerFaction && a_actor->IsInFaction(playerFaction);
	}
	bool ConditionEvaluator::IsPlayerEnemy(RE::Actor* a_actor) {
		auto player = RE::PlayerCharacter::GetSingleton();
		return a_actor && player && a_actor != player && player->GetHostileToActor(a_actor);
	}
	bool ConditionEvaluator::KeyBindStateMatches(const std::string& a_key, const std::string& a_expression) {
		std::uint32_t state = 0;
		if (KeyBindStateManager::GetSingleton()->GetState(a_key, state)) {
			const auto expr = ToLowerCopy(TrimCopy(a_expression));
			if (expr == "down" || expr == "pressed" || expr == "true") return state != 0;
			if (expr == "up" || expr == "released" || expr == "false") return state == 0;

			std::string op;
			float target = 0.0f;
			if (!ParseGlobalComparison(a_expression.empty() ? ">0" : a_expression, op, target)) return false;
			return CompareFloat(static_cast<float>(state), op, target);
		}

		// Legacy configurations used a raw virtual-key expression in Keyword.
		// Preserve that behavior when no named IED-style keybind exists.
		const auto pressed = IsVirtualKeyComboDown(a_key.empty() ? "F8" : a_key);
		const auto expr = ToLowerCopy(TrimCopy(a_expression));
		if (expr == "down" || expr == "pressed" || expr == "true") return pressed;
		if (expr == "up" || expr == "released" || expr == "false") return !pressed;

		std::string op;
		float target = 0.0f;
		if (!ParseGlobalComparison(a_expression.empty() ? ">0" : a_expression, op, target)) return false;
		return CompareFloat(pressed ? 1.0f : 0.0f, op, target);
	}
	bool ConditionEvaluator::PlayerEnemiesNearby(RE::Actor* a_actor, const std::string& a_radius) {
		(void)a_actor;
		auto player = RE::PlayerCharacter::GetSingleton();
		if (!player) return false;
		auto cell = player->GetParentCell();
		if (!cell) return false;

		const auto radius = std::clamp(ParseFloatParam(a_radius, 4096.0f), 128.0f, 65536.0f);
		const auto origin = player->GetPosition();
		bool found = false;

		cell->ForEachReferenceInRange(origin, radius, [&](RE::TESObjectREFR* a_ref) {
			auto other = a_ref ? a_ref->As<RE::Actor>() : nullptr;
			if (!other || other == player || other->IsDead(false)) {
				return RE::BSContainer::ForEachResult::kContinue;
			}
			if (player->GetHostileToActor(other) || other->GetHostileToActor(player)) {
				found = true;
				return RE::BSContainer::ForEachResult::kStop;
			}
			return RE::BSContainer::ForEachResult::kContinue;
		});

		return found;
	}
	bool ConditionEvaluator::InPlayerEnemyFaction(RE::Actor* a_actor) {
		auto player = RE::PlayerCharacter::GetSingleton();
		return a_actor && player && a_actor != player && (player->GetHostileToActor(a_actor) || a_actor->GetHostileToActor(player));
	}
	bool ConditionEvaluator::IsLayingDown(RE::Actor* a_actor) {
		if (!a_actor) return false;
		const auto state = a_actor->DoGetSitSleepState();
		return state == RE::SIT_SLEEP_STATE::kWantToSleep ||
			state == RE::SIT_SLEEP_STATE::kWaitingForSleepAnim ||
			state == RE::SIT_SLEEP_STATE::kIsSleeping ||
			state == RE::SIT_SLEEP_STATE::kWantToWake;
	}
	bool ConditionEvaluator::HasHumanoidSkeleton(RE::Actor* a_actor) {
		if (!a_actor) return false;
		const auto skeletonPath = GetActorSkeletonPath(a_actor);
		if (ContainsInsensitive(skeletonPath, "actors\\character\\characterassets") ||
			ContainsInsensitive(skeletonPath, "actors/character/characterassets") ||
			ContainsInsensitive(skeletonPath, "characterassets\\skeleton") ||
			ContainsInsensitive(skeletonPath, "characterassets/skeleton")) {
			return true;
		}
		return ActorHasAll3DNodes(a_actor, { "COM", "Pelvis", "Spine1", "LArm_Hand", "RArm_Hand" });
	}
	bool ConditionEvaluator::SkeletonPathContains(RE::Actor* a_actor, const std::string& a_text) {
		return ContainsInsensitive(GetActorSkeletonPath(a_actor), a_text);
	}

	bool ConditionEvaluator::IsInAir(RE::Actor* a_actor) {
		return a_actor ? a_actor->IsJumping() : false;
	}

	bool ConditionEvaluator::IsAiming(RE::Actor* a_actor) {
		if (!a_actor) return false;
		return a_actor->gunState == RE::GUN_STATE::kSighted || a_actor->gunState == RE::GUN_STATE::kFireSighted;
	}

	bool ConditionEvaluator::IsCrafting(RE::Actor* a_actor) {
		return a_actor ? a_actor->interactingState == RE::INTERACTING_STATE::kInteracting : false;
	}

	bool ConditionEvaluator::IsInDialogue(RE::Actor* a_actor) {
		return a_actor ? a_actor->talkingToPlayer == 1 : false;
	}

	bool ConditionEvaluator::IsPlayer(RE::Actor* a_actor) {
		return a_actor && a_actor->IsPlayerRef();
	}

	// 👇 终极修复：直接通过底层结构 data.objectReference 拿模型基底
	bool ConditionEvaluator::IsFemale(RE::Actor* a_actor) {
		if (!a_actor || !a_actor->data.objectReference) return false;
		auto base = a_actor->data.objectReference->As<RE::TESNPC>();
		return base && base->GetSex() == RE::SEX::kFemale;
	}

	bool ConditionEvaluator::IsDead(RE::Actor* a_actor) {
		return a_actor && a_actor->IsDead(false);
	}

	bool ConditionEvaluator::IsChild(RE::Actor* a_actor) {
		return a_actor && a_actor->IsChild();
	}

	bool ConditionEvaluator::IsUnconscious(RE::Actor* a_actor) {
		return a_actor && a_actor->lifeState == static_cast<std::uint32_t>(RE::ACTOR_LIFE_STATE::kUnconscious);
	}

	bool ConditionEvaluator::IsRestrained(RE::Actor* a_actor) {
		return a_actor && a_actor->lifeState == static_cast<std::uint32_t>(RE::ACTOR_LIFE_STATE::kRestrained);
	}

	bool ConditionEvaluator::IsTrespassing(RE::Actor* a_actor) {
		return a_actor && a_actor->boolFlags.all(RE::Actor::BOOL_FLAGS::kIsTresspassing);
	}

	bool ConditionEvaluator::IsInKillmove(RE::Actor* a_actor) {
		return a_actor && a_actor->boolFlags.all(RE::Actor::BOOL_FLAGS::kIsInKillMove);
	}

	bool ConditionEvaluator::IsBribedByPlayer(RE::Actor* a_actor) {
		return a_actor && !a_actor->IsPlayerRef() && a_actor->boolFlags.all(RE::Actor::BOOL_FLAGS::kBribedByPlayer);
	}

	bool ConditionEvaluator::IsAngryWithPlayer(RE::Actor* a_actor) {
		return a_actor && !a_actor->IsPlayerRef() && a_actor->boolFlags.all(RE::Actor::BOOL_FLAGS::kAngryWithPlayer);
	}

	bool ConditionEvaluator::IsEssential(RE::Actor* a_actor) {
		if (!a_actor) return false;
		if (a_actor->boolFlags.all(RE::Actor::BOOL_FLAGS::kEssential)) return true;
		auto base = GetActorNPCBase(a_actor);
		return base && base->IsEssential();
	}

	bool ConditionEvaluator::IsProtected(RE::Actor* a_actor) {
		if (!a_actor) return false;
		if (a_actor->boolFlags.all(RE::Actor::BOOL_FLAGS::kProtected)) return true;
		auto base = GetActorNPCBase(a_actor);
		return base && base->IsProtected();
	}

	bool ConditionEvaluator::IsUnique(RE::Actor* a_actor) {
		auto base = GetActorNPCBase(a_actor);
		return base && base->IsUnique();
	}

	bool ConditionEvaluator::IsSummonable(RE::Actor* a_actor) {
		auto base = GetActorNPCBase(a_actor);
		return base && base->IsSummonable();
	}

	bool ConditionEvaluator::IsInvulnerable(RE::Actor* a_actor) {
		auto base = GetActorNPCBase(a_actor);
		return base && base->IsInvulnerable();
	}

	bool ConditionEvaluator::IsCommanded(RE::Actor* a_actor) {
		return a_actor && a_actor->boolFlags.all(RE::Actor::BOOL_FLAGS::kIsCommandedActor);
	}

	bool ConditionEvaluator::IsParalyzed(RE::Actor* a_actor) {
		if (!a_actor) return false;
		auto* av = RE::ActorValue::GetSingleton();
		return av && av->paralysis && a_actor->GetActorValue(*av->paralysis) > 0.0f;
	}

	bool ConditionEvaluator::IsWaitingForPlayer(RE::Actor* a_actor) {
		if (!a_actor) return false;
		auto* av = RE::ActorValue::GetSingleton();
		return av && av->waitingForPlayer && a_actor->GetActorValue(*av->waitingForPlayer) == 1.0f;
	}

	bool ConditionEvaluator::ActorMatchesFormID(RE::Actor* a_actor, std::uint32_t a_formID) {
		return a_actor && a_formID != 0 && a_actor->GetFormID() == a_formID;
	}

	bool ConditionEvaluator::ActorBaseMatchesFormID(RE::Actor* a_actor, std::uint32_t a_formID) {
		if (!a_actor || !a_actor->data.objectReference || a_formID == 0) return false;
		auto base = a_actor->data.objectReference->As<RE::TESNPC>();
		return base && base->GetFormID() == a_formID;
	}

	bool ConditionEvaluator::ActorRaceMatchesFormID(RE::Actor* a_actor, std::uint32_t a_formID) {
		if (!a_actor || !a_actor->data.objectReference || a_formID == 0) return false;
		auto base = a_actor->data.objectReference->As<RE::TESNPC>();
		auto race = base ? base->GetFormRace() : nullptr;
		return race && race->GetFormID() == a_formID;
	}

	bool ConditionEvaluator::ActorCombatStyleMatchesFormID(RE::Actor* a_actor, std::uint32_t a_formID) {
		if (!a_actor || a_formID == 0) return false;
		if (auto combatStyle = a_actor->GetCombatStyle(); combatStyle && combatStyle->GetFormID() == a_formID) {
			return true;
		}
		auto base = GetActorNPCBase(a_actor);
		return base && base->combatStyle && base->combatStyle->GetFormID() == a_formID;
	}

	bool ConditionEvaluator::ActorClassMatchesFormID(RE::Actor* a_actor, std::uint32_t a_formID) {
		auto base = GetActorNPCBase(a_actor);
		return base && a_formID != 0 && base->cl && base->cl->GetFormID() == a_formID;
	}

	bool ConditionEvaluator::ActorInFaction(RE::Actor* a_actor, std::uint32_t a_formID) {
		if (!a_actor || a_formID == 0) return false;
		auto faction = RE::TESForm::GetFormByID<RE::TESFaction>(a_formID);
		return faction && a_actor->IsInFaction(faction);
	}

	bool ConditionEvaluator::ActorInCell(RE::Actor* a_actor, std::uint32_t a_formID) {
		if (!a_actor || a_formID == 0) return false;
		auto cell = a_actor->GetParentCell();
		return cell && cell->GetFormID() == a_formID;
	}

	bool ConditionEvaluator::ActorInLocation(RE::Actor* a_actor, std::uint32_t a_formID, bool a_includeChildLocations) {
		if (!a_actor || a_formID == 0) return false;
		auto targetLocation = RE::TESForm::GetFormByID<RE::BGSLocation>(a_formID);
		if (!targetLocation) return false;

		auto currentLocation = a_actor->GetCurrentLocation();
		if (!currentLocation) {
			auto cell = a_actor->GetParentCell();
			currentLocation = cell ? cell->GetLocation() : nullptr;
		}
		if (!currentLocation) return false;
		if (currentLocation == targetLocation || currentLocation->GetFormID() == a_formID) return true;
		return a_includeChildLocations && targetLocation->IsChild(currentLocation);
	}

	bool ConditionEvaluator::ActorInWorldspace(RE::Actor* a_actor, std::uint32_t a_formID) {
		if (!a_actor || a_formID == 0) return false;
		auto cell = a_actor->GetParentCell();
		auto worldspace = cell ? cell->worldSpace : nullptr;
		return worldspace && worldspace->GetFormID() == a_formID;
	}

	bool ConditionEvaluator::GlobalValueMatches(std::uint32_t a_formID, const std::string& a_expression) {
		if (a_formID == 0) return false;
		auto global = RE::TESForm::GetFormByID<RE::TESGlobal>(a_formID);
		if (!global) return false;
		std::string op;
		float target = 0.0f;
		if (!ParseGlobalComparison(a_expression, op, target)) return false;
		return CompareFloat(global->GetValue(), op, target);
	}

	bool ConditionEvaluator::ActorLevelMatches(RE::Actor* a_actor, const std::string& a_expression) {
		if (!a_actor) return false;
		std::string op;
		float target = 0.0f;
		if (!ParseGlobalComparison(a_expression, op, target)) return false;
		return CompareFloat(static_cast<float>(a_actor->GetLevel()), op, target);
	}

	bool ConditionEvaluator::ActorValueMatches(RE::Actor* a_actor, std::uint32_t a_formID, const std::string& a_expression) {
		if (!a_actor || a_formID == 0) return false;
		auto actorValue = RE::TESForm::GetFormByID<RE::ActorValueInfo>(a_formID);
		if (!actorValue) return false;
		std::string op;
		float target = 0.0f;
		if (!ParseGlobalComparison(a_expression, op, target)) return false;
		return CompareFloat(a_actor->GetActorValue(*actorValue), op, target);
	}

	bool ConditionEvaluator::ActorPerkRankMatches(RE::Actor* a_actor, std::uint32_t a_formID, const std::string& a_expression) {
		if (!a_actor || a_formID == 0) return false;
		auto perk = RE::TESForm::GetFormByID<RE::BGSPerk>(a_formID);
		if (!perk) return false;
		const auto expression = a_expression.empty() ? std::string(">=1") : a_expression;
		std::string op;
		float target = 0.0f;
		if (!ParseGlobalComparison(expression, op, target)) return false;
		return CompareFloat(static_cast<float>(a_actor->GetPerkRank(perk)), op, target);
	}

	bool ConditionEvaluator::RandomPercentMatches(RE::Actor* a_actor, const ActiveItem* a_candidateItem, float a_percent, const std::string& a_seed) {
		if (a_percent >= 100.0f) return true;
		if (a_percent <= 0.0f) return false;

		std::uint64_t hash = 1469598103934665603ull;
		HashCombine(hash, a_actor ? a_actor->GetFormID() : 0);
		HashCombine(hash, a_candidateItem && a_candidateItem->object ? a_candidateItem->object->GetFormID() : 0);
		HashCombine(hash, a_candidateItem ? a_candidateItem->uid : 0);
		HashCombine(hash, StableStringHash(a_seed));

		const float roll = static_cast<float>(hash % 10000) / 100.0f;
		return roll < a_percent;
	}

	bool ConditionEvaluator::DayOfWeekMatches(std::uint32_t a_day) {
		auto calendar = RE::Calendar::GetSingleton();
		if (!calendar) return false;
		return (calendar->midnightsPassed % 7u) == (a_day % 7u);
	}

	bool ConditionEvaluator::ActorLifeStateMatches(RE::Actor* a_actor, const std::string& a_state) {
		if (!a_actor) return false;
		std::uint32_t targetState = 0;
		if (!ParseLifeStateParam(a_state, targetState)) return false;
		return a_actor->lifeState == targetState;
	}

	bool ConditionEvaluator::NodeMonitor(RE::Actor* a_actor, const std::string& a_nodeName, const std::string& a_mode) {
		if (!a_actor || a_nodeName.empty()) return false;

		enum class TestType {
			kObject,
			kNode,
			kGeometry,
			kNodeWithGeometryChild
		};

		const auto mode = ToLowerCopy(TrimCopy(a_mode));
		const bool recursive = mode.find("recursive") != std::string::npos;
		const bool includeInvisible = mode.find("includeinvisible") != std::string::npos;
		TestType testType = TestType::kObject;
		if (mode.find("nodewithgeometrychild") != std::string::npos) {
			testType = TestType::kNodeWithGeometryChild;
		}
		else if (mode.find("geometry") != std::string::npos) {
			testType = TestType::kGeometry;
		}
		else if (mode.find("node") != std::string::npos) {
			testType = TestType::kNode;
		}

		auto* root = a_actor->Get3D(false);
		if (!root) return false;

		auto isVisible = [&](RE::NiAVObject* object) {
			return object && (includeInvisible || (!object->GetAppCulled() && object->local.scale != 0.0f));
		};
		auto hasGeometry = [&](const auto& self, RE::NiAVObject* object) -> bool {
			if (!object) return false;
			if (object->IsGeometry() && isVisible(object)) return true;
			auto* node = object->IsNode();
			if (!node) return false;
			for (auto& child : node->children) {
				if (!child) continue;
				if (recursive ? self(self, child.get()) : (child->IsGeometry() && isVisible(child.get()))) return true;
			}
			return false;
		};

		for (const auto& candidate : BuildManagedNodeNameCandidates(a_nodeName)) {
			RE::BSFixedString name(candidate.c_str());
			auto* object = root->GetObjectByName(name);
			if (!isVisible(object)) continue;

			switch (testType) {
			case TestType::kObject:
				return true;
			case TestType::kNode:
				if (object->IsNode()) return true;
				break;
			case TestType::kGeometry:
				if (object->IsGeometry()) return true;
				break;
			case TestType::kNodeWithGeometryChild:
				if (object->IsNode() && hasGeometry(hasGeometry, object)) return true;
				break;
			}
		}
		return false;
	}

	bool ConditionEvaluator::IsSunAboveHorizon() {
		return IsSkyDaytime(RE::Sky::GetSingleton());
	}

	bool ConditionEvaluator::TimeOfDayPhaseMatches(const std::string& a_phase) {
		return MatchTimeOfDayPhase(GetTimeOfDayPhase(RE::Sky::GetSingleton()), a_phase);
	}

	bool ConditionEvaluator::CurrentWeatherMatchesFormID(std::uint32_t a_formID) {
		auto weather = GetCurrentWeather();
		return weather && a_formID != 0 && weather->GetFormID() == a_formID;
	}

	bool ConditionEvaluator::WeatherClassMatches(const std::string& a_weatherClass) {
		auto weather = GetCurrentWeather();
		if (!weather) return false;
		const auto flags = GetWeatherDataFlags(weather);
		const auto value = ToLowerCopy(a_weatherClass);
		constexpr std::uint8_t pleasant = static_cast<std::uint8_t>(RE::TESWeather::WeatherDataFlags::kPleasant);
		constexpr std::uint8_t cloudy = static_cast<std::uint8_t>(RE::TESWeather::WeatherDataFlags::kCloudy);
		constexpr std::uint8_t rainy = static_cast<std::uint8_t>(RE::TESWeather::WeatherDataFlags::kRainy);
		constexpr std::uint8_t snow = static_cast<std::uint8_t>(RE::TESWeather::WeatherDataFlags::kSnow);
		if (value == "all" || value == "any") return (flags & (pleasant | cloudy | rainy | snow)) != 0;
		if (value == "pleasant" || value == "clear") return (flags & pleasant) != 0;
		if (value == "cloudy" || value == "cloud") return (flags & cloudy) != 0;
		if (value == "rainy" || value == "rain") return (flags & rainy) != 0;
		if (value == "snow" || value == "snowy") return (flags & snow) != 0;
		if (value == "precipitation" || value == "precip") return (flags & (rainy | snow)) != 0;
		return false;
	}

	bool ConditionEvaluator::LightingTemplateMatchesFormID(RE::Actor* a_actor, std::uint32_t a_formID) {
		if (!a_actor || a_formID == 0) return false;
		auto cell = a_actor->GetParentCell();
		if (!cell) return false;
		if (cell->lightingTemplate && cell->lightingTemplate->GetFormID() == a_formID) return true;
		auto worldspace = cell->worldSpace;
		return worldspace && worldspace->lightingTemplate && worldspace->lightingTemplate->GetFormID() == a_formID;
	}

	bool ConditionEvaluator::QuestStageMatches(std::uint32_t a_formID, const std::string& a_expression) {
		if (a_formID == 0) return false;
		auto* quest = RE::TESForm::GetFormByID<RE::TESQuest>(a_formID);
		if (!quest) return false;

		std::string op;
		float target = 0.0f;
		if (!ParseGlobalComparison(a_expression.empty() ? ">=1" : a_expression, op, target)) return false;
		return CompareFloat(static_cast<float>(quest->currentStage), op, target);
	}

	bool ConditionEvaluator::HasActiveEffect(RE::Actor* a_actor, std::uint32_t a_formID) {
		if (!a_actor || a_formID == 0) return false;
		auto* effects = a_actor->GetActiveEffectList();
		if (!effects) return false;

		for (const auto& effectPtr : effects->data) {
			auto* active = effectPtr.get();
			if (!active ||
				active->flags.any(RE::ActiveEffect::Flags::kInactive) ||
				active->flags.any(RE::ActiveEffect::Flags::kRemovedEffects) ||
				active->flags.any(RE::ActiveEffect::Flags::kDispelled) ||
				active->flags.any(RE::ActiveEffect::Flags::kWornOff)) {
				continue;
			}

			if (active->spell && active->spell->GetFormID() == a_formID) return true;
			if (active->source && active->source->GetFormID() == a_formID) return true;
		}
		return false;
	}

	bool ConditionEvaluator::HasMagicEffect(RE::Actor* a_actor, std::uint32_t a_formID) {
		if (!a_actor || a_formID == 0) return false;
		auto* effects = a_actor->GetActiveEffectList();
		if (!effects) return false;

		for (const auto& effectPtr : effects->data) {
			auto* active = effectPtr.get();
			if (!active ||
				active->flags.any(RE::ActiveEffect::Flags::kInactive) ||
				active->flags.any(RE::ActiveEffect::Flags::kRemovedEffects) ||
				active->flags.any(RE::ActiveEffect::Flags::kDispelled) ||
				active->flags.any(RE::ActiveEffect::Flags::kWornOff)) {
				continue;
			}

			if (active->effect && active->effect->effectSetting && active->effect->effectSetting->GetFormID() == a_formID) return true;
		}
		return false;
	}

	bool ConditionEvaluator::HasSpell(RE::Actor* a_actor, std::uint32_t a_formID) {
		if (!a_actor || a_formID == 0) return false;
		auto* effects = a_actor->GetActiveEffectList();
		if (!effects) return false;

		for (const auto& effectPtr : effects->data) {
			auto* active = effectPtr.get();
			if (!active ||
				active->flags.any(RE::ActiveEffect::Flags::kInactive) ||
				active->flags.any(RE::ActiveEffect::Flags::kRemovedEffects) ||
				active->flags.any(RE::ActiveEffect::Flags::kDispelled) ||
				active->flags.any(RE::ActiveEffect::Flags::kWornOff)) {
				continue;
			}

			if (active->spell && active->spell->GetFormID() == a_formID) return true;
		}
		return false;
	}

	bool ConditionEvaluator::IsInDarkArea(RE::Actor* a_actor) {
		if (!a_actor) return false;
		auto cell = a_actor->GetParentCell();
		if (cell && cell->IsInterior()) {
			const auto level = GetInteriorAmbientLightLevel(a_actor);
			return level >= 0.0f && level < kInteriorAmbientLightThreshold;
		}
		return IsExteriorDark();
	}

	bool ConditionEvaluator::IsInDarkness(RE::Actor* a_actor) {
		return IsInDarkArea(a_actor);
	}

	bool ConditionEvaluator::InteriorAmbientLightLevelMatches(RE::Actor* a_actor, const std::string& a_expression) {
		const auto level = GetInteriorAmbientLightLevel(a_actor);
		if (level < 0.0f) return false;

		std::string op;
		float target = 0.0f;
		if (!ParseGlobalComparison(a_expression.empty() ? "<0.425" : a_expression, op, target)) return false;
		return CompareFloat(level, op, target);
	}

	bool ConditionEvaluator::SunAngleMatches(const std::string& a_expression, const std::string& a_flags) {
		auto angle = GetSunAngleRadians();
		const auto flags = ToLowerCopy(a_flags);
		if (flags == "abs" || flags == "absolute") {
			angle = std::fabs(angle);
		}

		std::string op;
		float target = 0.0f;
		if (!ParseGlobalComparison(a_expression.empty() ? "<1.5708" : a_expression, op, target)) return false;
		return CompareFloat(angle, op, target);
	}

	bool ConditionEvaluator::IsBipedSlotOccupied(RE::Actor* a_actor, std::uint32_t a_slot) {
		if (!a_actor) return false;
		if (a_slot >= 30 && a_slot <= 61) {
			int bipedIndex = a_slot - 30;
			auto biped = a_actor->GetBiped(false);
			if (biped && biped->object[bipedIndex].parent.object != nullptr) return true;
		}
		return false;
	}

	bool ConditionEvaluator::InventoryItemCountMatches(RE::Actor* a_actor, std::uint32_t a_formID, const std::string& a_expression) {
		if (!a_actor || a_formID == 0) return false;
		std::string op;
		float target = 0.0f;
		if (!ParseGlobalComparison(a_expression, op, target)) return false;
		return CompareFloat(static_cast<float>(Scanner::GetItemCount(a_actor, a_formID)), op, target);
	}

	bool ConditionEvaluator::CandidateStackCountMatches(const ActiveItem* a_candidateItem, const std::string& a_expression) {
		if (!a_candidateItem) return false;
		std::string op;
		float target = 0.0f;
		if (!ParseGlobalComparison(a_expression, op, target)) return false;
		return CompareFloat(static_cast<float>(a_candidateItem->count), op, target);
	}

	bool ConditionEvaluator::CandidateInventoryCountMatches(RE::Actor* a_actor, const ActiveItem* a_candidateItem, const std::string& a_expression) {
		if (!a_actor || !a_candidateItem || !a_candidateItem->object) return false;
		std::string op;
		float target = 0.0f;
		if (!ParseGlobalComparison(a_expression, op, target)) return false;
		const auto totalCount = Scanner::GetItemCount(a_actor, a_candidateItem->object->GetFormID());
		return CompareFloat(static_cast<float>(totalCount), op, target);
	}

	// 👇 终极修复：直接通过底层结构 data.objectReference 拿模型基底
	bool ConditionEvaluator::ActorHasKeyword(RE::Actor* a_actor, const char* a_keywordEditorID) {
		if (!a_actor || !a_actor->data.objectReference) return false;
		auto kwdForm = a_actor->data.objectReference->As<RE::BGSKeywordForm>();
		return kwdForm ? kwdForm->HasKeywordString(a_keywordEditorID) : false;
	}

	bool ConditionEvaluator::HasKeywordEquipped(RE::Actor* a_actor, const char* a_keywordEditorID) {
		if (!a_actor || !a_actor->inventoryList) return false;
		for (auto& invItem : a_actor->inventoryList->data) {
			bool isEquipped = false;
			auto stack = invItem.stackData.get();
			while (stack) { if (stack->IsEquipped()) isEquipped = true; stack = stack->nextStack.get(); }

			if (isEquipped) {
				auto kwdForm = invItem.object->As<RE::BGSKeywordForm>();
				if (kwdForm && kwdForm->HasKeywordString(a_keywordEditorID)) return true;
			}
		}
		return false;
	}

	bool ConditionEvaluator::HasEquippedFormID(RE::Actor* a_actor, std::uint32_t a_formID, std::uint32_t a_omodID) {
		if (!a_actor || !a_actor->inventoryList) return false;
		RE::BGSMod::Attachment::Mod* requiredMod = nullptr;
		if (a_omodID != 0) {
			requiredMod = RE::TESForm::GetFormByID<RE::BGSMod::Attachment::Mod>(a_omodID);
			if (!requiredMod) return false;
		}

		for (auto& invItem : a_actor->inventoryList->data) {
			if (invItem.object && invItem.object->GetFormID() == a_formID) {
				auto stack = invItem.stackData.get();
				while (stack) {
					if (stack->IsEquipped()) {
						if (!requiredMod) return true;
						if (stack->extra) {
							auto* instExtra = stack->extra->GetByType<RE::BGSObjectInstanceExtra>();
							if (instExtra && instExtra->HasMod(*requiredMod)) {
								return true;
							}
						}
					}
					stack = stack->nextStack.get();
				}
			}
		}
		return false;
	}

	bool ConditionEvaluator::HasConditionRules(const ConditionNode& a_node)
	{
		if (!a_node.isGroup) return !a_node.type.empty();
		for (const auto& child : a_node.children) {
			if (HasConditionRules(child)) return true;
		}
		return false;
	}

	bool ConditionEvaluator::EvaluateConditionTree(RE::Actor* a_actor, const ConditionNode& node)
	{
		return EvaluateConditionTree(a_actor, node, nullptr);
	}

	bool ConditionEvaluator::EvaluateConditionTree(RE::Actor* a_actor, const ConditionNode& node, const ActiveItem* a_candidateItem)
	{
		if (node.isGroup) {
			if (node.children.empty()) return true;
			bool result = node.isAnd ? true : false;
			for (const auto& child : node.children) {
				bool childRes = EvaluateConditionTree(a_actor, child, a_candidateItem);
				if (node.isAnd) { result = result && childRes; if (!result) break; }
				else { result = result || childRes; if (result) break; }
			}
			return node.isNot ? !result : result;
		}
		else {
			bool result = false;
			if (IsSkyrimOnlyConditionType(node.type)) {
				return node.isNot ? (false != node.expected) : (false == node.expected);
			}

			if (node.type == "HasActiveEffect") {
				result = HasActiveEffect(a_actor, ParseFormIDParam(node.keyword));
				return node.isNot ? (result != node.expected) : (result == node.expected);
			}
			if (node.type == "HasMagicEffect") {
				result = HasMagicEffect(a_actor, ParseFormIDParam(node.keyword));
				return node.isNot ? (result != node.expected) : (result == node.expected);
			}
			if (node.type == "HasSpell") {
				result = HasSpell(a_actor, ParseFormIDParam(node.keyword));
				return node.isNot ? (result != node.expected) : (result == node.expected);
			}
			if (node.type == "QuestStage") {
				result = QuestStageMatches(ParseFormIDParam(node.keyword), node.keyword2);
				return node.isNot ? (result != node.expected) : (result == node.expected);
			}
			if (node.type == "InDarkArea") {
				result = IsInDarkArea(a_actor);
				return node.isNot ? (result != node.expected) : (result == node.expected);
			}
			if (node.type == "InDarkness") {
				result = IsInDarkness(a_actor);
				return node.isNot ? (result != node.expected) : (result == node.expected);
			}
			if (node.type == "InteriorAmbientLightLevel") {
				result = InteriorAmbientLightLevelMatches(a_actor, node.keyword);
				return node.isNot ? (result != node.expected) : (result == node.expected);
			}
			if (node.type == "SunAngle") {
				result = SunAngleMatches(node.keyword, node.keyword2);
				return node.isNot ? (result != node.expected) : (result == node.expected);
			}
			if (node.type == "IsPlayerTeammate") {
				result = IsPlayerTeammate(a_actor);
				return node.isNot ? (result != node.expected) : (result == node.expected);
			}
			if (node.type == "IsPlayerEnemy") {
				result = IsPlayerEnemy(a_actor);
				return node.isNot ? (result != node.expected) : (result == node.expected);
			}
			if (node.type == "KeyBindState") {
				result = KeyBindStateMatches(node.keyword, node.keyword2);
				return node.isNot ? (result != node.expected) : (result == node.expected);
			}
			if (node.type == "PlayerEnemiesNearby") {
				result = PlayerEnemiesNearby(a_actor, node.keyword);
				return node.isNot ? (result != node.expected) : (result == node.expected);
			}
			if (node.type == "InPlayerEnemyFaction") {
				result = InPlayerEnemyFaction(a_actor);
				return node.isNot ? (result != node.expected) : (result == node.expected);
			}
			if (node.type == "IsLayingDown") {
				result = IsLayingDown(a_actor);
				return node.isNot ? (result != node.expected) : (result == node.expected);
			}
			if (node.type == "HumanoidSkeleton") {
				result = HasHumanoidSkeleton(a_actor);
				return node.isNot ? (result != node.expected) : (result == node.expected);
			}
			if (node.type == "SkeletonPathContains") {
				result = SkeletonPathContains(a_actor, node.keyword);
				return node.isNot ? (result != node.expected) : (result == node.expected);
			}

			if (node.type == "IsSneaking") result = IsSneaking(a_actor);
			else if (node.type == "IsSprinting") result = IsSprinting(a_actor);
			else if (node.type == "IsSitting") result = IsSitting(a_actor);
			else if (node.type == "IsSleeping") result = IsSleeping(a_actor);
			else if (node.type == "IsInCombat") result = IsInCombat(a_actor);
			else if (node.type == "IsWeaponDrawn") result = IsWeaponDrawn(a_actor);
			else if (node.type == "IsWeaponDrawing") result = IsWeaponDrawing(a_actor);
			else if (node.type == "IsWeaponDrawnStrict") result = IsWeaponDrawnStrict(a_actor);
			else if (node.type == "IsWeaponSheathing") result = IsWeaponSheathing(a_actor);
			else if (node.type == "IsWeaponSheathed") result = IsWeaponSheathed(a_actor);
			else if (node.type == "IsDrawn_Pistol") result = IsDrawnPistol(a_actor);
			else if (node.type == "IsDrawn_Rifle") result = IsDrawnRifle(a_actor);
			else if (node.type == "IsDrawn_Melee") result = IsDrawnMelee(a_actor);
			else if (node.type == "IsInPowerArmor") result = IsInPowerArmor(a_actor);
			else if (node.type == "IsInInterior") result = IsInInterior(a_actor);
			else if (node.type == "InPublicCell") result = IsInPublicCell(a_actor);
			else if (node.type == "InOwnedCell") result = IsInOwnedCell(a_actor);
			else if (node.type == "IsCellOwner") result = IsCellOwner(a_actor);
			else if (node.type == "IsNPCCellOwner") result = IsNPCCellOwner(a_actor);
			else if (node.type == "IsInWater") result = IsInWater(a_actor);
			else if (node.type == "IsUnderwater") result = IsUnderwater(a_actor);
			else if (node.type == "IsUsingPipboy") result = IsUsingPipboy(a_actor);
			else if (node.type == "IsSwimming") result = IsSwimming(a_actor);
			else if (node.type == "IsInvisible") result = IsInvisible(a_actor);
			else if (node.type == "IsCrafting") result = IsCrafting(a_actor);
			else if (node.type == "IsInDialogue") result = IsInDialogue(a_actor);
			else if (node.type == "IsInVertibird") result = IsInVertibird(a_actor);
			else if (node.type == "IsVisible") result = IsVisible(a_actor);
			else if (node.type == "IsFollowing") result = IsFollowing(a_actor);
			else if (node.type == "IsPathing") result = IsPathing(a_actor);
			else if (node.type == "IsPathingComplete") result = IsPathingComplete(a_actor);
			else if (node.type == "IsQuadruped") result = IsQuadruped(a_actor);
			else if (node.type == "IsFlying") result = IsFlying(a_actor);
			else if (node.type == "IsFlightBlocked") result = IsFlightBlocked(a_actor);
			else if (node.type == "CanFly") result = CanFly(a_actor);
			else if (node.type == "IsForceRun") result = IsForceRun(a_actor);
			else if (node.type == "IsForceSneak") result = IsForceSneak(a_actor);
			else if (node.type == "IsHeadTracking") result = IsHeadTracking(a_actor);
			else if (node.type == "WantsBlocking") result = WantsBlocking(a_actor);
			else if (node.type == "IsStaggered") result = IsStaggered(a_actor);
			else if (node.type == "IsInSyncAnim") result = IsInSyncAnim(a_actor);
			else if (node.type == "IsReanimating") result = IsReanimating(a_actor);
			else if (node.type == "IsScenePackage") result = IsScenePackage(a_actor);
			else if (node.type == "IsInRandomScene") result = IsInRandomScene(a_actor);
			else if (node.type == "CanSpeak") result = CanSpeak(a_actor);
			else if (node.type == "CanDoFavor") result = CanDoFavor(a_actor);
			else if (node.type == "CanSpeakToEssentialDown") result = CanSpeakToEssentialDown(a_actor);
			else if (node.type == "IsAttackOnSight") result = IsAttackOnSight(a_actor);
			else if (node.type == "IsAttackingDisabled") result = IsAttackingDisabled(a_actor);
			else if (node.type == "IsCastingDisabled") result = IsCastingDisabled(a_actor);
			else if (node.type == "IsMovementBlocked") result = IsMovementBlocked(a_actor);
			else if (node.type == "DoNotShowOnStealthMeter") result = DoNotShowOnStealthMeter(a_actor);
			else if (node.type == "IsInBleedoutAnimation") result = IsInBleedoutAnimation(a_actor);
			else if (node.type == "IsGuard") result = IsGuard(a_actor);
			else if (node.type == "InMerchantFaction") result = IsInMerchantFaction(a_actor);
			else if (node.type == "IsPlayer") result = IsPlayer(a_actor);
			else if (node.type == "IsFemale") result = IsFemale(a_actor);
			else if (node.type == "IsDead") result = IsDead(a_actor);
			else if (node.type == "IsChild") result = IsChild(a_actor);
			else if (node.type == "IsUnconscious") result = IsUnconscious(a_actor);
			else if (node.type == "IsRestrained") result = IsRestrained(a_actor);
			else if (node.type == "IsTrespassing") result = IsTrespassing(a_actor);
			else if (node.type == "IsInKillmove") result = IsInKillmove(a_actor);
			else if (node.type == "IsBribedByPlayer") result = IsBribedByPlayer(a_actor);
			else if (node.type == "IsAngryWithPlayer") result = IsAngryWithPlayer(a_actor);
			else if (node.type == "IsEssential") result = IsEssential(a_actor);
			else if (node.type == "IsProtected") result = IsProtected(a_actor);
			else if (node.type == "IsUnique") result = IsUnique(a_actor);
			else if (node.type == "IsSummonable") result = IsSummonable(a_actor);
			else if (node.type == "IsInvulnerable") result = IsInvulnerable(a_actor);
			else if (node.type == "IsCommanded") result = IsCommanded(a_actor);
			else if (node.type == "IsParalyzed") result = IsParalyzed(a_actor);
			else if (node.type == "IsWaitingForPlayer") result = IsWaitingForPlayer(a_actor);
			else if (node.type == "ActorFormID") result = ActorMatchesFormID(a_actor, ParseFormIDParam(node.keyword));
			else if (node.type == "ActorBaseFormID") result = ActorBaseMatchesFormID(a_actor, ParseFormIDParam(node.keyword));
			else if (node.type == "ActorRaceFormID") result = ActorRaceMatchesFormID(a_actor, ParseFormIDParam(node.keyword));
			else if (node.type == "ActorCombatStyle") result = ActorCombatStyleMatchesFormID(a_actor, ParseFormIDParam(node.keyword));
			else if (node.type == "ActorClass") result = ActorClassMatchesFormID(a_actor, ParseFormIDParam(node.keyword));
			else if (node.type == "ActorInFaction") result = ActorInFaction(a_actor, ParseFormIDParam(node.keyword));
			else if (node.type == "ActorInCell") result = ActorInCell(a_actor, ParseFormIDParam(node.keyword));
			else if (node.type == "ActorInLocation") result = ActorInLocation(a_actor, ParseFormIDParam(node.keyword), false);
			else if (node.type == "ActorInLocationOrChild") result = ActorInLocation(a_actor, ParseFormIDParam(node.keyword), true);
			else if (node.type == "ActorInWorldspace") result = ActorInWorldspace(a_actor, ParseFormIDParam(node.keyword));
			else if (node.type == "GlobalValue") result = GlobalValueMatches(ParseFormIDParam(node.keyword), node.keyword2);
			else if (node.type == "ActorLevel") result = ActorLevelMatches(a_actor, node.keyword);
			else if (node.type == "ActorValue") result = ActorValueMatches(a_actor, ParseFormIDParam(node.keyword), node.keyword2);
			else if (node.type == "ActorPerkRank") result = ActorPerkRankMatches(a_actor, ParseFormIDParam(node.keyword), node.keyword2);
			else if (node.type == "DayOfWeek") result = DayOfWeekMatches(ParseDayOfWeekParam(node.keyword));
			else if (node.type == "ActorLifeState") result = ActorLifeStateMatches(a_actor, node.keyword);
			else if (node.type == "NodeMonitor") result = NodeMonitor(a_actor, node.keyword, node.keyword2);
			else if (node.type == "IsSunAboveHorizon") result = IsSunAboveHorizon();
			else if (node.type == "TimeOfDayPhase") result = TimeOfDayPhaseMatches(node.keyword);
			else if (node.type == "CurrentWeather") result = CurrentWeatherMatchesFormID(ParseFormIDParam(node.keyword));
			else if (node.type == "WeatherClass") result = WeatherClassMatches(node.keyword);
			else if (node.type == "LightingTemplate") result = LightingTemplateMatchesFormID(a_actor, ParseFormIDParam(node.keyword));
			else if (node.type == "RandomPercent") {
				try { result = RandomPercentMatches(a_actor, a_candidateItem, node.keyword.empty() ? 100.0f : std::stof(node.keyword), node.keyword2); }
				catch (...) {}
			}
			else if (node.type == "IsBleedingOut") result = IsBleedingOut(a_actor);
			else if (node.type == "IsFirstPerson") result = IsFirstPerson(a_actor);
			else if (node.type == "IsInAir") result = IsInAir(a_actor);
			else if (node.type == "IsAiming") result = IsAiming(a_actor);
			else if (node.type == "IsTimeOfDay") {
				try {
					const float startHour = node.keyword.empty() ? 0.0f : std::stof(node.keyword);
					const float endHour = node.keyword2.empty() ? 24.0f : std::stof(node.keyword2);
					result = IsTimeOfDay(startHour, endHour);
				}
				catch (...) {}
			}
			else if (node.type == "RuntimeVariable") {
				const bool variableValue = ConfigManager::GetSingleton()->GetRuntimeVariable(node.keyword);
				if (node.keyword2.empty()) {
					result = variableValue;
				}
				else {
					std::string op;
					float target = 0.0f;
					if (ParseGlobalComparison(node.keyword2, op, target)) {
						result = CompareFloat(variableValue ? 1.0f : 0.0f, op, target);
					}
				}
			}
			else if (node.type == "RuntimeNumberVariable") {
				std::string op;
				float target = 0.0f;
				if (ParseGlobalComparison(node.keyword2.empty() ? ">0" : node.keyword2, op, target)) {
					result = CompareFloat(ConfigManager::GetSingleton()->GetRuntimeNumberVariable(node.keyword), op, target);
				}
			}
			else if (node.type == "HasKeywordEquipped") result = HasKeywordEquipped(a_actor, node.keyword.c_str());
			else if (node.type == "ActorHasKeyword") result = ActorHasKeyword(a_actor, node.keyword.c_str());
			else if (node.type == "InventoryItemCount") result = InventoryItemCountMatches(a_actor, ParseFormIDParam(node.keyword), node.keyword2);
			else if (node.type == "CandidateHasKeyword") result = CandidateHasKeyword(a_candidateItem, node.keyword.c_str());
			else if (node.type == "CandidateFormID") result = CandidateMatchesFormID(a_candidateItem, ParseFormIDParam(node.keyword));
			else if (node.type == "CandidateFormType") result = CandidateMatchesFormType(a_candidateItem, ParseFormTypeParam(node.keyword));
			else if (node.type == "CandidateHasOMOD") result = CandidateHasOMOD(a_candidateItem, ParseFormIDParam(node.keyword));
			else if (node.type == "CandidateCount") result = CandidateStackCountMatches(a_candidateItem, node.keyword);
			else if (node.type == "CandidateInventoryCount") result = CandidateInventoryCountMatches(a_actor, a_candidateItem, node.keyword);
			else if (node.type == "CandidateIsEquipped") result = a_candidateItem && a_candidateItem->isEquipped;
			else if (node.type == "CandidateIsFavorited") result = a_candidateItem && a_candidateItem->isFavorited;
			else if (node.type == "HasEquippedBipedSlot") {
				try { result = IsBipedSlotOccupied(a_actor, std::stoi(node.keyword)); }
				catch (...) {}
			}
			else if (node.type == "HasEquippedFormID") {
				try {
					uint32_t fID = node.keyword.empty() ? 0 : std::stoul(node.keyword, nullptr, 16);
					uint32_t oID = node.keyword2.empty() ? 0 : std::stoul(node.keyword2, nullptr, 16);
					result = HasEquippedFormID(a_actor, fID, oID);
				}
				catch (...) {}
			}

			return node.isNot ? (result != node.expected) : (result == node.expected);
		}
	}

	bool ConditionEvaluator::CheckCannotWear(RE::Actor* a_actor, RE::TESBoundObject* a_item) {
		if (!a_actor || !a_item) return false;

		auto bipedForm = a_item->As<RE::BGSBipedObjectForm>();
		if (!bipedForm || bipedForm->bipedModelData.bipedObjectSlots == 0) return false;

		auto biped = a_actor->GetBiped(false);
		if (!biped) return false;

		uint32_t itemSlots = bipedForm->bipedModelData.bipedObjectSlots;

		for (int i = 0; i < 32; ++i) {
			if ((itemSlots & (1 << i)) != 0) {
				auto& slotData = biped->object[i];
				if (slotData.parent.object && slotData.parent.object != a_item) {
					if (slotData.parent.object->GetFormType() == RE::ENUM_FORM_ID::kARMO) {
						return true;
					}
				}
			}
		}
		return false;
	}

	bool ConditionEvaluator::RollSpawnChance(RE::Actor* a_actor, std::uint32_t a_formID, float a_chance) {
		if (a_chance >= 100.0f) return true;
		if (a_chance <= 0.0f) return false;
		std::uint32_t hash = (a_actor->GetFormID() * 31) ^ a_formID;
		float roll = (hash % 1000) / 10.0f;
		return roll < a_chance;
	}
}
