#include "Compat.h"
#include "HUDElements.h"
#include "HUDManager.h"
#include "MCMGen.h"
#include "Settings.h"
#include "Utils.h"

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

	// Helper to track settings that might be lost due to ID changes
	struct OrphanSetting
	{
		std::string id;
		std::string source;
		long value;
	};

	void ResetSessionFlag()
	{
		_iniModifiedThisSession = false;
	}

	// ==========================================
	// Main Update Loop
	// ==========================================

	void Update(bool a_isRuntime, bool a_widgetsPopulated)
	{
		const fs::path configDir = "Data/MCM/Config/ImmersiveHUD";
		const fs::path configPath = configDir / "config.json";
		const fs::path iniPath = configDir / "settings.ini";

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

			// 2. Prepare Data Sets
			std::map<std::string, std::string> allPaths;

			// Track IDs present in the previous session's JSON
			std::unordered_set<std::string> previousJsonIDs;
			// Track Sources present in the previous session's JSON (to detect index shifts)
			std::unordered_set<std::string> previousJsonSources;

			// Harvest potential orphans (current settings in the JSON)
			// We map Source -> List of Orphans
			std::map<std::string, std::vector<OrphanSetting>> potentialOrphans;

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

			// Source Collision Prep:
			// Build a set of all Source Files currently loaded in memory.
			std::unordered_set<std::string> activeSources;
			for (const auto& path : activePaths) {
				activeSources.insert(settings->GetWidgetSource(path));
			}

			// 3. Recover & Prune Existing Entries
			for (const auto& page : config["pages"]) {
				std::string pName = page.value("pageDisplayName", "");

				// Scan Widget page to recover IDs
				if (pName == "$fzIH_PageWidgets" && page.contains("content")) {
					for (const auto& item : page["content"]) {
						if (item.contains("help")) {
							std::string help = item["help"].get<std::string>();
							std::string idStr = item["id"].get<std::string>();  // e.g. "iMode_Foo:Widgets"

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
								// Register the source so we know this mod was already installed
								previousJsonSources.insert(sourceStr);

								// Harvest Orphan Candidate
								// We grab the INI key from the JSON ID (strip ":Widgets")
								std::string iniKey = idStr.substr(0, idStr.find(':'));
								long val = ini.GetLongValue("Widgets", iniKey.c_str(), -1);

								// Only store if it's a valid, non-default setting (1=Immersive is default)
								if (val != -1 && val != 1) {
									potentialOrphans[sourceStr].push_back({ rawID, sourceStr, val });
								}

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
								bool isVanilla = hardcodedVanillaPaths.contains(rawID);
								bool existsInMemory = activePaths.contains(rawID);

								// Identify if this is a System Menu that should be pruned (e.g. Fader Menu)
								// Fader Menu is explicitly checked because Utils::IsSystemMenu excludes it for logic reasons elsewhere.
								bool isSystemMenu = (rawID == "Fader Menu" || Utils::IsSystemMenu(rawID));

								// Source Collision Logic:
								// If the source file is currently loaded in memory (activeSources),
								// BUT this specific ID (rawID) is NOT in memory, it implies this ID is stale.
								// This catches:
								// 1. SkyUI Widget Position Jostling (WidgetContainer.5 moved to WidgetContainer.3)
								// 2. Versioned IDs (Menu_v1 replaced by Menu_v2)
								bool isStaleID = !existsInMemory && activeSources.contains(sourceStr);

								bool isInteractivePrune = false;
								if (!existsInMemory) {
									if (auto activeMenu = RE::UI::GetSingleton()->GetMenu(rawID)) {
										if (Utils::IsInteractiveMenu(activeMenu.get())) {
											isInteractivePrune = true;
										}
									}
								}
								if (!existsInMemory && !isInteractivePrune) {
									if (Utils::IsSourceInteractive(sourceStr)) {
										isInteractivePrune = true;
									}
								}

								bool shouldKeep = false;

								if (existsInMemory || isVanilla) {
									shouldKeep = true;
								} else if (isSystemMenu) {
									shouldKeep = false;
									logger::info("Pruning system menu from config: {}", rawID);
								} else if (isInteractivePrune) {
									shouldKeep = false;
									logger::info("Pruning interactive menu from config: {}", rawID);
								} else if (isStaleID) {
									// It's definitely dead. The file is loaded elsewhere, so this specific ID is invalid.
									shouldKeep = false;
								} else {
									// The source isn't loaded at all (Menu is closed).
									// Fall back to physical file check via BSResources.
									shouldKeep = WidgetSourceExists(sourceStr);
								}

								if (shouldKeep) {
									allPaths[rawID] = sourceStr;
								} else {
									logger::info("Pruning uninstalled widget: {} [Source: {}]", rawID, sourceStr);
								}
							}
						}
					}
				}
			}

			// 4. Merge New Discoveries
			bool foundNewWidgetInJson = false;
			auto memPaths = settings->GetSubWidgetPaths();

			for (const auto& path : memPaths) {
				std::string src = settings->GetWidgetSource(path);

				// If we are at the Main Menu (!a_widgetsPopulated), SkyUI widgets cannot physically exist.
				// Any entries appearing in memPaths are leftovers from the INI cache.
				// We must ignore them to prevent false "New Found" flags due to index shifting.
				bool isSkyUIWidget = (path.find("_root.WidgetContainer.") != std::string::npos);
				if (isSkyUIWidget && !a_widgetsPopulated) {
					continue;
				}

				allPaths[path] = src;

				// Detection Logic: Was this widget missing from the previous config?
				if (!previousJsonIDs.contains(path)) {
					if (hardcodedVanillaPaths.contains(path)) {
						continue;
					}

					// Check if the Source was already present (Index Shift vs New Mod).
					// If the source exists but the specific ID changed (e.g. .19 -> .22), it is an index shift.
					// This does not require a restart because settings are resolved via Source.
					if (previousJsonSources.contains(src)) {
						logger::info("Widget index shift detected: {} [Source: {}]. Updating config without status change.", path, src);
					} else {
						logger::info("New widget detected: {} [Source: {}]", path, src);
						foundNewWidgetInJson = true;
					}
				}
			}

			// 5. Generate Content for "HUD Elements" Page
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

			// 6. Generate Content for "Widgets" Page (Dynamic)
			std::map<std::string, std::vector<WidgetInfo>> groupedWidgets;
			for (const auto& [path, source] : allPaths) {
				if (processedPaths.contains(path)) {
					continue;
				}
				std::string pretty = Utils::GetWidgetDisplayName(source);
				groupedWidgets[pretty].push_back({ path, source, pretty });
			}

			std::map<std::string, json> finalWidgetsMap;
			for (auto& [prettyBase, widgets] : groupedWidgets) {
				// We only take the first instance for the MCM setting to avoid clutter
				const auto& w = widgets[0];
				std::string displayName = prettyBase;
				std::string safeID = Utils::SanitizeName(displayName);
				std::string finalID = "iMode_" + safeID + ":Widgets";
				std::string iniKey = "iMode_" + safeID;

				std::string help = "Source: " + w.source + "\nID: " + w.rawPath;
				if (widgets.size() > 1) {
					help += "\n(+ " + std::to_string(widgets.size() - 1) + " other instances)";
				}

				bool existsInIni = iniLoaded && (ini.GetValue("Widgets", iniKey.c_str(), nullptr) != nullptr);

				if (!existsInIni) {
					// SETTING MIGRATION:
					// If this is a new key, check if we have a valid orphan for this source.
					// Since we are grouping instances now, we take the first available orphan value.
					auto& orphans = potentialOrphans[w.source];
					long migratedValue = -1;
					if (!orphans.empty()) {
						migratedValue = orphans[0].value;
						orphans.clear();  // Clear orphans for this source to prevent reuse
					}
					if (migratedValue != -1) {
						ini.SetLongValue("Widgets", iniKey.c_str(), migratedValue);
						logger::info("Migrated setting for {}: {} -> {}", displayName, w.source, migratedValue);
						existsInIni = true;
					}
				}

				if (!existsInIni)
					newIniKeysWidgets.push_back(iniKey);
				finalWidgetsMap[finalID] = CreateEnum(displayName, finalID, help);
			}

			// 7. Calculate Status Flags
			// If new content is discovered during Initial/Mid Scans (!Runtime),
			// we flag the session to display the "Restart Required" warning.
			// Once Runtime is set (post-Mid Scan), we stop triggering this flag
			// as the MCM page cannot visually update, which would lead to stale messages.
			if (!a_isRuntime) {
				if (!newIniKeysElements.empty() || !newIniKeysWidgets.empty() || foundNewWidgetInJson) {
					_iniModifiedThisSession = true;
				}
			}

			// Show restart warning ONLY during non-runtime scans where new content was found
			// Runtime scans always show "registered" count since MCM can't update anyway
			bool showRestartWarning = !a_isRuntime && _iniModifiedThisSession;

			// 8. Inject JSON Content
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

			// Track if status text changed for write decision
			bool statusChanged = false;
			std::string oldWidgetStatus = "";
			std::string oldElementStatus = "";

			// Capture old status before clearing
			if (widgetsContent && !widgetsContent->empty()) {
				if ((*widgetsContent)[0].contains("text")) {
					oldWidgetStatus = (*widgetsContent)[0]["text"].get<std::string>();
				}
			}
			if (elementsContent && !elementsContent->empty()) {
				if ((*elementsContent)[0].contains("text")) {
					oldElementStatus = (*elementsContent)[0]["text"].get<std::string>();
				}
			}

			if (elementsContent) {
				elementsContent->clear();
				std::string newStatus = "<font color='#00FF00'>Status: " +
				                        std::to_string(elementsJsonList.size()) + " HUD Elements registered.</font>";

				if (oldElementStatus != newStatus) {
					statusChanged = true;
				}

				// For Elements, we always show the count. The list is static/hardcoded.
				elementsContent->push_back({ { "id", "ElemStatus" }, { "text", newStatus },
					{ "type", "text" } });
				elementsContent->push_back({ { "type", "header" } });
				for (const auto& widgetJson : elementsJsonList) {
					elementsContent->push_back(widgetJson);
				}
			}

			if (widgetsContent) {
				widgetsContent->clear();
				std::string newStatus;
				if (showRestartWarning) {
					newStatus = "$fzIH_WidgetNewFound";
				} else {
					newStatus = "<font color='#00FF00'>Status: " +
					            std::to_string(finalWidgetsMap.size()) + " widgets registered.</font>";
				}

				if (oldWidgetStatus != newStatus) {
					statusChanged = true;
				}

				widgetsContent->push_back({ { "id", "WidStatus" }, { "text", newStatus }, { "type", "text" } });
				widgetsContent->push_back({ { "type", "header" } });
				for (const auto& [id, widgetJson] : finalWidgetsMap) {
					widgetsContent->push_back(widgetJson);
				}
			}

			// 9. Write to Disk
			// Only write if config changed OR if we have meaningful changes OR status changed
			bool contentChanged = !newIniKeysElements.empty() || !newIniKeysWidgets.empty() || foundNewWidgetInJson;
			bool isInitialCreation = originalConfig.empty() || !originalConfig.contains("pages");
			bool shouldWrite = (config != originalConfig) || contentChanged || statusChanged || isInitialCreation;

			if (shouldWrite) {
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

				ini.SaveFile(iniPath.string().c_str());
			}

			// 10. Update Cache (Anti-Flicker)
			// Persist the discovered paths to the INI so next session
			// we can target them immediately on load.
			Settings::GetSingleton()->SaveCache();

		} catch (...) {
			logger::error("Failed to update MCM JSON");
		}
	}
}
