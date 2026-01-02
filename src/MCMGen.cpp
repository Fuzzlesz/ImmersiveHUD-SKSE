#include "MCMGen.h"
#include "HUDElements.h"
#include "Settings.h"
#include "Utils.h"

namespace fs = std::filesystem;

namespace MCMGen
{
	static bool _iniModifiedThisSession = false;

	bool WidgetSourceExists(const std::string& a_source)
	{
		if (a_source.empty() || a_source == "Unknown") {
			return true;
		}

		// Trust list for internal components or those likely to be in BSAs
		std::string lowerPath = a_source;
		std::transform(lowerPath.begin(), lowerPath.end(), lowerPath.begin(), ::tolower);

		static const std::vector<std::string> kTrustList = {
			"internal/",
			"skyui",
			"hudmenu",
			"vanilla",
			"exported/"
		};

		for (const auto& w : kTrustList) {
			if (lowerPath.find(w) != std::string::npos) {
				return true;
			}
		}

		// Check for loose files on disk to identify uninstalled mods
		std::string path = a_source;
		if (path.starts_with("file:///")) {
			path = path.substr(8);
		}
		std::replace(path.begin(), path.end(), '|', ':');
		std::replace(path.begin(), path.end(), '\\', '/');

		std::error_code ec;
		if (fs::exists(fs::path("Data") / path, ec)) {
			return true;
		}

		return false;
	}

json CreateEnum(const std::string& a_text, const std::string& a_id, const std::string& a_help)
	{
		return {
			{ "text", a_text },
			{ "type", "enum" },
			{ "help", a_help },
			{ "id", a_id },
			{ "valueOptions", { { "sourceType", "ModSettingInt" },
								  { "defaultValue", 1 },
								  { "options", json::array({ "$fzIH_ModeVisible",
												   "$fzIH_ModeImmersive",
												   "$fzIH_ModeHidden",
												   "$fzIH_ModeIgnored",
												   "$fzIH_ModeInterior",
												   "$fzIH_ModeExterior",
												   "$fzIH_ModeInCombat",
												   "$fzIH_ModeNotInCombat",
												   "$fzIH_ModeWeaponDrawn" }) } } }
		};
	}

	void SmartAppendIni(const std::vector<std::string>& a_newKeys, const char* a_section, const fs::path& a_path, CSimpleIniA& a_ini)
	{
		if (a_newKeys.empty()) {
			return;
		}

		bool changed = false;

		for (const auto& key : a_newKeys) {
			if (a_ini.GetValue(a_section, key.c_str(), nullptr) == nullptr) {
				a_ini.SetLongValue(a_section, key.c_str(), 1, nullptr);
				changed = true;
			}
		}

		if (changed) {
			a_ini.SaveFile(a_path.string().c_str());
			_iniModifiedThisSession = true;
		}
	}

	struct WidgetInfo
	{
		std::string rawPath;
		std::string source;
		std::string prettyName;
	};

	struct ElementSortEntry
	{
		std::string sortKey;
		json data;
	};

	void Update(bool a_isRuntime)
	{
		const fs::path configDir = "Data/MCM/Config/ImmersiveHUD";
		const fs::path configPath = configDir / "config.json";
		const fs::path iniPath = configDir / "settings.ini";

		try {
			fs::create_directories(configDir);
		} catch (...) {}

		try {
			// 1. Load Configs
			CSimpleIniA ini;
			ini.SetUnicode();
			bool iniLoaded = (ini.LoadFile(iniPath.string().c_str()) >= 0);
			std::vector<std::string> newIniKeysWidgets;
			std::vector<std::string> newIniKeysElements;

			json originalConfig;
			json config;

			if (fs::exists(configPath)) {
				try {
					std::ifstream inFile(configPath);
					originalConfig = json::parse(inFile);
					config = originalConfig;
				} catch (...) {
					config = json::object();
				}
			} else {
				config = json::object();
			}

			if (!config.contains("pages") || !config["pages"].is_array()) {
				config["pages"] = json::array();
			}

			// 2. Recover persisted paths from existing config & prune dead entries
			std::map<std::string, std::string> allPaths;

			for (const auto& page : config["pages"]) {
				std::string pName = page.value("pageDisplayName", "");
				if (pName == "$fzIH_PageWidgets" && page.contains("content")) {
					for (const auto& item : page["content"]) {
						if (item.contains("help")) {
							std::string help = item["help"].get<std::string>();
							std::string sourceStr = "Unknown";
							std::string rawID = "";


							// Parse "Source: [URL]"
							size_t srcPos = help.find("Source: ");
							if (srcPos != std::string::npos) {
								size_t endSrc = help.find('\n', srcPos);
								sourceStr = help.substr(srcPos + 8, endSrc - (srcPos + 8));
							}

							// Parse "ID: [PATH]"
							size_t idPos = help.find("ID: ");
							if (idPos != std::string::npos) {
								rawID = help.substr(idPos + 4);
								if (rawID.find('\n') != std::string::npos) {
									rawID = rawID.substr(0, rawID.find('\n'));
								}
							}

							if (!rawID.empty()) {
								// Keep entry if the mod is still installed, otherwise discard
								if (WidgetSourceExists(sourceStr)) {
									allPaths[rawID] = sourceStr;
								} else {
									logger::info("Pruning uninstalled widget: {} (Source: {})", rawID, sourceStr);
								}
							}
						}
					}
				}
			}

			// Merge in new runtime discovery data
			const auto settings = Settings::GetSingleton();
			auto memPaths = settings->GetSubWidgetPaths();
			for (const auto& path : memPaths) {
				std::string src = settings->GetWidgetSource(path);
				allPaths[path] = src;
			}

			// 3. Generate Content for "HUD Elements" Page
			std::vector<ElementSortEntry> validElements;
			std::unordered_set<std::string> processedPaths;

			for (const auto& def : HUDElements::Get()) {
				std::string iniKey = def.id;
				std::string mcmID = iniKey + ":HUDElements";
				std::string label = def.label;
				std::string help = "Source: Internal/Vanilla\nID: ";

				if (!def.paths.empty()) {
					help += def.paths[0];
				}

				if (iniLoaded) {
					if (ini.GetValue("HUDElements", iniKey.c_str(), nullptr) == nullptr) {
						newIniKeysElements.push_back(iniKey);
					}
				} else {
					newIniKeysElements.push_back(iniKey);
				}

				validElements.push_back({ label, CreateEnum(label, mcmID, help) });

				for (const auto& p : def.paths) {
					processedPaths.insert(p);
				}
			}

			std::sort(validElements.begin(), validElements.end(), [](const ElementSortEntry& a, const ElementSortEntry& b) {
				return a.sortKey < b.sortKey;
			});

			std::vector<json> elementsJsonList;
			for (const auto& entry : validElements) {
				elementsJsonList.push_back(entry.data);
			}

			// 4. Generate Content for "Widgets" Page (Dynamic)
			std::map<std::string, std::vector<WidgetInfo>> groupedWidgets;
			for (const auto& [path, source] : allPaths) {
				if (processedPaths.contains(path)) {
					continue;
				}
				std::string pretty = Utils::GetWidgetDisplayName(path, source);
				groupedWidgets[pretty].push_back({ path, source, pretty });
			}

			std::map<std::string, json> finalWidgetsMap;
			for (auto& [prettyBase, widgets] : groupedWidgets) {
				std::sort(widgets.begin(), widgets.end(), [](const WidgetInfo& a, const WidgetInfo& b) {
					return a.rawPath < b.rawPath;
				});

				bool multiple = widgets.size() > 1;
				int counter = 1;

				for (const auto& w : widgets) {
					std::string displayName = w.prettyName;
					if (multiple) {
						displayName += " " + std::to_string(counter);
						counter++;
					}

					std::string safeID = Utils::SanitizeName(displayName);
					std::string finalID = "iMode_" + safeID + ":Widgets";
					std::string iniKey = "iMode_" + safeID;
					std::string help = "Source: " + w.source + "\nID: " + w.rawPath;

					if (iniLoaded) {
						if (ini.GetValue("Widgets", iniKey.c_str(), nullptr) == nullptr) {
							newIniKeysWidgets.push_back(iniKey);
						}
					} else {
						newIniKeysWidgets.push_back(iniKey);
					}
					finalWidgetsMap[finalID] = CreateEnum(displayName, finalID, help);
				}
			}

			// 5. Calculate Status Flags
			if (!a_isRuntime && (!newIniKeysElements.empty() || !newIniKeysWidgets.empty())) {
				_iniModifiedThisSession = true;
			}

			// 6. Inject JSON Content
			json* widgetsContent = nullptr;
			json* elementsContent = nullptr;

			for (auto& page : config["pages"]) {
				if (page.value("pageDisplayName", "") == "$fzIH_PageWidgets") {
					widgetsContent = &page["content"];
				}
				if (page.value("pageDisplayName", "") == "$fzIH_PageElements") {
					elementsContent = &page["content"];
				}
			}

			if (elementsContent) {
				elementsContent->clear();
				if (_iniModifiedThisSession) {
					elementsContent->push_back({ { "text", "$fzIH_ElementNewFound" }, { "type", "text" }, { "id", "ElemStatus" } });
				} else {
					elementsContent->push_back({ { "text", "<font color='#00FF00'>Status: " + std::to_string(elementsJsonList.size()) + " HUD Elements registered.</font>" },
						{ "type", "text" }, { "id", "ElemStatus" } });
				}
				elementsContent->push_back({ { "type", "header" } });
				for (const auto& widgetJson : elementsJsonList) {
					elementsContent->push_back(widgetJson);
				}
			}

			if (widgetsContent) {
				widgetsContent->clear();
				if (_iniModifiedThisSession) {
					widgetsContent->push_back({ { "text", "$fzIH_WidgetNewFound" }, { "type", "text" }, { "id", "WidStatus" } });
				} else {
					widgetsContent->push_back({ { "text", "<font color='#00FF00'>Status: " + std::to_string(finalWidgetsMap.size()) + " widgets registered.</font>" },
						{ "type", "text" }, { "id", "WidStatus" } });
				}
				widgetsContent->push_back({ { "type", "header" } });
				for (const auto& [id, widgetJson] : finalWidgetsMap) {
					widgetsContent->push_back(widgetJson);
				}
			}

			// 7. Write to Disk
			if (config != originalConfig) {
				std::ofstream outFile(configPath, std::ios::trunc);
				if (outFile.is_open()) {
					outFile << config.dump(2);
					outFile.close();
				}
			}

			if (iniLoaded || !fs::exists(iniPath)) {
				SmartAppendIni(newIniKeysWidgets, "Widgets", iniPath, ini);
				SmartAppendIni(newIniKeysElements, "HUDElements", iniPath, ini);
			}

		} catch (...) {
			logger::error("Failed to update MCM JSON");
		}
	}
}
