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
	if (!g_DisableCompass) {
		g_DisableCompass = dataHandler->LookupForm<RE::TESGlobal>(0xEEE, "ImmersiveHUD.esp");
	}
	if (!g_DisableSneak) {
		g_DisableSneak = dataHandler->LookupForm<RE::TESGlobal>(0xFFF, "ImmersiveHUD.esp");
	}

	if (g_DisableCompass || g_DisableSneak) {
		logger::info("Linked generic HUD control globals from ImmersiveHUD.esp");
	}
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

bool Compat::IsFakeFirstPerson()
{
	auto camera = RE::PlayerCamera::GetSingleton();
	if (!camera || camera->IsInFirstPerson() || camera->IsInFreeCameraMode()) {
		return false;
	}

	auto thirdPersonState = static_cast<RE::ThirdPersonState*>(camera->currentState.get());
	return thirdPersonState && thirdPersonState->currentZoomOffset == -0.275f;
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
	return IsSpellContextual(a_player->magicCasters[0]->currentSpell) ||
	       IsSpellContextual(a_player->magicCasters[1]->currentSpell);
}

bool Compat::IsPlayerAttacking(RE::PlayerCharacter* a_player)
{
	if (!a_player) {
		return false;
	}
	auto equipped = a_player->GetEquippedObject(true);
	auto attackState = a_player->actorState1.meleeAttackState;

	if (equipped && equipped->GetFormType() == RE::FormType::Weapon) {
		auto weapon = equipped->As<RE::TESObjectWEAP>();
		if (weapon->IsBow()) {
			return attackState >= RE::ATTACK_STATE_ENUM::kBowAttached && attackState <= RE::ATTACK_STATE_ENUM::kBowNextAttack;
		}
		if (weapon->IsCrossbow()) {
			return attackState >= RE::ATTACK_STATE_ENUM::kBowDraw && attackState <= RE::ATTACK_STATE_ENUM::kNextAttack;
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
