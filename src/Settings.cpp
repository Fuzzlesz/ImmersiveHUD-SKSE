#include "Settings.h"
#include "Utils.h"

struct WidgetGroupItem
{
	std::string rawPath;
	std::string prettyName;
};

void Settings::Load()
{
	CSimpleIniA ini;
	ini.SetUnicode();

	const char* userPath = "Data/MCM/Settings/ImmersiveHUD.ini";
	const char* defaultsPath = "Data/MCM/Config/ImmersiveHUD/settings.ini";

	if (ini.LoadFile(userPath) < 0) {
		ini.LoadFile(defaultsPath);
	}

	// 1. Load HUD Settings
	const char* sectionHUD = "HUD";
	_toggleKey = ini.GetLongValue(sectionHUD, "iToggleKey", 45);
	_holdMode = ini.GetBoolValue(sectionHUD, "bHoldMode", false);
	_alwaysShowInCombat = ini.GetBoolValue(sectionHUD, "bShowInCombat", true);
	_alwaysShowWeaponDrawn = ini.GetBoolValue(sectionHUD, "bShowWeaponDrawn", true);
	_fadeSpeed = static_cast<float>(ini.GetDoubleValue(sectionHUD, "fFadeSpeed", 5.0));
	_dumpHUD = ini.GetBoolValue(sectionHUD, "bDumpHUD", false);

	_crosshair.enabled = ini.GetBoolValue("Crosshair", "bEnabled", true);
	_sneakMeter.enabled = ini.GetBoolValue("SneakMeter", "bEnabled", true);

	// 2. Load all raw values from [Widgets] into temporary map
	const char* sectionWidgets = "Widgets";
	std::map<std::string, int> rawIniValues;
	CSimpleIniA::TNamesDepend keys;
	ini.GetAllKeys(sectionWidgets, keys);

	for (const auto& key : keys) {
		std::string keyStr = key.pItem;
		if (keyStr.starts_with("iMode_")) {
			rawIniValues[keyStr] = ini.GetLongValue(sectionWidgets, keyStr.c_str(), 1);
		}
	}

	_widgetPathToMode.clear();

	// 3. Map Raw Runtime Paths to Pretty INI Keys
	std::map<std::string, std::vector<WidgetGroupItem>> groupedWidgets;

	for (const auto& path : _subWidgetPaths) {
		// We must URL Decode the source to match MCMGen's behaviour.
		// e.g. "B612%5FAnnouncement.swf" -> "B612_Announcement.swf"
		std::string source = Utils::UrlDecode(GetWidgetSource(path));
		std::string pretty = Utils::GetWidgetDisplayName(path, source);
		groupedWidgets[pretty].push_back({ path, pretty });
	}

	for (auto& [prettyBase, widgets] : groupedWidgets) {
		// Sort by raw path to ensure deterministic numbering (Meter 1, Meter 2).
		std::sort(widgets.begin(), widgets.end(), [](const WidgetGroupItem& a, const WidgetGroupItem& b) {
			return a.rawPath < b.rawPath;
		});

		bool multiple = widgets.size() > 1;
		int counter = 1;

		for (const auto& w : widgets) {
			std::string finalDisplayName = w.prettyName;
			if (multiple) {
				finalDisplayName += " " + std::to_string(counter);
				counter++;
			}

			// Generate INI Key
			std::string iniKey = "iMode_" + Utils::SanitizeName(finalDisplayName);

			int mode = 1;  // Default kImmersive
			if (auto it = rawIniValues.find(iniKey); it != rawIniValues.end()) {
				mode = it->second;
			}

			_widgetPathToMode[w.rawPath] = mode;
		}
	}

	logger::info("Settings loaded. Mapped {} widget paths.", _widgetPathToMode.size());
}

void Settings::ResetCache()
{
	_subWidgetPaths.clear();
	_widgetSources.clear();
	_widgetPathToMode.clear();
}

bool Settings::AddDiscoveredPath(const std::string& a_path, const std::string& a_source)
{
	bool isNew = _subWidgetPaths.insert(a_path).second;
	if (!a_source.empty()) {
		_widgetSources[a_path] = a_source;
	}
	return isNew;
}

std::string Settings::GetWidgetSource(const std::string& a_path) const
{
	auto it = _widgetSources.find(a_path);
	return it != _widgetSources.end() ? it->second : "Unknown";
}

int Settings::GetWidgetMode(const std::string& a_rawPath) const
{
	auto it = _widgetPathToMode.find(a_rawPath);
	return it != _widgetPathToMode.end() ? it->second : kImmersive;
}

const std::set<std::string>& Settings::GetSubWidgetPaths() const
{
	return _subWidgetPaths;
}

void OnConfigClose(RE::StaticFunctionTag*)
{
	logger::info("MCM menu closed. Reloading settings...");
	Settings::GetSingleton()->Load();
}

bool Settings::RegisterPapyrus(RE::BSScript::IVirtualMachine* a_vm)
{
	a_vm->RegisterFunction("OnConfigClose", "ImmersiveHUD_MCM", OnConfigClose);
	return true;
}
