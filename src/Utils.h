#pragma once

namespace Utils
{
	// Checks if a swf file is on the blocklist
	bool IsIgnoredUrl(const std::string& a_url);

	// Converts "_root.WidgetContainer.10" -> "_root_WidgetContainer_10".
	std::string SanitizeName(const std::string& a_name);

	// Decodes URL encoded strings (e.g. "%20" -> " ").
	std::string UrlDecode(const std::string& a_src);

	// Extract "meter" from "Interface/skyui/meter.swf".
	std::string ExtractFilename(std::string a_path);

	// Central logic to determine the human-readable name for the MCM.
	std::string GetWidgetDisplayName(const std::string& a_source);

	// Checks if a menu name corresponds to a vanilla System Menu (Map, Inventory, etc.)
	bool IsSystemMenu(const std::string& a_menuName);

	// Helper to safely extract the _url member from a MovieView
	std::string GetMenuURL(RE::GPtr<RE::GFxMovieView> a_movie);

	// Checks menu flags to determine if it is an interactive interface (e.g. Loot Menu, Explorer)
	// rather than a passive HUD element.
	bool IsInteractiveMenu(RE::IMenu* a_menu);

	// Helper to get a string representation of menu flags (e.g. "PausesGame | UsesCursor")
	std::string GetMenuFlags(RE::IMenu* a_menu);

	// Log flags (Now uses GetMenuFlags internally)
	void LogMenuFlags(const std::string& a_name, RE::IMenu* a_menu);

	// Registry for Interactive Sources (SWF files detected as interactive)
	// Used to prune config entries even if the menu is closed.
	void RegisterInteractiveSource(const std::string& a_source);
	bool IsSourceInteractive(const std::string& a_source);

	// Shared logic for scanning SkyUI Widget Containers.
	void ScanArrayContainer(const std::string& a_path, const RE::GFxValue& a_container, int& a_foundCount, bool& a_changes);

	// Dumps the structure of a GFxObject to the log.
	class DebugVisitor : public RE::GFxValue::ObjectVisitor
	{
	public:
		DebugVisitor(std::string a_prefix, int a_depth);
		void Visit(const char* a_name, const RE::GFxValue& a_val) override;

	private:
		std::string _prefix;
		int _depth;
	};

	// Scans a GFxObject for DisplayObjects (widgets) and registers them with Settings.
	class ContainerDiscoveryVisitor : public RE::GFxValue::ObjectVisitor
	{
	public:
		// Depth default set to 2 to allow entry into HUDMovieBaseInstance -> Children
		ContainerDiscoveryVisitor(int& a_count, bool& a_changes, std::string a_pathPrefix, int a_depth = 2);
		void Visit(const char* a_name, const RE::GFxValue& a_val) override;

	private:
		int& _count;
		bool& _changes;
		std::string _pathPrefix;
		int _depth;
	};
}
