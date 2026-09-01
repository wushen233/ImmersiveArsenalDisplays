#include "pch.h"
#include "Data/ConfigManager.h"
#include "PapyrusBridge.h"
#include "System/HolsterManager.h"

namespace IAD::PapyrusBridge
{
	namespace
	{
		constexpr std::string_view kScriptName = "IAD_Native";

		void Refresh()
		{
			HolsterManager::GetSingleton()->ForceRefreshAll();
		}

		void SetBooleanVariable(std::monostate, std::string a_name, bool a_value)
		{
			if (a_name.empty()) return;
			ConfigManager::GetSingleton()->SetRuntimeVariable(a_name, a_value);
			Refresh();
		}

		bool GetBooleanVariable(std::monostate, std::string a_name)
		{
			return ConfigManager::GetSingleton()->GetRuntimeVariable(a_name);
		}

		void SetNumberVariable(std::monostate, std::string a_name, float a_value)
		{
			if (a_name.empty()) return;
			ConfigManager::GetSingleton()->SetRuntimeNumberVariable(a_name, a_value);
			Refresh();
		}

		float GetNumberVariable(std::monostate, std::string a_name)
		{
			return ConfigManager::GetSingleton()->GetRuntimeNumberVariable(a_name);
		}

		void SetModelPathVariable(std::monostate, std::string a_name, std::string a_path)
		{
			if (a_name.empty()) return;
			ConfigManager::GetSingleton()->SetRuntimeModelPathVariable(a_name, a_path);
			Refresh();
		}

		std::string GetModelPathVariable(std::monostate, std::string a_name)
		{
			return ConfigManager::GetSingleton()->GetRuntimeModelPathVariable(a_name);
		}

		void SetFormVariable(std::monostate, std::string a_name, RE::TESForm* a_form)
		{
			if (a_name.empty()) return;
			ConfigManager::GetSingleton()->SetRuntimeFormVariable(a_name, a_form ? a_form->GetFormID() : 0);
			Refresh();
		}

		RE::TESForm* GetFormVariable(std::monostate, std::string a_name)
		{
			const auto formID = ConfigManager::GetSingleton()->GetRuntimeFormVariable(a_name);
			return formID ? RE::TESForm::GetFormByID(formID) : nullptr;
		}

		void ClearVariable(std::monostate, std::string a_name)
		{
			if (a_name.empty()) return;
			auto* config = ConfigManager::GetSingleton();
			config->RemoveRuntimeVariable(a_name);
			config->RemoveRuntimeNumberVariable(a_name);
			config->RemoveRuntimeModelPathVariable(a_name);
			config->RemoveRuntimeFormVariable(a_name);
			Refresh();
		}

		void ForceRefresh(std::monostate)
		{
			Refresh();
		}
	}

	bool RegisterFunctions(RE::BSScript::IVirtualMachine* a_vm)
	{
		if (!a_vm) return false;

		a_vm->BindNativeMethod(kScriptName, "SetBooleanVariable"sv, SetBooleanVariable, true);
		a_vm->BindNativeMethod(kScriptName, "GetBooleanVariable"sv, GetBooleanVariable, true);
		a_vm->BindNativeMethod(kScriptName, "SetNumberVariable"sv, SetNumberVariable, true);
		a_vm->BindNativeMethod(kScriptName, "GetNumberVariable"sv, GetNumberVariable, true);
		a_vm->BindNativeMethod(kScriptName, "SetModelPathVariable"sv, SetModelPathVariable, true);
		a_vm->BindNativeMethod(kScriptName, "GetModelPathVariable"sv, GetModelPathVariable, true);
		a_vm->BindNativeMethod(kScriptName, "SetFormVariable"sv, SetFormVariable, true);
		a_vm->BindNativeMethod(kScriptName, "GetFormVariable"sv, GetFormVariable, true);
		a_vm->BindNativeMethod(kScriptName, "ClearVariable"sv, ClearVariable, true);
		a_vm->BindNativeMethod(kScriptName, "ForceRefresh"sv, ForceRefresh, true);
		REX::INFO("[IAD Papyrus] registered IAD_Native runtime-variable functions");
		return true;
	}
}
