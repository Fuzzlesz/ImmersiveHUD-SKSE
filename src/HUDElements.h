#pragma once

namespace HUDElements
{
	struct Def
	{
		const char* id;                  // INI/MCM ID (e.g., "iMode_Health")
		const char* label;               // Localization Key (e.g., "$fzIH_ElemHealth")
		std::vector<const char*> paths;  // Flash paths (e.g., "_root...Health")
		bool isCrosshair;                // Special flag for contextual logic
	};

	inline const std::vector<Def>& Get()
	{
		static const std::vector<Def> data = {
			
			// Vanilla and SkyHUD elements
			{ "iMode_Ammo", "$fzIH_ElemAmmo", {
				"_root.HUDMovieBaseInstance.ArrowInfoInstance"
			}, false },

			{ "iMode_Compass", "$fzIH_ElemCompass", { 
				"_root.HUDMovieBaseInstance.CompassShoutMeterHolder.Compass.CompassFrame",
				"_root.HUDMovieBaseInstance.CompassShoutMeterHolder.Compass.CompassFrameAlt",
				"_root.HUDMovieBaseInstance.CompassShoutMeterHolder.Compass.DirectionRect",
				"_root.HUDMovieBaseInstance.CompassShoutMeterHolder.Compass.CompassRect",
				"_root.HUDMovieBaseInstance.CompassShoutMeterHolder.Compass.CompassCard",
				"_root.HUDMovieBaseInstance.CompassShoutMeterHolder.Compass.CompassCardAlt",
				"_root.HUDMovieBaseInstance.CompassShoutMeterHolder.Compass.CompassMask_mc"
			}, false },

			{ "iMode_Crosshair", "$fzIH_ElemCrosshair", {
				"_root.HUDMovieBaseInstance.Crosshair"
			}, true },

			{ "iMode_EnchantLeft", "$fzIH_ElemEnchantLeft", {
				"_root.HUDMovieBaseInstance.BottomLeftLockInstance",
				"_root.HUDMovieBaseInstance.LeftChargeMeter"
			}, false },

			{ "iMode_EnchantRight", "$fzIH_ElemEnchantRight", {
				"_root.HUDMovieBaseInstance.BottomRightLockInstance",
				"_root.HUDMovieBaseInstance.RightChargeMeter"
			}, false },

			{ "iMode_EnemyHealth", "$fzIH_ElemEnemyHealth", {
				"_root.HUDMovieBaseInstance.EnemyHealth_mc",
				"_root.HUDMovieBaseInstance.EnemyHealthMeter"
			}, false },

			{ "iMode_FloatingQuestMarker", "$fzIH_ElemFloatMark", {
				"_root.HUDMovieBaseInstance.FloatingQuestMarkerInstance"
			}, false },

			{ "iMode_Health", "$fzIH_ElemHealth", {
				"_root.HUDMovieBaseInstance.Health",
				"_root.HUDMovieBaseInstance.HealthMeterLeft"
			},false },

			{ "iMode_Magicka", "$fzIH_ElemMagicka", {
				"_root.HUDMovieBaseInstance.Magica",
				"_root.HUDMovieBaseInstance.MagickaMeter"
			}, false },

			{ "iMode_ShoutMeter", "$fzIH_ElemShout", {
				"_root.HUDMovieBaseInstance.CompassShoutMeterHolder.ShoutMeterInstance",
				"_root.HUDMovieBaseInstance.CompassShoutMeterHolder.ShoutWarningInstance",
				"_root.HUDMovieBaseInstance.CompassShoutMeterHolder.ShoutMeterBarAlt",
				"_root.HUDMovieBaseInstance.CompassShoutMeterHolder.ShoutWarningInstanceAlt"
			}, false },

			{ "iMode_Stamina", "$fzIH_ElemStamina", {
				"_root.HUDMovieBaseInstance.Stamina",
				"_root.HUDMovieBaseInstance.StaminaMeter"
			}, false },

			{ "iMode_StealthMeter", "$fzIH_ElemStealth", {
				"_root.HUDMovieBaseInstance.StealthMeterInstance"
			}, false },

			{ "iMode_Temperature", "$fzIH_ElemTemperature", {
				"_root.HUDMovieBaseInstance.TemperatureMeter_mc"
			}, false },

			// SkyHUD specific elements
			{ "iMode_EnchantCombined", "$fzIH_ElemEnchantCombined", {
				"_root.HUDMovieBaseInstance.ChargeMeterBaseAlt"
			}, false },

			{ "iMode_TimeDisplay", "$fzIH_ElemTime", {
				"_root.HUDMovieBaseInstance.TimeDisplay"
			}, false }
		};
		return data;
	}
}
