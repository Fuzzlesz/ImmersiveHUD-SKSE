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
	// Check if already exists first to avoid string ops
	if (_subWidgetPaths.contains(a_path)) {
		return false;
	}

	_subWidgetPaths.insert(a_path);
	if (!a_source.empty()) {
		// Store decoded source once to prevent repetitive decoding elsewhere
		_widgetSources[a_path] = Utils::UrlDecode(a_source);
	}
	return true;
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

	// Look up the cached mode from the INI load
	auto dit = _dynamicWidgetModes.find(iniKey);
	return dit != _dynamicWidgetModes.end() ? dit->second : kImmersive;
}

const std::set<std::string>& Settings::GetSubWidgetPaths() const
{
	return _subWidgetPaths;
}
