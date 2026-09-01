#pragma once

#include "Data/ConfigManager.h"

#include <F4SE/Interfaces.h>

#include <map>
#include <mutex>
#include <string>

namespace IAD
{
	class KeyBindStateManager
	{
	public:
		static KeyBindStateManager* GetSingleton()
		{
			static KeyBindStateManager singleton;
			return &singleton;
		}

		// Returns true when at least one named binding advances to its next state.
		bool Update(const std::map<std::string, KeyBindDefinition>& a_definitions);
		// Feeds the game window's physical key edge into the IED-style state machine.
		bool ProcessKeyEvent(std::uint32_t a_key, bool a_isDown);
		bool GetState(const std::string& a_name, std::uint32_t& a_outState) const;
		void ResetForLoad();

		static void F4SEAPI OnSave(const F4SE::SerializationInterface* a_interface);
		static void F4SEAPI OnLoad(const F4SE::SerializationInterface* a_interface);
		static void F4SEAPI OnRevert(const F4SE::SerializationInterface* a_interface);

	private:
		mutable std::mutex _lock;
		std::map<std::string, KeyBindDefinition> _definitions;
		std::map<std::string, std::uint32_t> _states;
		std::map<std::string, bool> _keysDown;
	};
}
