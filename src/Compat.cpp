#include "Compat.h"

	// ==========================================
	// Compatibility Implementation
	// ==========================================

	void Compat::InitExternalData()
{
	auto dataHandler = RE::TESDataHandler::GetSingleton();
	if (!dataHandler) {
		return;
	}

	// Immersive First Person View lookup
	if (!g_IFPV) {
		g_IFPV = dataHandler->LookupForm<RE::TESGlobal>(0x801, "IFPVDetector.esl");
	}

	// Internal Global Control lookup (ImmersiveHUD.esp)
	// These allow external mods to force-hide elements via script/patch
	if (!g_DisableiHUD) {
		g_DisableiHUD = dataHandler->LookupForm<RE::TESGlobal>(0xDDD, "ImmersiveHUD.esp");
	}
	if (!g_DisableCompass) {
		g_DisableCompass = dataHandler->LookupForm<RE::TESGlobal>(0xEEE, "ImmersiveHUD.esp");
	}
	if (!g_DisableSneak) {
		g_DisableSneak = dataHandler->LookupForm<RE::TESGlobal>(0xFFF, "ImmersiveHUD.esp");
	}

	if (g_DisableCompass || g_DisableSneak || g_DisableiHUD) {
		logger::info("Linked generic HUD control globals from ImmersiveHUD.esp");
	}

	// SkyHUD alt charge detection
	const fs::path skyhudPath = "Data/Interface/skyhud/skyhud.txt";
	if (fs::exists(skyhudPath)) {
		CSimpleIniA ini;
		ini.SetUnicode();
		if (ini.LoadFile(skyhudPath.string().c_str()) >= 0) {
			int val = ini.GetLongValue("Interface", "bAltCharge", -1);
			if (val == -1) {
				val = ini.GetLongValue("Gameplay", "bAltCharge", -1);
			}
			_skyHUDAltCharge = (val == 1);
			if (_skyHUDAltCharge) {
				logger::info("SkyHUD bAltCharge=1 detected");
			}
		}
	}
}

bool Compat::IsSkyHUDAltChargeEnabled() const
{
	return _skyHUDAltCharge;
}

void Compat::ManageSmoothCamControl(bool a_shouldBlock)
{
	if (!g_SmoothCam) {
		return;
	}

	auto handle = SKSE::GetPluginHandle();

	if (a_shouldBlock) {
		if (!_hasSmoothCamCrosshairControl) {
			auto res = g_SmoothCam->RequestCrosshairControl(handle);
			if (res == SmoothCamAPI::APIResult::OK || res == SmoothCamAPI::APIResult::AlreadyGiven) {
				_hasSmoothCamCrosshairControl = true;
			}
		}
		if (!_hasSmoothCamStealthControl) {
			auto res = g_SmoothCam->RequestStealthMeterControl(handle);
			if (res == SmoothCamAPI::APIResult::OK || res == SmoothCamAPI::APIResult::AlreadyGiven) {
				_hasSmoothCamStealthControl = true;
			}
		}
	} else {
		if (_hasSmoothCamCrosshairControl) {
			auto res = g_SmoothCam->ReleaseCrosshairControl(handle);
			if (res == SmoothCamAPI::APIResult::OK || res == SmoothCamAPI::APIResult::NotOwner) {
				_hasSmoothCamCrosshairControl = false;
			}
		}
		if (_hasSmoothCamStealthControl) {
			auto res = g_SmoothCam->ReleaseStealthMeterControl(handle);
			if (res == SmoothCamAPI::APIResult::OK || res == SmoothCamAPI::APIResult::NotOwner) {
				_hasSmoothCamStealthControl = false;
			}
		}
	}
}

bool Compat::IsTDMActive()
{
	return g_TDM && g_TDM->GetTargetLockState();
}

bool Compat::IsSmoothCamActive()
{
	if (IsIFPVActive() || IsFakeFirstPerson()) {
		return false;
	}

	if (g_SmoothCam && g_SmoothCam->IsCameraEnabled()) {
		if (auto playerCamera = RE::PlayerCamera::GetSingleton(); playerCamera) {
			return playerCamera->currentState == playerCamera->cameraStates[RE::CameraState::kThirdPerson];
		}
	}
	return false;
}

bool Compat::IsDetectionMeterInstalled()
{
	return g_DetectionMeter != nullptr;
}

bool Compat::IsBTPSActive()
{
	return g_BTPS && g_BTPS->GetWidget3DEnabled();
}

bool Compat::IsIFPVActive()
{
	return g_IFPV && (g_IFPV->value != 0.0f);
}

// Heuristic for "Improved Camera", provided by ArranzCNL
bool Compat::IsFakeFirstPerson()
{
	auto camera = RE::PlayerCamera::GetSingleton();
	if (!camera || camera->IsInFirstPerson() || camera->IsInFreeCameraMode()) {
		return false;
	}

	auto thirdPersonState = static_cast<RE::ThirdPersonState*>(camera->currentState.get());
	return thirdPersonState && thirdPersonState->currentZoomOffset == -0.275f;
}

bool Compat::IsImmersiveHUDDisabled()
{
	// ImmersiveHUD is disabled when the global is set to 1
	return g_DisableiHUD && (g_DisableiHUD->value != 0.0f);
}

bool Compat::IsCompassAllowed()
{
	// Element is allowed if the "Disable" global is 0 (or if the global isn't found)
	return !g_DisableCompass || (g_DisableCompass->value == 0.0f);
}

bool Compat::IsSneakAllowed()
{
	// Element is allowed if the "Disable" global is 0 (or if the global isn't found)
	return !g_DisableSneak || (g_DisableSneak->value == 0.0f);
}

bool Compat::HasEnchantedWeapon(bool a_leftHand)
{
	auto player = RE::PlayerCharacter::GetSingleton();
	if (!player) {
		return false;
	}

	auto obj = player->GetEquippedObject(a_leftHand);
	if (!obj || obj->GetFormType() != RE::FormType::Weapon) {
		return false;
	}

	auto entryData = player->GetEquippedEntryData(a_leftHand);
	return entryData && entryData->IsEnchanted();
}

bool Compat::IsEnchantmentFull(bool a_leftHand)
{
	auto player = RE::PlayerCharacter::GetSingleton();
	if (!player)
		return true;

	auto av = a_leftHand ? RE::ActorValue::kLeftItemCharge : RE::ActorValue::kRightItemCharge;

	float maxCharge = player->GetBaseActorValue(av);
	float currentCharge = player->GetActorValue(av);

	if (maxCharge <= 0.0f)
		return true;

	// 2.0f tolerance to handle engine rounding/float precision
	return currentCharge >= (maxCharge - 2.0f);
}

bool Compat::IsPlayerWeaponDrawn()
{
	auto player = RE::PlayerCharacter::GetSingleton();
	if (!player)
		return false;

	// RE::ActorState is a base class of RE::Actor
	auto state = player->GetWeaponState();

	// We consider the weapon "Active" if it is out, being drawn, or being put away.
	return state != RE::WEAPON_STATE::kSheathed && state != RE::WEAPON_STATE::kWantToSheathe;
}

bool Compat::CameraStateCheck()
{
	auto camera = RE::PlayerCamera::GetSingleton();
	if (!camera || !camera->currentState) {
		return false;
	}

	// VATS / Kill Cam
	if (camera->currentState == camera->cameraStates[RE::CameraState::kVATS]) {
		return true;
	}

	// Auto Vanity / Vanity Cam
	if (camera->currentState == camera->cameraStates[RE::CameraState::kAutoVanity]) {
		return true;
	}

	return false;
}

bool Compat::IsCrosshairTargetValid()
{
	if (auto crosshairPickData = RE::CrosshairPickData::GetSingleton()) {
		if (auto refr = crosshairPickData->target.get()) {
			return refr->GetFormType() != RE::FormType::ActorCharacter || refr.get()->As<RE::Actor>()->IsDead();
		}
	}
	return false;
}

bool Compat::IsPlayerCasting(RE::PlayerCharacter* a_player)
{
	if (!a_player) {
		return false;
	}

	bool leftCasting = false;
	if (auto* leftCaster = a_player->magicCasters[0]) {
		if (leftCaster->currentSpell) {
			leftCasting = IsSpellContextual(leftCaster->currentSpell);
		}
	}

	bool rightCasting = false;
	if (auto* rightCaster = a_player->magicCasters[1]) {
		if (rightCaster->currentSpell) {
			rightCasting = IsSpellContextual(rightCaster->currentSpell);
		}
	}

	return leftCasting || rightCasting;
}

bool Compat::IsPlayerAttacking(RE::PlayerCharacter* a_player)
{
	if (!a_player) {
		return false;
	}

	auto attackState = a_player->actorState1.meleeAttackState;

	for (bool isLeft : { false, true }) {
		auto equipped = a_player->GetEquippedObject(isLeft);

		if (equipped && equipped->GetFormType() == RE::FormType::Weapon) {
			auto weapon = equipped->As<RE::TESObjectWEAP>();

			if (weapon->IsBow()) {
				if (attackState >= RE::ATTACK_STATE_ENUM::kBowDraw &&
					attackState <= RE::ATTACK_STATE_ENUM::kBowFollowThrough) {
					return true;
				}
			} else if (weapon->IsCrossbow()) {
				if (attackState == RE::ATTACK_STATE_ENUM::kBowDrawn ||
					attackState == RE::ATTACK_STATE_ENUM::kBowReleasing ||
					attackState == RE::ATTACK_STATE_ENUM::kBowReleased) {
					return true;
				}

				// Crossbow Aiming is considered a Blocking state
				if (a_player->IsBlocking()) {
					return true;
				}
			}
		}
	}
	return false;
}

bool Compat::IsSpellContextual(RE::MagicItem* a_magicItem)
{
	if (!a_magicItem) {
		return false;
	}

	auto isTelekinesis = false;
	for (auto effect : a_magicItem->effects) {
		if (effect->baseEffect && effect->baseEffect->HasArchetype(RE::EffectSetting::Archetype::kTelekinesis)) {
			isTelekinesis = true;
			break;
		}
	}

	return ((a_magicItem->GetDelivery() == RE::MagicSystem::Delivery::kAimed) &&
			   (a_magicItem->GetCastingType() != RE::MagicSystem::CastingType::kConcentration)) ||
	       isTelekinesis;
}
