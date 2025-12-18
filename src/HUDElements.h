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
			// Vanilla
			{ "iMode_Compass", "$fzIH_ElemCompass", { "_root.HUDMovieBaseInstance.CompassShoutMeterHolder" }, false },
			{ "iMode_Crosshair", "$fzIH_ElemCrosshair", { "_root.HUDMovieBaseInstance.Crosshair" }, true },
			{ "iMode_StealthMeter", "$fzIH_ElemStealth", { "_root.HUDMovieBaseInstance.StealthMeterInstance" }, false },
			{ "iMode_Health", "$fzIH_ElemHealth", { "_root.HUDMovieBaseInstance.Health" }, false },
			{ "iMode_Magicka", "$fzIH_ElemMagicka", { "_root.HUDMovieBaseInstance.Magica" }, false },
			{ "iMode_Stamina", "$fzIH_ElemStamina", { "_root.HUDMovieBaseInstance.Stamina" }, false },
			{ "iMode_EnchantLeft", "$fzIH_ElemEnchantLeft", { "_root.HUDMovieBaseInstance.BottomLeftLockInstance" }, false },
			{ "iMode_EnchantRight", "$fzIH_ElemEnchantRight", { "_root.HUDMovieBaseInstance.BottomRightLockInstance" }, false },
			{ "iMode_EnemyHealth", "$fzIH_ElemEnemyHealth", { "_root.HUDMovieBaseInstance.EnemyHealth_mc" }, false },
			{ "iMode_FloatingQuestMarker", "$fzIH_ElemFloatMark", { "_root.HUDMovieBaseInstance.FloatingQuestMarkerInstance" }, false },
			{ "iMode_Ammo", "$fzIH_ElemAmmo", { "_root.HUDMovieBaseInstance.ArrowInfoInstance" }, false },

			//SkyHUD
			{ "iMode_EnchantCombined", "$fzIH_ElemEnchantCombined", { "_root.HUDMovieBaseInstance.ChargeMeterBaseAlt" }, false },
			{ "iMode_TimeDisplay", "$fzIH_ElemTime", { "_root.HUDMovieBaseInstance.TimeDisplay" }, false }
		};
		return data;
	}
}
