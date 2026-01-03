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
	};

	struct SneakMeterSettings
	{
		bool enabled{ true };
	};

	void Load();
	void ResetCache();

	[[nodiscard]] bool AddDiscoveredPath(const std::string& a_path, const std::string& a_source = "");

	[[nodiscard]] std::uint32_t GetToggleKey() const { return _toggleKey; }
	[[nodiscard]] bool IsHoldMode() const { return _holdMode; }
	[[nodiscard]] bool IsStartVisible() const { return _startVisible; }
	[[nodiscard]] bool IsAlwaysShowInCombat() const { return _alwaysShowInCombat; }
	[[nodiscard]] bool IsAlwaysShowWeaponDrawn() const { return _alwaysShowWeaponDrawn; }
	[[nodiscard]] float GetFadeSpeed() const { return _fadeSpeed; }
	[[nodiscard]] float GetDisplayDuration() const { return _displayDuration; }
	[[nodiscard]] bool IsDumpHUDEnabled() const { return _dumpHUD; }

	[[nodiscard]] int GetWidgetMode(const std::string& a_rawPath) const;

	[[nodiscard]] const std::set<std::string>& GetSubWidgetPaths() const;
	[[nodiscard]] std::string GetWidgetSource(const std::string& a_path) const;

	[[nodiscard]] const CrosshairSettings& GetCrosshairSettings() const { return _crosshair; }
	[[nodiscard]] const SneakMeterSettings& GetSneakMeterSettings() const { return _sneakMeter; }

private:
	std::uint32_t _toggleKey = 0x2D;
	bool _holdMode = false;
	bool _startVisible = false;
	bool _alwaysShowInCombat = false;
	bool _alwaysShowWeaponDrawn = false;
	float _fadeSpeed = 5.0f;
	float _displayDuration = 0.0f;
	bool _dumpHUD = false;

	CrosshairSettings _crosshair;
	SneakMeterSettings _sneakMeter;

	std::map<std::string, int> _widgetPathToMode;
	std::set<std::string> _subWidgetPaths;
	std::map<std::string, std::string> _widgetSources;
};
