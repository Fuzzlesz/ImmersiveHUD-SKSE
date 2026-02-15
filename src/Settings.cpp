#include "Settings.h"
#include "HUDElements.h"
#include "Utils.h"

	// -------------------------------------------------------------------------
	// Helper: Loads a Default INI, then overlays a User INI, then runs callback
	// -------------------------------------------------------------------------
	void Settings::LoadINI(const fs::path& a_defaultPath, const fs::path& a_userPath, INIFunc a_func)
{
	CSimpleIniA ini;
	ini.SetUnicode();

	// 1. Load the Base Config (Read-Only reference)
	if (fs::exists(a_defaultPath)) {
		ini.LoadFile(a_defaultPath.string().c_str());
	}

	// 2. Load User Config (Overrides)
	if (fs::exists(a_userPath)) {
		ini.LoadFile(a_userPath.string().c_str());
	}

	// 3. Execute the data extraction logic
	if (a_func) {
		a_func(ini);
	}
}

// -------------------------------------------------------------------------
// Main Load Function
// -------------------------------------------------------------------------
void Settings::Load()
{
	// Delete old cache if it exists
	const fs::path oldCache = "Data/MCM/Settings/ImmersiveHUD_Cache.ini";
	if (fs::exists(oldCache)) {
		fs::remove(oldCache);
	}

	// Use the helper to handle file I/O logic
	LoadINI(defaultPath, userPath, [&](CSimpleIniA& ini) {
		const char* sectionHUD = "HUD";

		_toggleKey = ini.GetLongValue(sectionHUD, "iToggleKey", 45);
		_holdMode = ini.GetBoolValue(sectionHUD, "bHoldMode", false);
		_startVisible = ini.GetBoolValue(sectionHUD, "bStartVisible", false);
		_alwaysShowInCombat = ini.GetBoolValue(sectionHUD, "bShowInCombat", false);
		_alwaysShowWeaponDrawn = ini.GetBoolValue(sectionHUD, "bShowWeaponDrawn", false);

		// Fallback for migration: If iFadeSpeed exists but split keys don't, use it.
		// Otherwise default.
		long legacySpeed = ini.GetLongValue(sectionHUD, "iFadeSpeed", 5);
		_fadeInSpeed = static_cast<float>(ini.GetLongValue(sectionHUD, "iFadeInSpeed", legacySpeed));
		_fadeOutSpeed = static_cast<float>(ini.GetLongValue(sectionHUD, "iFadeOutSpeed", legacySpeed));

		_displayDuration = static_cast<float>(ini.GetDoubleValue(sectionHUD, "fDisplayDuration", 0.0));
		_dumpHUD = ini.GetBoolValue(sectionHUD, "bDumpHUD", false);
		_logMenuFlags = ini.GetBoolValue(sectionHUD, "bLogMenuFlags", false);

		_hudOpacityMin = static_cast<float>(ini.GetDoubleValue(sectionHUD, "fHUDOpacityMin", 0.0));
		_hudOpacityMax = static_cast<float>(ini.GetDoubleValue(sectionHUD, "fHUDOpacityMax", 100.0));
		_contextOpacityMin = static_cast<float>(ini.GetDoubleValue(sectionHUD, "fContextOpacityMin", 0.0));
		_contextOpacityMax = static_cast<float>(ini.GetDoubleValue(sectionHUD, "fContextOpacityMax", 100.0));

		_crosshair.enabled = ini.GetBoolValue("Crosshair", "bEnabled", true);
		_crosshair.hideWhileAiming = ini.GetBoolValue("Crosshair", "bHideWhileAiming", false);
		_sneakMeter.enabled = ini.GetBoolValue("SneakMeter", "bEnabled", true);

		// --- Map Vanilla HUD Elements ---
		_widgetPathToMode.clear();
		for (const auto& def : HUDElements::Get()) {
			int mode = ini.GetLongValue("HUDElements", def.id, 1);
			for (const auto& path : def.paths) {
				_widgetPathToMode[path] = mode;
			}
		}

		// --- Cache Dynamic Widget Settings ---
		// We read all keys in "Widgets" to match source files later
		_dynamicWidgetModes.clear();
		CSimpleIniA::TNamesDepend keys;
		ini.GetAllKeys("Widgets", keys);

		for (const auto& key : keys) {
			int val = ini.GetLongValue("Widgets", key.pItem, 1);
			_dynamicWidgetModes[key.pItem] = val;
		}
	});

	// --- Load Path Cache ---
	if (_subWidgetPaths.empty() && fs::exists(cachePath)) {
		CSimpleIniA cacheIni;
		cacheIni.SetUnicode();

		if (cacheIni.LoadFile(cachePath.string().c_str()) >= 0) {
			long cachedVer = cacheIni.GetLongValue("General", "iCacheVersion", 0);
			if (cachedVer != kCacheVersion) {
				logger::info("Cache version mismatch (Expected: {}, Found: {}). Invalidating cache.", kCacheVersion, cachedVer);
				return;
			}

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

	cacheIni.SetLongValue("General", "iCacheVersion", kCacheVersion);

	for (const auto& path : _subWidgetPaths) {
		cacheIni.SetValue("PathCache", path.c_str(), GetWidgetSource(path).c_str());
	}

	cacheIni.SaveFile(cachePath.string().c_str());
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

	// We only edit the User path
	if (fs::exists(userPath)) {
		ini.LoadFile(userPath.string().c_str());
	}

	// Ensure directory exists if this is the first time saving
	try {
		if (const auto dir = userPath.parent_path(); !fs::exists(dir)) {
			fs::create_directories(dir);
		}
	} catch (...) {}

	ini.SetLongValue("HUD", "bDumpHUD", a_enabled ? 1 : 0);
	ini.SaveFile(userPath.string().c_str());
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
	// 1. Check direct override (Vanilla elements / Static mappings)
	auto it = _widgetPathToMode.find(a_rawPath);
	if (it != _widgetPathToMode.end()) {
		return it->second;
	}

	// 2. Resolve Dynamic Source-based ID
	// This ensures that "meter.swf" shares a setting regardless of being _root.WidgetContainer.5 or .13
	std::string source = GetWidgetSource(a_rawPath);

	// Generate the Stable ID
	std::string prettyName = Utils::GetWidgetDisplayName(source);
	std::string safeID = Utils::SanitizeName(prettyName);
	std::string iniKey = "iMode_" + safeID;

	// 3. Look up in cached dynamic settings
	for (const auto& [key, value] : _dynamicWidgetModes) {
		if (string::iequals(key, iniKey)) {
			return value;
		}
	}

	return kImmersive;  // Default fallback
}
