#pragma once

#include "Data/ConfigManager.h"

#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace IAD::Profile
{
	enum class ProfileFlags : std::uint32_t {
		kNone = 0,
		kMergeOnly = 1u << 0
	};

	template <class T>
	struct ProfileRecord {
		using data_type = T;

		std::string name;
		std::filesystem::path path;
		std::optional<std::string> description;
		std::uint32_t flags = static_cast<std::uint32_t>(ProfileFlags::kNone);
		bool modified = false;
		bool parserErrors = false;
		bool requiresMetadataMigration = false;
		std::string lastError;
		T data{};

		bool IsMergeOnly() const {
			return (flags & static_cast<std::uint32_t>(ProfileFlags::kMergeOnly)) != 0;
		}

		void SetMergeOnly(bool a_value) {
			if (a_value) {
				flags |= static_cast<std::uint32_t>(ProfileFlags::kMergeOnly);
			}
			else {
				flags &= ~static_cast<std::uint32_t>(ProfileFlags::kMergeOnly);
			}
			modified = true;
		}

		void MarkModified() {
			modified = true;
		}
	};

	template <class T>
	class ProfileManager
	{
	public:
		using record_type = ProfileRecord<T>;
		using storage_type = std::map<std::string, record_type>;
		using LoadDataFn = std::function<bool(const std::string&, T&)>;
		using SaveDataFn = std::function<void(const std::string&, const T&)>;
		using ChangeFn = std::function<void()>;

		ProfileManager(std::string a_folderName, LoadDataFn a_loadFn, SaveDataFn a_saveFn) :
			_folderName(std::move(a_folderName)),
			_loadFn(std::move(a_loadFn)),
			_saveFn(std::move(a_saveFn))
		{}

		void SetChangedCallback(ChangeFn a_callback) {
			_onChanged = std::move(a_callback);
		}

		bool Load() {
			_lastError.clear();
			_data.clear();
			std::filesystem::create_directories(GetRootPath());

			auto* config = ConfigManager::GetSingleton();
			for (const auto& name : config->GetAvailableProfiles(_folderName)) {
				record_type record;
				if (LoadRecord(name, record)) {
					// Existing payload formats remain untouched.  Only the common IED-style
					// metadata envelope is added, so legacy profiles keep loading unchanged.
					if (record.requiresMetadataMigration && !WriteMetadata(record)) {
						record.parserErrors = true;
						record.lastError = _lastError;
					}
					_data.emplace(record.name, std::move(record));
				}
			}
			_initialized = true;
			NotifyChanged();
			return true;
		}

		bool ReloadProfile(const std::string& a_name) {
			record_type record;
			if (!LoadRecord(a_name, record)) {
				return false;
			}
			_data[a_name] = std::move(record);
			NotifyChanged();
			return true;
		}

		bool CreateProfile(const std::string& a_name, const T& a_data = T{}) {
			if (!ValidateName(a_name)) {
				return false;
			}
			if (_data.contains(a_name) || std::filesystem::exists(GetProfilePath(a_name))) {
				_lastError = "Profile already exists";
				return false;
			}

			record_type record;
			record.name = a_name;
			record.path = GetProfilePath(a_name);
			record.data = a_data;
			record.modified = true;

			_data.emplace(a_name, std::move(record));
			return SaveProfile(a_name);
		}

		bool SaveProfile(const std::string& a_name) {
			auto it = _data.find(a_name);
			if (it == _data.end()) {
				_lastError = "No such profile";
				return false;
			}

			try {
				std::filesystem::create_directories(GetRootPath());
				_saveFn(a_name, it->second.data);
				if (!WriteMetadata(it->second)) {
					return false;
				}
				it->second.modified = false;
				it->second.path = GetProfilePath(a_name);
				NotifyChanged();
				return true;
			}
			catch (const std::exception& e) {
				_lastError = e.what();
				it->second.lastError = e.what();
				return false;
			}
		}

		bool DeleteProfile(const std::string& a_name) {
			auto it = _data.find(a_name);
			if (it == _data.end()) {
				_lastError = "No such profile";
				return false;
			}
			if (!ConfigManager::GetSingleton()->DeleteProfile(_folderName, a_name)) {
				_lastError = "Delete failed";
				return false;
			}
			_data.erase(it);
			NotifyChanged();
			return true;
		}

		bool RenameProfile(const std::string& a_oldName, const std::string& a_newName) {
			if (!ValidateName(a_newName)) {
				return false;
			}
			auto it = _data.find(a_oldName);
			if (it == _data.end()) {
				_lastError = "No such profile";
				return false;
			}
			if (_data.contains(a_newName)) {
				_lastError = "Profile already exists";
				return false;
			}
			if (!ConfigManager::GetSingleton()->RenameProfile(_folderName, a_oldName, a_newName)) {
				_lastError = "Rename failed";
				return false;
			}

			auto record = std::move(it->second);
			_data.erase(it);
			record.name = a_newName;
			record.path = GetProfilePath(a_newName);
			_data.emplace(a_newName, std::move(record));
			NotifyChanged();
			return true;
		}

		record_type* Find(const std::string& a_name) {
			auto it = _data.find(a_name);
			return it != _data.end() ? &it->second : nullptr;
		}

		const record_type* Find(const std::string& a_name) const {
			auto it = _data.find(a_name);
			return it != _data.end() ? &it->second : nullptr;
		}

		storage_type& Data() {
			return _data;
		}

		const storage_type& Data() const {
			return _data;
		}

		const std::string& FolderName() const {
			return _folderName;
		}

		const std::string& LastError() const {
			return _lastError;
		}

		bool IsInitialized() const {
			return _initialized;
		}

	private:
		bool LoadRecord(const std::string& a_name, record_type& a_out) {
			if (!ValidateName(a_name)) {
				return false;
			}

			a_out = record_type{};
			a_out.name = a_name;
			a_out.path = GetProfilePath(a_name);

			try {
				if (!_loadFn(a_name, a_out.data)) {
					a_out.parserErrors = true;
					a_out.lastError = "Profile parse failed";
					_lastError = a_out.lastError;
					ReadMetadata(a_out);
					a_out.modified = false;
					return true;
				}

				ReadMetadata(a_out);
				a_out.modified = false;
				return true;
			}
			catch (const std::exception& e) {
				a_out.parserErrors = true;
				a_out.lastError = e.what();
				_lastError = e.what();
				ReadMetadata(a_out);
				a_out.modified = false;
				return true;
			}
		}

		bool ValidateName(const std::string& a_name) {
			if (a_name.empty()) {
				_lastError = "Profile name is empty";
				return false;
			}
			if (a_name.find_first_of("\\/:*?\"<>|") != std::string::npos) {
				_lastError = "Profile name contains invalid path characters";
				return false;
			}
			return true;
		}

		void NotifyChanged() {
			if (_onChanged) {
				_onChanged();
			}
		}

		std::filesystem::path GetRootPath() const {
			const auto dir = ConfigManager::GetSingleton()->GetConfigDir();
			return std::filesystem::path(dir.empty() ? "Data/F4SE/Plugins/ImmersiveArsenalDisplays" : dir) / "Profiles" / _folderName;
		}

		std::filesystem::path GetProfilePath(const std::string& a_name) const {
			return GetRootPath() / (a_name + ".json");
		}

		void ReadMetadata(record_type& a_record) {
			std::ifstream file(a_record.path);
			if (!file.is_open()) {
				return;
			}

			auto root = nlohmann::json::parse(file, nullptr, false);
			if (!root.is_object()) {
				a_record.parserErrors = true;
				return;
			}

			if (root.contains("desc") && root["desc"].is_string()) {
				a_record.description = root["desc"].get<std::string>();
			}
			else {
				a_record.description.reset();
			}

			a_record.flags = root.value("flags", static_cast<std::uint32_t>(ProfileFlags::kNone));
			a_record.requiresMetadataMigration =
				root.value("format", std::string{}) != "IAD.Profile" ||
				root.value("version", 0u) < 2u ||
				root.value("kind", std::string{}) != _folderName;
		}

		bool WriteMetadata(const record_type& a_record) {
			std::ifstream in(a_record.path);
			if (!in.is_open()) {
				_lastError = "Could not reopen saved profile";
				return false;
			}

			auto root = nlohmann::json::parse(in, nullptr, false);
			if (!root.is_object()) {
				_lastError = "Saved profile is not a JSON object";
				return false;
			}
			in.close();

			if (a_record.description && !a_record.description->empty()) {
				root["desc"] = *a_record.description;
			}
			else {
				root.erase("desc");
			}
			root["flags"] = a_record.flags;
			root["format"] = "IAD.Profile";
			root["version"] = 2;
			root["kind"] = _folderName;

			std::ofstream out(a_record.path);
			if (!out.is_open()) {
				_lastError = "Could not write profile metadata";
				return false;
			}
			out << root.dump(4);
			return true;
		}

		std::string _folderName;
		LoadDataFn _loadFn;
		SaveDataFn _saveFn;
		ChangeFn _onChanged;
		storage_type _data;
		std::string _lastError;
		bool _initialized = false;
	};
}
