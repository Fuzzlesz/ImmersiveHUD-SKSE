#include "Compat.h"
#include "HUDElements.h"
#include "HUDManager.h"
#include "MCMGen.h"
#include "Settings.h"
#include "Utils.h"

namespace fs = std::filesystem;

namespace MCMGen
{
	static bool _iniModifiedThisSession = false;

	// ==========================================
	// Utility Helpers
	// ==========================================

	bool WidgetSourceExists(const std::string& a_source)
	{
		RE::BSResourceNiBinaryStream stream(a_source);
		return stream.good();
	}

	json CreateEnum(const std::string& a_text, const std::string& a_id, const std::string& a_help)
	{
		std::vector<std::string> optionsList = {
			"$fzIH_ModeVisible",
			"$fzIH_ModeImmersive",
			"$fzIH_ModeHidden",
			"$fzIH_ModeIgnored",
			"$fzIH_ModeInterior",
			"$fzIH_ModeExterior",
			"$fzIH_ModeInCombat",
			"$fzIH_ModeNotInCombat",
			"$fzIH_ModeWeaponDrawn"
		};

		// Safely add Locked On placeholder if TDM is not installed.
		// LockedOn MUST be the last item to preserve index safety for previous items.
		if (Compat::GetSingleton()->g_TDM) {
			optionsList.push_back("$fzIH_ModeLockedOn");
		} else {
			optionsList.push_back("$fzIH_ModeTDMDisabled");
		}

		// Note to self: Further options get added here, after TDM, so we maintain positioning.

		// Construct in alphabetical order to match MCM Helper output structure
		return {
			{ "help", a_help },
			{ "id", a_id },
			{ "text", a_text },
			{ "type", "enum" },
			{ "valueOptions", { { "defaultValue", 1 },
								  { "options", optionsList },
								  { "sourceType", "ModSettingInt" } } }
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

	// ==========================================
	// Main Update Loop
	// ==========================================

	void Update(bool a_isRuntime, bool a_widgetsPopulated)
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

			// 2. Recover persisted paths & Prune dead entries
			std::map<std::string, std::string> allPaths;

			// Track IDs present in the previous session's JSON
			std::unordered_set<std::string> previousJsonIDs;

			// Lookup set for Hardcoded/Vanilla paths to prevent flagging them as "New"
			std::unordered_set<std::string> hardcodedVanillaPaths;
			for (const auto& def : HUDElements::Get()) {
				for (const auto& p : def.paths) {
					hardcodedVanillaPaths.insert(p);
				}
			}

			// Grab currently active paths from memory
			const auto settings = Settings::GetSingleton();
			const auto& activePaths = settings->GetSubWidgetPaths();

			for (const auto& page : config["pages"]) {
				std::string pName = page.value("pageDisplayName", "");

				// Scan both Widget and Element pages to recover IDs
				if ((pName == "$fzIH_PageWidgets" || pName == "$fzIH_PageElements") && page.contains("content")) {
					for (const auto& item : page["content"]) {
						if (item.contains("help")) {
							std::string help = item["help"].get<std::string>();
							std::string sourceStr = "Unknown";
							std::string rawID = "";

							// Parse "Source: [URL]"
							size_t srcPos = help.find("Source: ");
							if (srcPos != std::string::npos) {
								size_t endSrc = help.find('\n', srcPos);
								if (endSrc != std::string::npos) {
									sourceStr = help.substr(srcPos + 8, endSrc - (srcPos + 8));
								} else {
									sourceStr = help.substr(srcPos + 8);
								}
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
								previousJsonIDs.insert(rawID);

								std::string lowerSrc = sourceStr;
								std::transform(lowerSrc.begin(), lowerSrc.end(), lowerSrc.begin(), ::tolower);

								// Heuristic: Is this a SkyUI widget? 💁🦋
								bool isWidget = (lowerSrc.find("widgets/") != std::string::npos) ||
								                (lowerSrc.find("skyui") != std::string::npos);

								// Pruning Guard: SkyUI widgets are late-loading.
								// During Initial Scans (before the HUD Menu loads), the WidgetContainer is empty.
								// We must skip pruning these specific IDs until we are in Runtime and know the container is populated.
								// Otherwise, installed widgets would be wrongly flagged as uninstalled and removed from the config.
								if (isWidget && !a_widgetsPopulated) {
									allPaths[rawID] = sourceStr;
									continue;
								}

								// Check Validity:
								// 1. Is it a Vanilla element? (Skip disk check, always valid)
								bool isVanilla = hardcodedVanillaPaths.contains(rawID);

								// 2. Is it in active memory? (Currently loaded)
								bool existsInMemory = activePaths.contains(rawID);

								// 3. Does the file exist? (Check BSResource)
								// Only run if not Vanilla to avoid checking "Internal/Vanilla" strings against the filesystem.
								bool existsOnDisk = false;
								if (!isVanilla) {
									existsOnDisk = WidgetSourceExists(sourceStr);
								}

								if (isVanilla || existsInMemory || existsOnDisk) {
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
			bool foundNewWidgetInJson = false;
			auto memPaths = settings->GetSubWidgetPaths();

			for (const auto& path : memPaths) {
				std::string src = settings->GetWidgetSource(path);
				allPaths[path] = src;

				// Detection Logic: Was this widget missing from the previous config?
				if (!previousJsonIDs.contains(path)) {
					// Ignore vanilla secondary paths (e.g. HealthMeterLeft) to prevent false positives.
					if (hardcodedVanillaPaths.contains(path)) {
						continue;
					}
					foundNewWidgetInJson = true;
				}
			}

			// 3. Generate Content for "HUD Elements" Page
			std::vector<ElementSortEntry> validElements;
			std::unordered_set<std::string> processedPaths;

			// Check for SkyHUD presence to conditionally filter the combined meter
			bool isSkyHUD = HUDManager::GetSingleton()->IsSkyHUDActive();

			for (const auto& def : HUDElements::Get()) {
				// Conditional Filter: Skip SkyHUD Combined meter if SkyHUD not active
				if (strcmp(def.id, "iMode_EnchantCombined") == 0 && !isSkyHUD) {
					continue;
				}

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
			// If new content is discovered during Initial/Mid Scans (!Runtime),
			// we flag the session to display the "Restart Required" warning.
			// Once Runtime is set (post-Mid Scan), we stop triggering this flag
			// as the MCM page cannot visually update, which would lead to stale messages.
			if (!a_isRuntime) {
				if (!newIniKeysElements.empty() || !newIniKeysWidgets.empty() || foundNewWidgetInJson) {
					_iniModifiedThisSession = true;
				}
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

			bool showRestartWarning = _iniModifiedThisSession;

			if (elementsContent) {
				elementsContent->clear();
				if (showRestartWarning) {
					elementsContent->push_back({ { "id", "ElemStatus" }, { "text", "$fzIH_ElementNewFound" }, { "type", "text" } });
				} else {
					elementsContent->push_back({ { "id", "ElemStatus" }, { "text", "<font color='#00FF00'>Status: " + std::to_string(elementsJsonList.size()) + " HUD Elements registered.</font>" },
						{ "type", "text" } });
				}
				elementsContent->push_back({ { "type", "header" } });
				for (const auto& widgetJson : elementsJsonList) {
					elementsContent->push_back(widgetJson);
				}
			}

			if (widgetsContent) {
				widgetsContent->clear();
				if (showRestartWarning) {
					widgetsContent->push_back({ { "id", "WidStatus" }, { "text", "$fzIH_WidgetNewFound" }, { "type", "text" } });
				} else {
					widgetsContent->push_back({ { "id", "WidStatus" }, { "text", "<font color='#00FF00'>Status: " + std::to_string(finalWidgetsMap.size()) + " widgets registered.</font>" },
						{ "type", "text" } });
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
					// Use 2-space indent, nlohmann usually defaults to alpha keys
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
