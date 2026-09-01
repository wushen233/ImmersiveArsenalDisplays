#include "pch.h"
#include "Profile/GlobalProfileManager.h"

namespace IAD::Profile
{
	GlobalProfileManager& GlobalProfileManager::GetSingleton() {
		static GlobalProfileManager instance;
		return instance;
	}

	GlobalProfileManager::GlobalProfileManager() :
		_slots(
			"Slot",
			[](const std::string& name, std::vector<SlotDefinition>& data) {
				data.clear();
				return ConfigManager::GetSingleton()->LoadSlotProfile(name, data, true);
			},
			[](const std::string& name, const std::vector<SlotDefinition>& data) {
				ConfigManager::GetSingleton()->SaveSlotProfile(name, data);
			}),
		_nodes(
			"NodeOverrides",
			[](const std::string& name, std::vector<NodeDefinition>& data) {
				data.clear();
				return ConfigManager::GetSingleton()->LoadNodeProfile(name, data, true);
			},
			[](const std::string& name, const std::vector<NodeDefinition>& data) {
				ConfigManager::GetSingleton()->SaveNodeProfile(name, data);
			}),
		_customs(
			"Custom",
			[](const std::string& name, std::vector<CustomDefinition>& data) {
				data.clear();
				return ConfigManager::GetSingleton()->LoadCustomProfile(name, data, true);
			},
			[](const std::string& name, const std::vector<CustomDefinition>& data) {
				ConfigManager::GetSingleton()->SaveCustomProfile(name, data);
			}),
		_modelGroups(
			"ModelGroups",
			[](const std::string& name, std::vector<ModelGroupEntry>& data) {
				data.clear();
				return ConfigManager::GetSingleton()->LoadModelGroupProfile(name, data, true);
			},
			[](const std::string& name, const std::vector<ModelGroupEntry>& data) {
				ConfigManager::GetSingleton()->SaveModelGroupProfile(name, data);
			}),
		_nodeMonitors(
			"NodeMonitors",
			[](const std::string& name, std::vector<std::string>& data) {
				data.clear();
				return ConfigManager::GetSingleton()->LoadNodeMonitorProfile(name, data, true);
			},
			[](const std::string& name, const std::vector<std::string>& data) {
				ConfigManager::GetSingleton()->SaveNodeMonitorProfile(name, data);
			}),
		_conditionalVariables(
			"ConditionalVariables",
			[](const std::string& name, std::vector<ConditionalVariableDefinition>& data) {
				data.clear();
				return ConfigManager::GetSingleton()->LoadConditionalVariableProfile(name, data, true);
			},
			[](const std::string& name, const std::vector<ConditionalVariableDefinition>& data) {
				ConfigManager::GetSingleton()->SaveConditionalVariableProfile(name, data);
			}),
		_conditions(
			"Conditions",
			[](const std::string& name, ConditionNode& data) {
				return ConfigManager::GetSingleton()->LoadConditionProfile(name, data);
			},
			[](const std::string& name, const ConditionNode& data) {
				ConfigManager::GetSingleton()->SaveConditionProfile(name, data);
			}),
		_transforms(
			"Transforms",
			[](const std::string& name, TransformData& data) {
				return ConfigManager::GetSingleton()->LoadTransformProfile(name, data);
			},
			[](const std::string& name, const TransformData& data) {
				ConfigManager::GetSingleton()->SaveTransformProfile(name, data);
			}),
		_physics(
			"Physics",
			[](const std::string& name, PhysicsValues& data) {
				return ConfigManager::GetSingleton()->LoadPhysicsProfile(name, data);
			},
			[](const std::string& name, const PhysicsValues& data) {
				ConfigManager::GetSingleton()->SavePhysicsProfile(name, data);
			}),
		_formFilters(
			"FormFilters",
			[](const std::string& name, FormFilter& data) {
				return ConfigManager::GetSingleton()->LoadFormFilterProfile(name, data);
			},
			[](const std::string& name, const FormFilter& data) {
				ConfigManager::GetSingleton()->SaveFormFilterProfile(name, data);
			})
	{}

	void GlobalProfileManager::LoadAll() {
		_slots.Load();
		_nodes.Load();
		_customs.Load();
		_modelGroups.Load();
		_nodeMonitors.Load();
		_conditionalVariables.Load();
		_conditions.Load();
		_transforms.Load();
		_physics.Load();
		_formFilters.Load();
		_loaded = true;
	}
}
