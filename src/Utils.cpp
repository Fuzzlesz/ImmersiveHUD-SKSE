#include "HUDManager.h"
#include "Settings.h"
#include "Utils.h"

namespace Utils
{
	// Blocklist to help against recursion and snagging junk/crashing.
	static const std::unordered_set<std::string> kDiscoveryBlockList = {
		"markerData",
		"widgetLoaderContainer",
		"aCompassMarkerList",
		"HUDHooksContainer",
		"HudElements"
	};

	// Registry to track SWF files known to be interactive interfaces.
	// This allows MCMGen to prune them from the config even if the menu is closed.
	static std::unordered_set<std::string> g_interactiveSources;
	static std::mutex g_interactiveSourceLock;

	// ==========================================
	// String & Path Helpers
	// ==========================================

	std::string SanitizeName(const std::string& a_name)
	{
		std::string clean = a_name;
		for (char& c : clean) {
			if (!isalnum(static_cast<unsigned char>(c))) {
				c = '_';
			}
		}
		return clean;
	}

	std::string UrlDecode(const std::string& a_src)
	{
		std::string ret;
		ret.reserve(a_src.length());
		unsigned int ii;

		for (size_t i = 0; i < a_src.length(); i++) {
			if (a_src[i] == '%') {
				if (i + 2 < a_src.length()) {
					if (sscanf_s(a_src.substr(i + 1, 2).c_str(), "%x", &ii) != EOF) {
						ret += static_cast<char>(ii);
						i = i + 2;
						continue;
					}
				}
			}
			ret += (a_src[i] == '+') ? ' ' : a_src[i];
		}
		return ret;
	}

	std::string ExtractFilename(std::string a_path)
	{
		if (a_path.empty()) {
			return "";
		}

		std::replace(a_path.begin(), a_path.end(), '\\', '/');

		size_t lastSlash = a_path.rfind('/');
		if (lastSlash != std::string::npos) {
			a_path = a_path.substr(lastSlash + 1);
		}

		size_t lastDot = a_path.rfind('.');
		if (lastDot != std::string::npos) {
			a_path = a_path.substr(0, lastDot);
		}

		if (!a_path.empty()) {
			// Only fix purely lowercase strings.
			bool hasUpper = std::any_of(a_path.begin(), a_path.end(), [](unsigned char c) {
				return std::isupper(c);
			});

			if (!hasUpper) {
				a_path[0] = static_cast<char>(toupper(static_cast<unsigned char>(a_path[0])));
			}
		}

		return a_path;
	}

	std::string GetWidgetDisplayName(const std::string& a_source)
	{
		// Extract the clean name from the Source URL (e.g., "meter.swf" -> "Meter").
		// We rely on the fact that ScanArrayContainer and GetMenuURL ensure a_source is never empty.
		std::string name = ExtractFilename(a_source);
		return name;
	}

	// ==========================================
	// Menu & URL Logic
	// ==========================================

	bool IsSystemMenu(const std::string& a_menuName)
	{
		// Fader Menu excluded to preserve vanilla fade timing
		static const std::unordered_set<std::string> systemMenus = {
			"BarterMenu", "Book Menu", "Console", "Console Native UI Menu",
			"ContainerMenu", "Crafting Menu", "Creation Club Menu", "Credits Menu",
			"Cursor Menu", "Dialogue Menu", "FavoritesMenu", "GiftMenu",
			"InventoryMenu", "Journal Menu", "Kinect Menu", "LevelUp Menu",
			"Loading Menu", "LoadWaitSpinner", "Lockpicking Menu", "MagicMenu",
			"Main Menu", "MapMenu", "MessageBoxMenu", "Mist Menu",
			"Mod Manager Menu", "RaceSex Menu", "SafeZoneMenu", "Sleep/Wait Menu",
			"TitleSequence Menu", "Training Menu", "Tutorial Menu", "TweenMenu"
		};
		return systemMenus.contains(a_menuName);
	}

	std::string GetMenuURL(RE::GPtr<RE::GFxMovieView> a_movie)
	{
		if (!a_movie) {
			return "Unknown";
		}
		RE::GFxValue root;
		if (a_movie->GetVariable(&root, "_root")) {
			RE::GFxValue urlVal;
			if (root.GetMember("_url", &urlVal) && urlVal.IsString()) {
				return urlVal.GetString();
			}
		}
		return "Unknown";
	}

	// ==========================================
	// Interactive Menu Heuristics
	// ==========================================

	bool IsInteractiveMenu(RE::IMenu* a_menu)
	{
		if (!a_menu) {
			return false;
		}

		using Flag = RE::UI_MENU_FLAGS;
		auto flags = a_menu->menuFlags;

		return flags.any(
			Flag::kPausesGame,
			Flag::kUsesCursor,
			Flag::kUsesMenuContext);
	}

	void RegisterInteractiveSource(const std::string& a_source)
	{
		if (a_source.empty() || a_source == "Unknown")
			return;
		std::lock_guard<std::mutex> lock(g_interactiveSourceLock);
		g_interactiveSources.insert(a_source);
	}

	bool IsSourceInteractive(const std::string& a_source)
	{
		if (a_source.empty())
			return false;
		std::lock_guard<std::mutex> lock(g_interactiveSourceLock);
		return g_interactiveSources.contains(a_source);
	}

	std::string GetMenuFlags(RE::IMenu* a_menu)
	{
		if (!a_menu) {
			return "None";
		}

		using Flag = RE::UI_MENU_FLAGS;
		auto flags = a_menu->menuFlags;

		std::vector<std::string> activeFlags;

		// Primary heuristic flags (The ones we ignore)
		if (flags.all(Flag::kPausesGame))
			activeFlags.push_back("PausesGame");
		if (flags.all(Flag::kUsesCursor))
			activeFlags.push_back("UsesCursor");
		if (flags.all(Flag::kUsesMenuContext))
			activeFlags.push_back("UsesMenuContext");

		// Secondary flags (Informational)
		if (flags.all(Flag::kAllowSaving))
			activeFlags.push_back("AllowSaving");
		if (flags.all(Flag::kAlwaysOpen))
			activeFlags.push_back("AlwaysOpen");
		if (flags.all(Flag::kApplicationMenu))
			activeFlags.push_back("ApplicationMenu");
		if (flags.all(Flag::kAssignCursorToRenderer))
			activeFlags.push_back("AssignCursorToRenderer");
		if (flags.all(Flag::kCustomRendering))
			activeFlags.push_back("CustomRendering");
		if (flags.all(Flag::kDisablePauseMenu))
			activeFlags.push_back("DisablePauseMenu");
		if (flags.all(Flag::kHasButtonBar))
			activeFlags.push_back("HasButtonBar");
		if (flags.all(Flag::kInventoryItemMenu))
			activeFlags.push_back("InventoryItemMenu");
		if (flags.all(Flag::kModal))
			activeFlags.push_back("Modal");
		if (flags.all(Flag::kRendersOffscreenTargets))
			activeFlags.push_back("RendersOffscreen");
		if (flags.all(Flag::kRendersUnderPauseMenu))
			activeFlags.push_back("RendersUnderPauseMenu");
		if (flags.all(Flag::kTopmostRenderedMenu))
			activeFlags.push_back("Topmost");
		if (flags.all(Flag::kUsesMovementToDirection))
			activeFlags.push_back("UsesMovementToDirection");

		if (activeFlags.empty()) {
			return "None";
		}

		std::string flagStr;
		for (size_t i = 0; i < activeFlags.size(); ++i) {
			flagStr += activeFlags[i];
			if (i < activeFlags.size() - 1)
				flagStr += " | ";
		}
		return flagStr;
	}

	void LogMenuFlags(const std::string& a_name, RE::IMenu* a_menu)
	{
		// Added check for debug setting
		if (!Settings::GetSingleton()->IsMenuFlagLoggingEnabled()) {
			return;
		}

		if (!a_menu)
			return;

		// Guard: Only log each menu once per session to prevent spam
		static std::unordered_set<std::string> _loggedMenus;
		if (_loggedMenus.contains(a_name)) {
			return;
		}
		_loggedMenus.insert(a_name);

		std::string flagStr = GetMenuFlags(a_menu);

		bool isInteractive = IsInteractiveMenu(a_menu);
		std::string logPrefix = isInteractive ? "Ignoring Interactive Menu" : "Analyzing Menu";

		logger::info("{}: '{}'. Flags: [{}]", logPrefix, a_name, flagStr);
	}

	// ==========================================
	// Widget Scanning Logic
	// ==========================================

	void ScanArrayContainer(const std::string& a_path, const RE::GFxValue& a_container, int& a_foundCount, bool& a_changes)
	{
		for (int i = 0; i < 128; i++) {
			RE::GFxValue entry;
			std::string indexStr = std::to_string(i);

			if (!const_cast<RE::GFxValue&>(a_container).GetMember(indexStr.c_str(), &entry)) {
				continue;
			}
			if (!entry.IsObject()) {
				continue;
			}

			RE::GFxValue widget;
			if (!entry.GetMember("widget", &widget)) {
				if (entry.IsDisplayObject()) {
					widget = entry;
				} else {
					continue;
				}
			}
			if (!widget.IsDisplayObject()) {
				continue;
			}

			std::string widgetPath = a_path + "." + indexStr;
			std::string url = "Internal/SkyUI Widget";

			RE::GFxValue urlVal;
			if (widget.GetMember("_url", &urlVal) && urlVal.IsString()) {
				url = urlVal.GetString();
			}

			// Always increment count for valid widgets, whether new or old
			a_foundCount++;

			if (Settings::GetSingleton()->AddDiscoveredPath(widgetPath, url)) {
				a_changes = true;
				logger::info("Discovered SkyUI Widget: {} [Source: {}]", widgetPath, url);
			}
		}
	}

	// ==========================================
	// DebugVisitor
	// ==========================================

	DebugVisitor::DebugVisitor(std::string a_prefix, int a_depth) :
		_prefix(std::move(a_prefix)),
		_depth(a_depth)
	{}

	void DebugVisitor::Visit(const char* a_name, const RE::GFxValue& a_val)
	{
		if (!a_name) {
			return;
		}
		std::string name(a_name);

		if (kDiscoveryBlockList.contains(name) || name.starts_with("instance")) {
			return;
		}

		// Only log DisplayObjects with detailed alpha/visible information
		if (a_val.IsDisplayObject()) {
			std::string sourceInfo;
			RE::GFxValue urlVal;

			// We need mutable access to call GetMember
			auto& obj = const_cast<RE::GFxValue&>(a_val);

			if (obj.GetMember("_url", &urlVal) && urlVal.IsString()) {
				sourceInfo = "[Source: " + std::string(urlVal.GetString()) + "] ";
			}

			// Get Alpha
			RE::GFxValue alphaVal;
			double alpha = 0.0;
			if (obj.GetMember("_alpha", &alphaVal) && alphaVal.IsNumber()) {
				alpha = alphaVal.GetNumber();
			}

			// Get Visible
			RE::GFxValue visVal;
			std::string visStr = "?";
			if (obj.GetMember("_visible", &visVal) && visVal.IsBool()) {
				visStr = visVal.GetBool() ? "TRUE" : "FALSE";
			}

			// Log format: [Source] [DisplayObject] [A=000.0] [V=TRUE] Path
			logger::info("{}[DisplayObject] [A={:05.1f}] [V={}] {}.{}", sourceInfo, alpha, visStr, _prefix, name);
		}

		// Recurse into DisplayObjects and Arrays only (not generic Objects)
		if (_depth > 0 && (a_val.IsDisplayObject() || a_val.IsArray())) {
			DebugVisitor subVisitor(_prefix + "." + name, _depth - 1);
			const_cast<RE::GFxValue&>(a_val).VisitMembers(&subVisitor);
		}
	}

	// ==========================================
	// ContainerDiscoveryVisitor
	// ==========================================

	ContainerDiscoveryVisitor::ContainerDiscoveryVisitor(int& a_count, bool& a_changes, std::string a_pathPrefix, int a_depth) :
		_count(a_count),
		_changes(a_changes),
		_pathPrefix(std::move(a_pathPrefix)),
		_depth(a_depth)
	{}

	void ContainerDiscoveryVisitor::Visit(const char* a_name, const RE::GFxValue& a_val)
	{
		if (!a_name) {
			return;
		}
		std::string name(a_name);

		// Use the general blocklist
		if (kDiscoveryBlockList.contains(name)) {
			return;
		}

		std::string currentPath = _pathPrefix + "." + name;

		// Special handling for SkyUI WidgetContainer
		if (name == "WidgetContainer") {
			ScanArrayContainer(currentPath, a_val, _count, _changes);
			return;
		}

		// Check if this is a discoverable widget
		if (a_val.IsDisplayObject()) {
			RE::GFxValue urlVal;
			if (const_cast<RE::GFxValue&>(a_val).GetMember("_url", &urlVal) && urlVal.IsString()) {
				std::string url = urlVal.GetString();
				std::string lowerUrl = url;
				std::transform(lowerUrl.begin(), lowerUrl.end(), lowerUrl.begin(), ::tolower);

				// Exclude Compass Navigation Overhaul compass for harmless settings conflict.
				// QuestItemList visibility is tied to the compass already, we don't want control.
				if (lowerUrl.find("compass.swf") != std::string::npos ||
					lowerUrl.find("questitemlist.swf") != std::string::npos) {
					return;
				}

				// If it's not the vanilla HUD, add it to settings.
				bool isVanilla = (lowerUrl.find("hudmenu.swf") != std::string::npos);

				if (!isVanilla) {
					// Always increment found count for population check
					_count++;
					if (Settings::GetSingleton()->AddDiscoveredPath(currentPath, url)) {
						_changes = true;
						logger::info("Discovered External Element: {} [Source: {}]", currentPath, url);
					}
					// Don't recurse into discovered external widgets
					return;
				}

				// If we are here, it is a Vanilla object.
				if (name != "HUDMovieBaseInstance") {
					return;
				}
			}
		}

		// Recurse into DisplayObjects and Arrays only (not generic Objects)
		if (_depth > 0 && (a_val.IsDisplayObject() || a_val.IsArray())) {
			ContainerDiscoveryVisitor subVisitor(_count, _changes, currentPath, _depth - 1);
			const_cast<RE::GFxValue&>(a_val).VisitMembers(&subVisitor);
		}
	}
}
