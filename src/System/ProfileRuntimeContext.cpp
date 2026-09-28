#include "pch.h"
#include "System/ProfileRuntimeContext.h"

namespace IAD::Profile
{
	ProfileRuntimeContext& ProfileRuntimeContext::GetSingleton()
	{
		static ProfileRuntimeContext instance;
		return instance;
	}

	void ProfileRuntimeContext::Publish(ProfileRuntimeSnapshot a_snapshot)
	{
		std::lock_guard lock(_mutex);
		_snapshot = std::move(a_snapshot);
		_initialized = true;
	}

	bool ProfileRuntimeContext::IsInitialized() const
	{
		std::lock_guard lock(_mutex);
		return _initialized;
	}

	std::optional<TransformData> ProfileRuntimeContext::FindTransform(const std::string& a_name) const
	{
		std::lock_guard lock(_mutex);
		if (!_initialized) {
			return std::nullopt;
		}
		const auto it = _snapshot.transforms.find(a_name);
		return it != _snapshot.transforms.end() ? std::optional<TransformData>(it->second) : std::nullopt;
	}

	std::optional<PhysicsValues> ProfileRuntimeContext::FindPhysics(const std::string& a_name) const
	{
		std::lock_guard lock(_mutex);
		if (!_initialized) {
			return std::nullopt;
		}
		const auto it = _snapshot.physics.find(a_name);
		return it != _snapshot.physics.end() ? std::optional<PhysicsValues>(it->second) : std::nullopt;
	}

	std::optional<FormFilter> ProfileRuntimeContext::FindFormFilter(const std::string& a_name) const
	{
		std::lock_guard lock(_mutex);
		if (!_initialized) {
			return std::nullopt;
		}
		const auto it = _snapshot.formFilters.find(a_name);
		return it != _snapshot.formFilters.end() ? std::optional<FormFilter>(it->second) : std::nullopt;
	}
}
