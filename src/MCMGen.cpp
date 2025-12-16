#include "MCMGen.h"
#include "Settings.h"
#include "Utils.h"
#include <nlohmann/json.hpp>

using json = nlohmann::json;
namespace fs = std::filesystem;

namespace MCMGen
{
	static std::set<std::string> _sessionStartIDs;
	static bool _sessionInitialized = false;
	static bool _iniModifiedThisSession = false;

	void ResetSession() {}

	bool WidgetSourceExists(const std::string& a_source)
	{
		if (a_source.empty() || a_source == "Unknown") {
			return true;
		}

		std::string path = a_source;
		if (path.starts_with("file:///")) {
			path = path.substr(8);
		}
		std::replace(path.begin(), path.end(), '|', ':');
		std::replace(path.begin(), path.end(), '\\', '/');

		std::string lowerPath = path;
		std::transform(lowerPath.begin(), lowerPath.end(), lowerPath.begin(), ::tolower);

		static const std::vector<std::string> kInternalWhitelist = {
			"internal/stealthmeter",
			"internal/skyui widget",
			"internal/skyui",
			"hudmenu",
			"hudmenu.swf"
		};

		for (const auto& w : kInternalWhitelist) {
			if (lowerPath.find(w) != std::string::npos) {
				return true;
			}
		}

		fs::path dataPath = "Data";
		std::error_code ec;

		if (fs::exists(dataPath / path, ec)) {
			return true;
		}
		if (lowerPath.starts_with("interface/")) {
			if (fs::exists(dataPath / path.substr(10), ec)) {
				return true;
			}
		}

		fs::path filename = fs::path(path).filename();
		if (!filename.empty()) {
			static const std::vector<fs::path> kSearchPaths = {
				"Interface/exported/widgets/skyui",
				"Interface/exported/widgets",
				"Interface"
			};
			for (const auto& searchPath : kSearchPaths) {
				if (fs::exists(dataPath / searchPath / filename, ec)) {
					return true;
				}
			}
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
								  { "options", json::array({ "$fzIH_ModeIgnored", "$fzIH_ModeImmersive", "$fzIH_ModeHidden" }) } } }
		};
	}

	void SmartAppendIni(const std::vector<std::string>& a_newKeys, const fs::path& a_path, CSimpleIniA& a_ini)
	{
		if (a_newKeys.empty()) {
			return;
		}

		bool changed = false;
		const char* section = "Widgets";

		for (const auto& key : a_newKeys) {
			if (a_ini.GetValue(section, key.c_str(), nullptr) == nullptr) {
				a_ini.SetLongValue(section, key.c_str(), 1, nullptr);
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

	void Update(bool a_isRuntime)
	{
		const fs::path configDir = "Data/MCM/Config/ImmersiveHUD";
		const fs::path configPath = configDir / "config.json";
		const fs::path iniPath = configDir / "settings.ini";

		try {
			fs::create_directories(configDir);
		} catch (...) {}

		try {
			// 1. Session Baseline
			if (!_sessionInitialized) {
				if (fs::exists(configPath)) {
					try {
						std::ifstream inFile(configPath);
						json j = json::parse(inFile);
						if (j.contains("pages") && j["pages"].is_array()) {
							for (const auto& page : j["pages"]) {
								if (page.value("pageDisplayName", "") == "$fzIH_PageWidgets" && page.contains("content")) {
									for (const auto& item : page["content"]) {
										if (item.contains("id")) {
											std::string id = item["id"].get<std::string>();
											if (id != "WidStatus") {
												_sessionStartIDs.insert(id);
											}
										}
									}
								}
							}
						}
					} catch (...) {}
				}
				_sessionInitialized = true;
			}

			// 2. Load Configs
			CSimpleIniA ini;
			ini.SetUnicode();
			bool iniLoaded = (ini.LoadFile(iniPath.string().c_str()) >= 0);
			std::vector<std::string> newIniKeys;

			json config;
			bool loadedSkeleton = false;
			if (fs::exists(configPath)) {
				try {
					std::ifstream inFile(configPath);
					config = json::parse(inFile);
					loadedSkeleton = true;
				} catch (...) {
					config = json::object();
				}
			} else {
				config = json::object();
			}

			if (!config.contains("pages") || !config["pages"].is_array()) {
				config["pages"] = json::array();
			}

			// 3. Gather all raw paths
			std::map<std::string, std::string> allPaths;

			// 3a. From Skeleton (Persistence)
			if (loadedSkeleton) {
				for (const auto& page : config["pages"]) {
					// STRICT: Only checks for localization key
					std::string pName = page.value("pageDisplayName", "");
					if (pName == "$fzIH_PageWidgets" && page.contains("content")) {
						for (const auto& item : page["content"]) {
							if (item.contains("help")) {
								std::string help = item["help"].get<std::string>();
								std::string sourceStr = "Unknown";
								std::string rawID = "";

								size_t idPos = help.find("ID: ");
								if (idPos != std::string::npos) {
									rawID = help.substr(idPos + 4);
									if (rawID.find('\n') != std::string::npos) {
										rawID = rawID.substr(0, rawID.find('\n'));
									}
								}

								if (!rawID.empty()) {
									if (help.starts_with("Source: ")) {
										size_t endPos = help.find('\n');
										if (endPos != std::string::npos) {
											sourceStr = help.substr(8, endPos - 8);
										}
									}
									sourceStr = Utils::UrlDecode(sourceStr);
									if (WidgetSourceExists(sourceStr)) {
										allPaths[rawID] = sourceStr;
									}
								}
							}
						}
					}
				}
			}

			// 3b. From Memory (Live Discovery)
			const auto settings = Settings::GetSingleton();
			auto memPaths = settings->GetSubWidgetPaths();
			for (const auto& path : memPaths) {
				std::string src = Utils::UrlDecode(settings->GetWidgetSource(path));
				allPaths[path] = src;
			}

			// 4. Group by pretty name
			std::map<std::string, std::vector<WidgetInfo>> groupedWidgets;
			for (const auto& [path, source] : allPaths) {
				std::string pretty = Utils::GetWidgetDisplayName(path, source);
				groupedWidgets[pretty].push_back({ path, source, pretty });
			}

			// 5. Generate Widgets
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
							newIniKeys.push_back(iniKey);
						}
					} else {
						newIniKeys.push_back(iniKey);
					}
					finalWidgetsMap[finalID] = CreateEnum(displayName, finalID, help);
				}
			}

			// 6. Calculate Status
			std::vector<std::string> addedIDs;
			for (const auto& [id, _] : finalWidgetsMap) {
				if (_sessionStartIDs.find(id) == _sessionStartIDs.end()) {
					addedIDs.push_back(id);
				}
			}

			bool relaunchNeeded = (!newIniKeys.empty() || _iniModifiedThisSession || !addedIDs.empty());

			// 7. Inject into JSON
			json* widgetsContent = nullptr;
			for (auto& page : config["pages"]) {
				if (page.value("pageDisplayName", "") == "$fzIH_PageGeneral") {
					for (auto& item : page["content"]) {
						if (item.contains("id")) {
							std::string id = item["id"].get<std::string>();
							if (id.find(":Settings") != std::string::npos) {
								std::string newId = id.substr(0, id.find(":Settings")) + ":HUD";
								item["id"] = newId;
							}
						}
					}
				}

				if (page.value("pageDisplayName", "") == "$fzIH_PageWidgets") {
					widgetsContent = &page["content"];
				}
			}

			if (widgetsContent) {
				widgetsContent->clear();
				if (relaunchNeeded && !a_isRuntime) {
					widgetsContent->push_back({ { "text", "$fzIH_WidgetNewFound" }, { "type", "text" }, { "id", "WidStatus" } });
				} else {
					widgetsContent->push_back({ { "text", "<font color='#00FF00'>Status: " + std::to_string(finalWidgetsMap.size()) + " widgets registered.</font>" },
						{ "type", "text" }, { "id", "WidStatus" } });
				}
				widgetsContent->push_back({ { "type", "header" } });  // Empty header acts as a spacer
				for (const auto& [id, widgetJson] : finalWidgetsMap) {
					widgetsContent->push_back(widgetJson);
				}
			}

			std::ofstream outFile(configPath, std::ios::trunc);
			if (outFile.is_open()) {
				outFile << config.dump(2);
				outFile.close();
			}

			if (!newIniKeys.empty()) {
				SmartAppendIni(newIniKeys, iniPath, ini);
			}

		} catch (...) {
			logger::error("Failed to update MCM JSON");
		}
	}
}
