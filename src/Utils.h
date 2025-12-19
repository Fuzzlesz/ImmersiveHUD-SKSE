#pragma once

namespace Utils
{
	// Converts "_root.WidgetContainer.10" -> "_root_WidgetContainer_10".
	std::string SanitizeName(const std::string& a_name);

	// Decodes URL encoded strings (e.g. "%20" -> " ").
	std::string UrlDecode(const std::string& a_src);

	// Extract "meter" from "Interface/skyui/meter.swf".
	std::string ExtractFilename(std::string a_path);

	// Central logic to determine the human-readable name for the MCM.
	std::string GetWidgetDisplayName(const std::string& a_rawPath, const std::string& a_source);

	// Checks if a menu name corresponds to a vanilla System Menu (Map, Inventory, etc.)
	bool IsSystemMenu(const std::string& a_menuName);

	// Helper to safely extract the _url member from a MovieView
	std::string GetMenuURL(RE::GPtr<RE::GFxMovieView> a_movie);

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
		ContainerDiscoveryVisitor(int& a_count, bool& a_changes, std::string a_pathPrefix, int a_depth = 0);
		void Visit(const char* a_name, const RE::GFxValue& a_val) override;

	private:
		int& _count;
		bool& _changes;
		std::string _pathPrefix;
		int _depth;
	};
}
