#pragma once

#include "Data/ConfigManager.h"

#include <map>
#include <mutex>
#include <optional>
#include <string>

namespace IAD::Profile
{
	struct ProfileRuntimeSnapshot
	{
		std::map<std::string, TransformData> transforms;
		std::map<std::string, PhysicsValues> physics;
		std::map<std::string, FormFilter> formFilters;
	};

	class ProfileRuntimeContext final
	{
	public:
		static ProfileRuntimeContext& GetSingleton();

		void Publish(ProfileRuntimeSnapshot a_snapshot);
		bool IsInitialized() const;
		std::optional<TransformData> FindTransform(const std::string& a_name) const;
		std::optional<PhysicsValues> FindPhysics(const std::string& a_name) const;
		std::optional<FormFilter> FindFormFilter(const std::string& a_name) const;

	private:
		ProfileRuntimeContext() = default;

		mutable std::mutex _mutex;
		ProfileRuntimeSnapshot _snapshot;
		bool _initialized = false;
	};
}
