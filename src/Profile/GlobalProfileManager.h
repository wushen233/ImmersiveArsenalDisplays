#pragma once

#include "Profile/ProfileManager.h"

namespace IAD::Profile
{
	using SlotProfileManager = ProfileManager<std::vector<SlotDefinition>>;
	using NodeProfileManager = ProfileManager<std::vector<NodeDefinition>>;
	using CustomProfileManager = ProfileManager<std::vector<CustomDefinition>>;
	using ModelGroupProfileManager = ProfileManager<std::vector<ModelGroupEntry>>;
	using NodeMonitorProfileManager = ProfileManager<std::vector<std::string>>;
	using ConditionalVariableProfileManager = ProfileManager<std::vector<ConditionalVariableDefinition>>;
	using ConditionProfileManager = ProfileManager<ConditionNode>;
	using TransformProfileManager = ProfileManager<TransformData>;
	using PhysicsProfileManager = ProfileManager<PhysicsValues>;
	using FormFilterProfileManager = ProfileManager<FormFilter>;

	class GlobalProfileManager
	{
	public:
		static GlobalProfileManager& GetSingleton();

		void LoadAll();
		bool IsLoaded() const { return _loaded; }

		SlotProfileManager& Slots() { return _slots; }
		NodeProfileManager& Nodes() { return _nodes; }
		CustomProfileManager& Customs() { return _customs; }
		ModelGroupProfileManager& ModelGroups() { return _modelGroups; }
		NodeMonitorProfileManager& NodeMonitors() { return _nodeMonitors; }
		ConditionalVariableProfileManager& ConditionalVariables() { return _conditionalVariables; }
		ConditionProfileManager& Conditions() { return _conditions; }
		TransformProfileManager& Transforms() { return _transforms; }
		PhysicsProfileManager& Physics() { return _physics; }
		FormFilterProfileManager& FormFilters() { return _formFilters; }

	private:
		GlobalProfileManager();

		SlotProfileManager _slots;
		NodeProfileManager _nodes;
		CustomProfileManager _customs;
		ModelGroupProfileManager _modelGroups;
		NodeMonitorProfileManager _nodeMonitors;
		ConditionalVariableProfileManager _conditionalVariables;
		ConditionProfileManager _conditions;
		TransformProfileManager _transforms;
		PhysicsProfileManager _physics;
		FormFilterProfileManager _formFilters;
		bool _loaded = false;
	};
}
