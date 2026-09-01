#include "pch.h"
#include "System/KeyBindStateManager.h"

#include <algorithm>
#include <limits>

namespace IAD
{
	namespace
	{
		constexpr std::uint32_t kRecordType = 'IKBS';
		constexpr std::uint32_t kRecordVersion = 1;
		constexpr std::uint32_t kMaxBindings = 256;
		constexpr std::uint32_t kMaxNameLength = 128;

		bool IsVirtualKeyDown(std::uint32_t a_key)
		{
			return a_key != 0 && a_key <= 0xFF && (GetAsyncKeyState(static_cast<int>(a_key)) & 0x8000) != 0;
		}

		bool ReadExact(const F4SE::SerializationInterface* a_interface, void* a_data, std::uint32_t a_size, std::uint32_t& a_remaining)
		{
			if (!a_interface || a_size > a_remaining || a_interface->ReadRecordData(a_data, a_size) != a_size) {
				return false;
			}
			a_remaining -= a_size;
			return true;
		}

		template <class T>
		bool ReadValue(const F4SE::SerializationInterface* a_interface, T& a_value, std::uint32_t& a_remaining)
		{
			return ReadExact(a_interface, std::addressof(a_value), sizeof(T), a_remaining);
		}
	}

	bool KeyBindStateManager::Update(const std::map<std::string, KeyBindDefinition>& a_definitions)
	{
		std::lock_guard lock(_lock);
		_definitions = a_definitions;
		bool changed = false;

		for (auto it = _states.begin(); it != _states.end();) {
			if (!a_definitions.contains(it->first)) {
				_keysDown.erase(it->first);
				it = _states.erase(it);
			}
			else {
				++it;
			}
		}

		for (const auto& [name, definition] : a_definitions) {
			if (name.empty() || definition.key == 0 || definition.key > 0xFF) {
				continue;
			}

			const auto isDown = IsVirtualKeyDown(definition.key);
			auto [stateIt, insertedState] = _states.try_emplace(name, 0);
			auto [downIt, insertedDown] = _keysDown.try_emplace(name, isDown);
			const auto maxState = std::clamp(definition.numStates, 1u, 32u);
			if (!insertedState && stateIt->second > maxState) {
				stateIt->second = 0;
				changed = true;
			}
			if (insertedDown) {
				continue;
			}

			if (isDown && !downIt->second &&
				(definition.comboKey == 0 || IsVirtualKeyDown(definition.comboKey))) {
				stateIt->second = stateIt->second >= maxState ? 0 : stateIt->second + 1;
				changed = true;
				REX::INFO("[IAD Keybind] '{}' advanced to state {}/{}", name, stateIt->second, maxState);
			}
			downIt->second = isDown;
		}

		return changed;
	}

	bool KeyBindStateManager::ProcessKeyEvent(std::uint32_t a_key, bool a_isDown)
	{
		if (a_key == 0 || a_key > 0xFF) return false;
		std::lock_guard lock(_lock);
		bool changed = false;
		for (const auto& [name, definition] : _definitions) {
			if (definition.key != a_key) continue;

			auto [stateIt, insertedState] = _states.try_emplace(name, 0);
			auto [downIt, insertedDown] = _keysDown.try_emplace(name, false);
			const auto maxState = std::clamp(definition.numStates, 1u, 32u);
			if (!insertedState && stateIt->second > maxState) {
				stateIt->second = 0;
				changed = true;
			}
			if (a_isDown && !downIt->second &&
				(definition.comboKey == 0 || IsVirtualKeyDown(definition.comboKey))) {
				stateIt->second = stateIt->second >= maxState ? 0 : stateIt->second + 1;
				changed = true;
				REX::INFO("[IAD Keybind] '{}' advanced to state {}/{}", name, stateIt->second, maxState);
			}
			downIt->second = a_isDown;
		}
		return changed;
	}

	bool KeyBindStateManager::GetState(const std::string& a_name, std::uint32_t& a_outState) const
	{
		std::lock_guard lock(_lock);
		const auto it = _states.find(a_name);
		if (it == _states.end()) {
			return false;
		}
		a_outState = it->second;
		return true;
	}

	void KeyBindStateManager::ResetForLoad()
	{
		std::lock_guard lock(_lock);
		_definitions.clear();
		_states.clear();
		_keysDown.clear();
	}

	void F4SEAPI KeyBindStateManager::OnSave(const F4SE::SerializationInterface* a_interface)
	{
		if (!a_interface) return;
		auto* manager = GetSingleton();
		std::lock_guard lock(manager->_lock);
		if (!a_interface->OpenRecord(kRecordType, kRecordVersion)) {
			REX::WARN("[IAD Keybind] failed to open serialization record");
			return;
		}

		const auto count = static_cast<std::uint32_t>(manager->_states.size());
		if (!a_interface->WriteRecordData(count)) return;
		for (const auto& [name, state] : manager->_states) {
			const auto nameLength = static_cast<std::uint32_t>(name.size());
			if (nameLength == 0 || nameLength > kMaxNameLength ||
				!a_interface->WriteRecordData(nameLength) ||
				!a_interface->WriteRecordData(name.data(), nameLength) ||
				!a_interface->WriteRecordData(state)) {
				REX::WARN("[IAD Keybind] failed to write binding state");
				return;
			}
		}
		REX::DEBUG("[IAD Keybind] saved {} named keybind state(s)", count);
	}

	void F4SEAPI KeyBindStateManager::OnLoad(const F4SE::SerializationInterface* a_interface)
	{
		auto* manager = GetSingleton();
		manager->ResetForLoad();
		if (!a_interface) return;

		std::map<std::string, std::uint32_t> loadedStates;
		std::uint32_t type = 0;
		std::uint32_t version = 0;
		std::uint32_t length = 0;
		while (a_interface->GetNextRecordInfo(type, version, length)) {
			if (type != kRecordType) {
				continue;
			}
			if (version != kRecordVersion) {
				REX::WARN("[IAD Keybind] ignored unsupported serialization version {}", version);
				continue;
			}

			std::uint32_t remaining = length;
			std::uint32_t count = 0;
			if (!ReadValue(a_interface, count, remaining) || count > kMaxBindings) {
				REX::WARN("[IAD Keybind] ignored invalid serialized binding count");
				return;
			}
			for (std::uint32_t i = 0; i < count; ++i) {
				std::uint32_t nameLength = 0;
				std::uint32_t state = 0;
				if (!ReadValue(a_interface, nameLength, remaining) || nameLength == 0 || nameLength > kMaxNameLength || nameLength > remaining) {
					REX::WARN("[IAD Keybind] ignored malformed serialized binding name");
					return;
				}
				std::string name(nameLength, '\0');
				if (!ReadExact(a_interface, name.data(), nameLength, remaining) || !ReadValue(a_interface, state, remaining)) {
					REX::WARN("[IAD Keybind] ignored truncated serialized binding");
					return;
				}
				loadedStates.emplace(std::move(name), state);
			}
		}

		std::lock_guard lock(manager->_lock);
		manager->_states = std::move(loadedStates);
		manager->_keysDown.clear();
		REX::DEBUG("[IAD Keybind] restored {} named keybind state(s)", manager->_states.size());
	}

	void F4SEAPI KeyBindStateManager::OnRevert(const F4SE::SerializationInterface*)
	{
		GetSingleton()->ResetForLoad();
		REX::DEBUG("[IAD Keybind] cleared named keybind state for load transition");
	}
}
