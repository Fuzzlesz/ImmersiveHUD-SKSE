#include "Utils.h"
#include "Settings.h"

namespace Utils
{
	// Blocklist to help against recursion and snagging junk/crashing.
	// NOTE: Removed "HUDMovieBaseInstance" from here so the Debug Dump can see it.
	// It is now manually blocked in ContainerDiscoveryVisitor instead.
	static const std::unordered_set<std::string> kDiscoveryBlockList = {
		"markerData",
		"widgetLoaderContainer",
		"aCompassMarkerList",
		"HUDHooksContainer"
	};

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

	std::string GetWidgetDisplayName(const std::string& a_rawPath, const std::string& a_source)
	{
		// 1. Try to get a clean name from the Source URL (e.g., "meter.swf" -> "Meter").
		std::string name = ExtractFilename(a_source);

		// 2. If the Source was generic or missing (e.g. "Internal/SkyUI Widget"), we might get "Widget" or "SkyUI Widget".
		// If it's too generic or empty, fall back to parsing the path.
		bool isGeneric = name.empty() || name == "Unknown" || name == "Hudmenu";

		if (isGeneric) {
			// Check if it's a specific SkyUI container.
			if (a_rawPath.find("WidgetContainer") != std::string::npos) {
				// Parse "_root.WidgetContainer.10" -> "SkyUI Widget 10".
				size_t lastDot = a_rawPath.rfind('.');
				if (lastDot != std::string::npos && lastDot + 1 < a_rawPath.length()) {
					return "SkyUI Widget " + a_rawPath.substr(lastDot + 1);
				}
			}

			// Fallback: Use the path filename.
			name = ExtractFilename(a_rawPath);
		}

		// 3. Final safety check.
		if (name.empty()) {
			return "Unknown Widget";
		}

		return name;
	}

	bool IsSystemMenu(const std::string& a_menuName)
	{
		static const std::unordered_set<std::string> systemMenus = {
			"LevelUp Menu", "Credits Menu", "TitleSequence Menu",
			"Dialogue Menu", "Lockpicking Menu", "Creation Club Menu", "LoadWaitSpinner",
			"MessageBoxMenu", "Cursor Menu", "Loading Menu", "Console",
			"Training Menu", "Mist Menu", "Kinect Menu", "TweenMenu",
			"BarterMenu", "Mod Manager Menu", "Journal Menu", "Tutorial Menu",
			"SafeZoneMenu", "MapMenu", "Sleep/Wait Menu", "Console Native UI Menu",
			"Book Menu", "FavoritesMenu", "GiftMenu", "MagicMenu",
			"Crafting Menu", "ContainerMenu", "Main Menu", "InventoryMenu",
			"RaceSex Menu"
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

			if (Settings::GetSingleton()->AddDiscoveredPath(widgetPath, url)) {
				a_changes = true;
				a_foundCount++;
				logger::info("Discovered SkyUI Element: {} [Source: {}]", widgetPath, url);
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

		// Explicitly block the main HUD instance from discovery to avoid duplicating
		// vanilla elements that are already handled by HUDElements::Get().
		// We handle this here specifically so the DebugVisitor (Dump Button) can still see it.
		if (name == "HUDMovieBaseInstance") {
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

				// Skip the HUD menu itself
				if (!url.ends_with("hudmenu.swf") && !url.ends_with("HUDMenu.swf")) {
					if (Settings::GetSingleton()->AddDiscoveredPath(currentPath, url)) {
						_changes = true;
						_count++;
						logger::info("Discovered Element: {} [Source: {}]", currentPath, url);
					}
					// Don't recurse into discovered widgets - we've found what we need
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
