#pragma once

#include "HUDManager.h"
#include "PCH.h"
#include "Settings.h"

namespace Events
{
	class InputEventSink : public RE::BSTEventSink<RE::InputEvent*>
	{
	public:
		static InputEventSink* GetSingleton()
		{
			static InputEventSink singleton;
			return &singleton;
		}

		static void Register()
		{
			if (auto* deviceManager = RE::BSInputDeviceManager::GetSingleton()) {
				deviceManager->AddEventSink(GetSingleton());
				logger::info("Registered Input Event Sink");
			}
		}

		RE::BSEventNotifyControl ProcessEvent(RE::InputEvent* const* a_event, [[maybe_unused]] RE::BSTEventSource<RE::InputEvent*>* a_eventSource) override
		{
			if (!a_event || !*a_event) {
				return RE::BSEventNotifyControl::kContinue;
			}

			auto settings = Settings::GetSingleton();
			auto key = settings->GetToggleKey();

			if (key == static_cast<std::uint32_t>(-1)) {
				return RE::BSEventNotifyControl::kContinue;
			}

			for (auto event = *a_event; event; event = event->next) {
				if (auto button = event->AsButtonEvent()) {
					if (button->GetIDCode() == key) {
						if (button->IsDown()) {
							HUDManager::GetSingleton()->OnButtonDown();
						} else if (button->IsUp()) {
							HUDManager::GetSingleton()->OnButtonUp();
						}
					}
				}
			}
			return RE::BSEventNotifyControl::kContinue;
		}

	private:
		InputEventSink() = default;
		InputEventSink(const InputEventSink&) = delete;
		InputEventSink(InputEventSink&&) = delete;
		~InputEventSink() = default;
	};

	class MenuOpenCloseEventSink : public RE::BSTEventSink<RE::MenuOpenCloseEvent>
	{
	public:
		static MenuOpenCloseEventSink* GetSingleton()
		{
			static MenuOpenCloseEventSink singleton;
			return &singleton;
		}

		static void Register()
		{
			if (auto* ui = RE::UI::GetSingleton()) {
				ui->AddEventSink(GetSingleton());
				logger::info("Registered Menu Open/Close Event Sink");
			}
		}

		RE::BSEventNotifyControl ProcessEvent(const RE::MenuOpenCloseEvent* a_event, [[maybe_unused]] RE::BSTEventSource<RE::MenuOpenCloseEvent>* a_eventSource) override
		{
			if (!a_event) {
				return RE::BSEventNotifyControl::kContinue;
			}

			// Reload settings when Journal closes
			if (a_event->menuName == RE::JournalMenu::MENU_NAME && !a_event->opening) {
				Settings::GetSingleton()->Load();
				HUDManager::GetSingleton()->Reset();
			}

			// Force scan when HUD opens (game load finish)
			if (a_event->menuName == RE::HUDMenu::MENU_NAME && a_event->opening) {
				HUDManager::GetSingleton()->ScanIfReady();
			}

			return RE::BSEventNotifyControl::kContinue;
		}

	private:
		MenuOpenCloseEventSink() = default;
		MenuOpenCloseEventSink(const MenuOpenCloseEventSink&) = delete;
		MenuOpenCloseEventSink(MenuOpenCloseEventSink&&) = delete;
		~MenuOpenCloseEventSink() = default;
	};
}
