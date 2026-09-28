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
	{
		auto onChanged = [this]() {
			if (_loaded) {
				RefreshRuntimeSnapshot();
			}
		};
		_slots.SetChangedCallback(onChanged);
		_nodes.SetChangedCallback(onChanged);
		_customs.SetChangedCallback(onChanged);
		_modelGroups.SetChangedCallback(onChanged);
		_nodeMonitors.SetChangedCallback(onChanged);
		_conditionalVariables.SetChangedCallback(onChanged);
		_conditions.SetChangedCallback(onChanged);
		_transforms.SetChangedCallback(onChanged);
		_physics.SetChangedCallback(onChanged);
		_formFilters.SetChangedCallback(onChanged);
	}

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
		RefreshRuntimeSnapshot();
	}

	void GlobalProfileManager::RefreshRuntimeSnapshot()
	{
		ProfileRuntimeSnapshot snapshot;
		for (const auto& [name, record] : _transforms.Data()) {
			if (!name.empty()) {
				snapshot.transforms.emplace(name, record.data);
			}
		}
		for (const auto& [name, record] : _physics.Data()) {
			if (!name.empty()) {
				snapshot.physics.emplace(name, record.data);
			}
		}
		for (const auto& [name, record] : _formFilters.Data()) {
			if (!name.empty() && !record.parserErrors) {
				snapshot.formFilters.emplace(name, record.data);
			}
		}
		REX::INFO(
			"[IAD Profile] runtime snapshot published: transforms={} physics={} formFilters={}",
			snapshot.transforms.size(),
			snapshot.physics.size(),
			snapshot.formFilters.size());
		ProfileRuntimeContext::GetSingleton().Publish(std::move(snapshot));
	}

	std::optional<TransformData> GlobalProfileManager::ResolveRuntimeTransform(const std::string& a_name) const
	{
		return _loaded ? ProfileRuntimeContext::GetSingleton().FindTransform(a_name) : std::nullopt;
	}

	std::optional<PhysicsValues> GlobalProfileManager::ResolveRuntimePhysics(const std::string& a_name) const
	{
		return _loaded ? ProfileRuntimeContext::GetSingleton().FindPhysics(a_name) : std::nullopt;
	}

	std::optional<FormFilter> GlobalProfileManager::ResolveRuntimeFormFilter(const std::string& a_name) const
	{
		return _loaded ? ProfileRuntimeContext::GetSingleton().FindFormFilter(a_name) : std::nullopt;
	}
}
