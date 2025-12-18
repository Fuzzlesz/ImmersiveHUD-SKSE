#include "Settings.h"
#include "HUDElements.h"
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

	// Load the base config (has all defaults)
	const char* configPath = "Data/MCM/Config/ImmersiveHUD/settings.ini";
	ini.LoadFile(configPath);

	// Overlay user changes (only contains modified values)
	const char* userPath = "Data/MCM/Settings/ImmersiveHUD.ini";
	ini.LoadFile(userPath);

	const char* sectionHUD = "HUD";
	_toggleKey = ini.GetLongValue(sectionHUD, "iToggleKey", 45);
	_holdMode = ini.GetBoolValue(sectionHUD, "bHoldMode", false);
	_alwaysShowInCombat = ini.GetBoolValue(sectionHUD, "bShowInCombat", false);
	_alwaysShowWeaponDrawn = ini.GetBoolValue(sectionHUD, "bShowWeaponDrawn", false);
	_fadeSpeed = static_cast<float>(ini.GetLongValue(sectionHUD, "iFadeSpeed", 5));
	_dumpHUD = ini.GetBoolValue(sectionHUD, "bDumpHUD", false);

	_crosshair.enabled = ini.GetBoolValue("Crosshair", "bEnabled", true);
	_sneakMeter.enabled = ini.GetBoolValue("SneakMeter", "bEnabled", true);

	_widgetPathToMode.clear();
	std::unordered_set<std::string> vanillaPaths;

	// Map Vanilla HUD Elements
	for (const auto& def : HUDElements::Get()) {
		int mode = ini.GetLongValue("HUDElements", def.id, 1);
		for (const auto& path : def.paths) {
			_widgetPathToMode[path] = mode;
			vanillaPaths.insert(path);
		}
	}

	// Map Dynamic Widgets
	std::map<std::string, std::vector<WidgetGroupItem>> groupedWidgets;

	for (const auto& path : _subWidgetPaths) {
		if (vanillaPaths.contains(path)) {
			continue;
		}

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

			int mode = ini.GetLongValue("Widgets", iniKey.c_str(), 1); // Default kImmersive

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
