#include "Compat.h"
#include "HUDManager.h"
#include "Settings.h"
#include "Utils.h"

namespace Utils
{
	// Blocklist to help against recursion and snagging junk/crashing.
	static const std::unordered_set<std::string_view> kDiscoveryBlockList = {
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

	// Hardcoded blocked swf files.
	bool IsIgnoredUrl(std::string_view a_url)
	{
		// Use thread_local buffer to avoid repeated allocations
		thread_local std::string lowerUrl;
		lowerUrl.assign(a_url);
		std::transform(lowerUrl.begin(), lowerUrl.end(), lowerUrl.begin(), ::tolower);

		// Exclude Compass Navigation Overhaul compass for harmless settings conflict.
		// QuestItemList visibility is tied to the compass already, we don't want control.
		if (lowerUrl.find("compass.swf") != std::string::npos ||
			lowerUrl.find("questitemlist.swf") != std::string::npos ||

			// Exclude moreHUD icon swf that we don't need spamming logs/cache.
			lowerUrl.find("baseicons.swf") != std::string::npos ||

			// Exclude Sneak Vignette mod to avoid issues.
			lowerUrl.find("sneakvignette.swf") != std::string::npos ||
			lowerUrl.find("sneakvignettedummy.swf") != std::string::npos) {
			return true;
		}

		return false;
	}

	// ==========================================
	// String & Path Helpers
	// ==========================================

	std::string SanitizeName(std::string_view a_name)
	{
		std::string clean(a_name);
		for (char& c : clean) {
			if (!isalnum(static_cast<unsigned char>(c))) {
				c = '_';
			}
		}
		return clean;
	}

	std::string UrlDecode(std::string_view a_src)
	{
		std::string ret;
		ret.reserve(a_src.length());
		unsigned int ii;

		for (size_t i = 0; i < a_src.length(); i++) {
			if (a_src[i] == '%') {
				if (i + 2 < a_src.length()) {
					// Need a temporary string for sscanf if using string_view source
					char hex[3] = { a_src[i + 1], a_src[i + 2], '\0' };
					if (sscanf_s(hex, "%x", &ii) != EOF) {
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

	std::string ExtractFilename(std::string_view a_path)
	{
		if (a_path.empty()) {
			return "";
		}

		// Work with view first to identify bounds
		std::string_view p = a_path;

		// 1. Handle directory separators
		// find_last_of handles both / and \ so manual replacement is not needed
		size_t lastSlash = p.find_last_of("/\\");
		if (lastSlash != std::string_view::npos) {
			p = p.substr(lastSlash + 1);
		}

		// 2. Handle extensions
		size_t lastDot = p.rfind('.');
		if (lastDot != std::string_view::npos) {
			p = p.substr(0, lastDot);
		}

		if (p.empty()) {
			return "";
		}

		// 3. Construct result and fix case
		std::string result(p);

		// Only fix purely lowercase strings.
		bool hasUpper = std::any_of(result.begin(), result.end(), [](unsigned char c) {
			return std::isupper(c);
		});

		if (!hasUpper) {
			result[0] = static_cast<char>(toupper(static_cast<unsigned char>(result[0])));
		}

		return result;
	}

	std::string GetWidgetDisplayName(std::string_view a_source)
	{
		// Extract the clean name from the Source URL (e.g., "meter.swf" -> "Meter").
		// We rely on the fact that ScanArrayContainer and GetMenuURL ensure a_source is never empty.
		std::string name = ExtractFilename(a_source);
		return name;
	}

	// ==========================================
	// Menu & URL Logic
	// ==========================================

	bool IsSystemMenu(std::string_view a_menuName)
	{
		// Fader Menu excluded to preserve vanilla fade timing
		static const std::unordered_set<std::string_view> systemMenus = {
			"BarterMenu", "Book Menu", "Console", "Console Native UI Menu",
			"ContainerMenu", "Crafting Menu", "Credits Menu",
			"Cursor Menu", "Dialogue Menu", "FavoritesMenu", "GiftMenu",
			"InventoryMenu", "Journal Menu", "Kinect Menu", "LevelUp Menu",
			"Loading Menu", "LoadWaitSpinner", "Lockpicking Menu", "Login Menu", "MagicMenu",
			"Main Menu", "MapMenu", "Marketplace Menu", "MessageBoxMenu", "Mist Menu",
			"Mod Manager Menu", "PluginExplorerMenu", "RaceSex Menu", "SafeZoneMenu", "Sleep/Wait Menu",
			"StatsMenu", "TitleSequence Menu", "Training Menu", "Tutorial Menu", "TweenMenu"
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

		std::ostringstream oss;
		bool first = true;

		auto checkFlag = [&](Flag f, const char* name) {
			if (flags.all(f)) {
				if (!first)
					oss << " | ";
				oss << name;
				first = false;
			}
		};

		// Primary heuristic flags (The ones we ignore)
		checkFlag(Flag::kPausesGame, "PausesGame");
		checkFlag(Flag::kUsesCursor, "UsesCursor");
		checkFlag(Flag::kUsesMenuContext, "UsesMenuContext");

		// Secondary flags (Informational)
		checkFlag(Flag::kAllowSaving, "AllowSaving");
		checkFlag(Flag::kAlwaysOpen, "AlwaysOpen");
		checkFlag(Flag::kApplicationMenu, "ApplicationMenu");
		checkFlag(Flag::kAssignCursorToRenderer, "AssignCursorToRenderer");
		checkFlag(Flag::kCustomRendering, "CustomRendering");
		checkFlag(Flag::kDisablePauseMenu, "DisablePauseMenu");
		checkFlag(Flag::kHasButtonBar, "HasButtonBar");
		checkFlag(Flag::kInventoryItemMenu, "InventoryItemMenu");
		checkFlag(Flag::kModal, "Modal");
		checkFlag(Flag::kRendersOffscreenTargets, "RendersOffscreen");
		checkFlag(Flag::kRendersUnderPauseMenu, "RendersUnderPauseMenu");
		checkFlag(Flag::kTopmostRenderedMenu, "Topmost");
		checkFlag(Flag::kUsesMovementToDirection, "UsesMovementToDirection");

		if (first)
			return "None";
		return oss.str();
	}

	void LogMenuFlags(std::string_view a_name, RE::IMenu* a_menu)
	{
		// Added check for debug setting
		if (!Settings::GetSingleton()->IsMenuFlagLoggingEnabled()) {
			return;
		}

		if (!a_menu)
			return;

		// Guard: Only log each menu once per session to prevent spam
		static std::unordered_set<std::string> _loggedMenus;
		std::string nameStr(a_name);

		if (_loggedMenus.contains(nameStr)) {
			return;
		}
		_loggedMenus.insert(nameStr);

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

			if (const_cast<RE::GFxValue&>(a_val).GetMember("_url", &urlVal) && urlVal.IsString()) {
				sourceInfo = "[Source: " + std::string(urlVal.GetString()) + "] ";
			}

			// Get Alpha & Visible
			RE::GFxValue::DisplayInfo dInfo;
			const_cast<RE::GFxValue&>(a_val).GetDisplayInfo(&dInfo);

			double alpha = dInfo.GetAlpha();
			std::string visStr = dInfo.GetVisible() ? "TRUE" : "FALSE";

			// Log format: [Source] [DisplayObject] [A=000.0] [V=TRUE] Path
			logger::info("{}[DisplayObject] [A={:05.1f}] [V={}] {}.{}", sourceInfo, alpha, visStr, _prefix, name);
		}

		// Recurse into DisplayObjects and Arrays only (not generic Objects)
		if (_depth > 0 && (a_val.IsDisplayObject() || a_val.IsArray())) {
			DebugVisitor subVisitor(_prefix + "." + name, _depth - 1);
			a_val.VisitMembers([&subVisitor](const char* name, const RE::GFxValue& val) {
				subVisitor.Visit(name, val);
			});
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

		// Use the general blocklist and ignore auto-generated flash instances
		if (kDiscoveryBlockList.contains(name) || name.starts_with("instance")) {
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

				if (IsIgnoredUrl(url)) {
					return;
				}

				std::string lowerUrl = url;
				std::transform(lowerUrl.begin(), lowerUrl.end(), lowerUrl.begin(), ::tolower);

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

				// INPA SEKIRO FIX: Inpa Sekiro Combat heavily modifies HUDMovieBaseInstance.
				// Calling VisitMembers on it causes a crash. We skip deep scanning it.
				// Direct path access (GetVariable) for hardcoded elements still works fine.
				if (Compat::GetSingleton()->IsInpaSekiroCombatLoaded()) {
					return;
				}
			}
		}

		// Recurse into DisplayObjects and Arrays only (not generic Objects)
		if (_depth > 0 && (a_val.IsDisplayObject() || a_val.IsArray())) {
			ContainerDiscoveryVisitor subVisitor(_count, _changes, currentPath, _depth - 1);
			a_val.VisitMembers([&subVisitor](const char* name, const RE::GFxValue& val) {
				subVisitor.Visit(name, val);
			});
		}
	}
}
