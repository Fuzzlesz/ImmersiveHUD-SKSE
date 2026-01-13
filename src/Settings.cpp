#include "Settings.h"
#include "HUDElements.h"
#include "Utils.h"

void Settings::Load()
{
	CSimpleIniA ini;
	ini.SetUnicode();

	// Load the base config (has all defaults)
	const char* configPath = "Data/MCM/Config/ImmersiveHUD/settings.ini";
	ini.LoadFile(configPath);

	// Overlay user changes (only contains modified values)
	const char* userPath = "Data/MCM/Settings/ImmersiveHUD.ini";
	ini.LoadFile(userPath);

	const char* sectionHUD = "HUD";
	_toggleKey = ini.GetLongValue(sectionHUD, "iToggleKey", 45);
	_holdMode = ini.GetBoolValue(sectionHUD, "bHoldMode", false);
	_startVisible = ini.GetBoolValue(sectionHUD, "bStartVisible", false);
	_alwaysShowInCombat = ini.GetBoolValue(sectionHUD, "bShowInCombat", false);
	_alwaysShowWeaponDrawn = ini.GetBoolValue(sectionHUD, "bShowWeaponDrawn", false);
	_fadeSpeed = static_cast<float>(ini.GetLongValue(sectionHUD, "iFadeSpeed", 5));
	_displayDuration = static_cast<float>(ini.GetDoubleValue(sectionHUD, "fDisplayDuration", 0.0));
	_dumpHUD = ini.GetBoolValue(sectionHUD, "bDumpHUD", false);
	_logMenuFlags = ini.GetBoolValue(sectionHUD, "bLogMenuFlags", false);

	_crosshair.enabled = ini.GetBoolValue("Crosshair", "bEnabled", true);
	_sneakMeter.enabled = ini.GetBoolValue("SneakMeter", "bEnabled", true);

	_widgetPathToMode.clear();

	// Map Vanilla HUD Elements
	for (const auto& def : HUDElements::Get()) {
		int mode = ini.GetLongValue("HUDElements", def.id, 1);
		for (const auto& path : def.paths) {
			_widgetPathToMode[path] = mode;
		}
	}

	// Cache Dynamic Widget settings from INI to support Source-based lookup.
	// We read all keys in the "Widgets" section so we can match them later.
	_dynamicWidgetModes.clear();
	CSimpleIniA::TNamesDepend keys;
	ini.GetAllKeys("Widgets", keys);

	for (const auto& key : keys) {
		int val = ini.GetLongValue("Widgets", key.pItem, 1);
		_dynamicWidgetModes[key.pItem] = val;
	}

	// Pre-load paths discovered in previous sessions from cache file.
	if (_subWidgetPaths.empty()) {
		CSimpleIniA cacheIni;
		cacheIni.SetUnicode();

		const char* cachePath = "Data/MCM/Settings/ImmersiveHUD_Cache.ini";

		if (cacheIni.LoadFile(cachePath) >= 0) {
			CSimpleIniA::TNamesDepend cacheKeys;
			cacheIni.GetAllKeys("PathCache", cacheKeys);

			for (const auto& key : cacheKeys) {
				std::string path = key.pItem;
				std::string source = cacheIni.GetValue("PathCache", key.pItem, "");

				// Insert directly (bypass scanning logic)
				_subWidgetPaths.insert(path);
				if (!source.empty()) {
					_widgetSources[path] = source;
				}
			}
		}
	}
}

void Settings::SaveCache()
{
	CSimpleIniA cacheIni;
	cacheIni.SetUnicode();

	const char* cachePath = "Data/MCM/Settings/ImmersiveHUD_Cache.ini";

	// Ensure the directory exists (in case the user hasn't changed an MCM setting yet)
	try {
		fs::create_directories("Data/MCM/Settings");
	} catch (...) {}

	// We start fresh with the cache file to avoid stale entries
	for (const auto& path : _subWidgetPaths) {
		std::string source = GetWidgetSource(path);
		cacheIni.SetValue("PathCache", path.c_str(), source.c_str());
	}

	cacheIni.SaveFile(cachePath);
}

void Settings::ResetCache()
{
	_subWidgetPaths.clear();
	_widgetSources.clear();
	_widgetPathToMode.clear();
	_dynamicWidgetModes.clear();
}

void Settings::SetDumpHUDEnabled(bool a_enabled)
{
	_dumpHUD = a_enabled;

	CSimpleIniA ini;
	ini.SetUnicode();

	// We only load/save the User Settings file (not the Config default)
	const char* userPath = "Data/MCM/Settings/ImmersiveHUD.ini";

	// Try to load existing user settings to preserve other values
	ini.LoadFile(userPath);

	ini.SetLongValue("HUD", "bDumpHUD", a_enabled ? 1 : 0);

	ini.SaveFile(userPath);
}

bool Settings::AddDiscoveredPath(const std::string& a_path, const std::string& a_source)
{
	bool changed = false;

	// 1. Add path if new
	if (!_subWidgetPaths.contains(a_path)) {
		_subWidgetPaths.insert(a_path);
		changed = true;
	}

	// 2. Update Source if changed
	// Necessary for index collision handling (e.g. if WidgetContainer.0 changes from "Meter" to "Clock")
	if (!a_source.empty()) {
		std::string decoded = Utils::UrlDecode(a_source);
		if (_widgetSources[a_path] != decoded) {
			_widgetSources[a_path] = decoded;
			changed = true;
		}
	}

	return changed;
}

std::string Settings::GetWidgetSource(const std::string& a_path) const
{
	auto it = _widgetSources.find(a_path);
	return it != _widgetSources.end() ? it->second : "Unknown";
}

int Settings::GetWidgetMode(const std::string& a_rawPath) const
{
	// Check direct override (Vanilla elements / Static mappings)
	auto it = _widgetPathToMode.find(a_rawPath);
	if (it != _widgetPathToMode.end()) {
		return it->second;
	}

	// Resolve Dynamic Source-based ID
	// This ensures that "meter.swf" shares a setting regardless of being _root.WidgetContainer.5 or .13
	std::string source = GetWidgetSource(a_rawPath);

	// Generate the Stable ID
	std::string prettyName = Utils::GetWidgetDisplayName(source);
	std::string safeID = Utils::SanitizeName(prettyName);
	std::string iniKey = "iMode_" + safeID;

	// Look up the cached mode from the INI load (case-insensitive)
	for (const auto& [key, value] : _dynamicWidgetModes) {
		if (string::iequals(key, iniKey)) {
			return value;
		}
	}

	return kImmersive;
}

const std::set<std::string>& Settings::GetSubWidgetPaths() const
{
	return _subWidgetPaths;
}
