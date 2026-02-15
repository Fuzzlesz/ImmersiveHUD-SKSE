#pragma once

	class Settings : public ISingleton<Settings>
{
public:
	enum WidgetMode
	{
		kVisible = 0,
		kImmersive = 1,
		kHidden = 2,
		kIgnored = 3,
		kInterior = 4,
		kExterior = 5,
		kInCombat = 6,
		kNotInCombat = 7,
		kWeaponDrawn = 8,
		kLockedOn = 9
	};

	struct CrosshairSettings
	{
		bool enabled{ true };
		bool hideWhileAiming{ false };
	};

	struct SneakMeterSettings
	{
		bool enabled{ true };
	};

	void Load();
	void SaveCache();
	void ResetCache();
	void SetDumpHUDEnabled(bool a_enabled);

	[[nodiscard]] bool AddDiscoveredPath(const std::string& a_path, const std::string& a_source = "");

	[[nodiscard]] std::uint32_t GetToggleKey() const { return _toggleKey; }
	[[nodiscard]] bool IsHoldMode() const { return _holdMode; }
	[[nodiscard]] bool IsStartVisible() const { return _startVisible; }
	[[nodiscard]] bool IsAlwaysShowInCombat() const { return _alwaysShowInCombat; }
	[[nodiscard]] bool IsAlwaysShowWeaponDrawn() const { return _alwaysShowWeaponDrawn; }
	[[nodiscard]] float GetFadeInSpeed() const { return _fadeInSpeed; }
	[[nodiscard]] float GetFadeOutSpeed() const { return _fadeOutSpeed; }
	[[nodiscard]] float GetDisplayDuration() const { return _displayDuration; }
	[[nodiscard]] bool IsDumpHUDEnabled() const { return _dumpHUD; }
	[[nodiscard]] bool IsMenuFlagLoggingEnabled() const { return _logMenuFlags; }

	[[nodiscard]] float GetHUDOpacityMin() const { return _hudOpacityMin; }
	[[nodiscard]] float GetHUDOpacityMax() const { return _hudOpacityMax; }
	[[nodiscard]] float GetContextOpacityMin() const { return _contextOpacityMin; }
	[[nodiscard]] float GetContextOpacityMax() const { return _contextOpacityMax; }

	[[nodiscard]] int GetWidgetMode(const std::string& a_rawPath) const;

	[[nodiscard]] const std::set<std::string>& GetSubWidgetPaths() const { return _subWidgetPaths; }
	[[nodiscard]] std::string GetWidgetSource(const std::string& a_path) const;

	[[nodiscard]] const CrosshairSettings& GetCrosshairSettings() const { return _crosshair; }
	[[nodiscard]] const SneakMeterSettings& GetSneakMeterSettings() const { return _sneakMeter; }

private:
	using INIFunc = std::function<void(CSimpleIniA&)>;

	void LoadINI(const fs::path& a_defaultPath, const fs::path& a_userPath, INIFunc a_func);

	const fs::path defaultPath{ "Data/MCM/Config/ImmersiveHUD/settings.ini" };
	const fs::path userPath{ "Data/MCM/Settings/ImmersiveHUD.ini" };
	const fs::path cachePath{ "Data/SKSE/Plugins/ImmersiveHUD_Cache.ini" };

	std::uint32_t _toggleKey = 0x2D;
	bool _holdMode = false;
	bool _startVisible = false;
	bool _alwaysShowInCombat = false;
	bool _alwaysShowWeaponDrawn = false;
	float _fadeInSpeed = 10.0f;
	float _fadeOutSpeed = 5.0f;
	float _displayDuration = 0.0f;
	bool _dumpHUD = false;
	bool _logMenuFlags = false;

	float _hudOpacityMin = 0.0f;
	float _hudOpacityMax = 100.0f;
	float _contextOpacityMin = 0.0f;
	float _contextOpacityMax = 100.0f;

	CrosshairSettings _crosshair;
	SneakMeterSettings _sneakMeter;

	std::map<std::string, int> _widgetPathToMode;
	std::map<std::string, int> _dynamicWidgetModes;
	std::set<std::string> _subWidgetPaths;
	std::map<std::string, std::string> _widgetSources;
};
