#pragma once
#include "PCH.h"

namespace Events
{
	class InputEventSink : public RE::BSTEventSink<RE::InputEvent*>
	{
	public:
		static InputEventSink* GetSingleton();
		static void Register();

		RE::BSEventNotifyControl ProcessEvent(RE::InputEvent* const* a_event, RE::BSTEventSource<RE::InputEvent*>* a_eventSource) override;

	private:
		// Helper to map XInput driver masks to SkyUI integer codes
		static std::uint32_t RemapGamepadCode(std::uint32_t a_rawCode);

		InputEventSink() = default;
		InputEventSink(const InputEventSink&) = delete;
		InputEventSink(InputEventSink&&) = delete;
		~InputEventSink() = default;
	};

	class MenuOpenCloseEventSink : public RE::BSTEventSink<RE::MenuOpenCloseEvent>
	{
	public:
		static MenuOpenCloseEventSink* GetSingleton();
		static void Register();

		RE::BSEventNotifyControl ProcessEvent(const RE::MenuOpenCloseEvent* a_event, RE::BSTEventSource<RE::MenuOpenCloseEvent>* a_eventSource) override;

	private:
		MenuOpenCloseEventSink() = default;
		MenuOpenCloseEventSink(const MenuOpenCloseEventSink&) = delete;
		MenuOpenCloseEventSink(MenuOpenCloseEventSink&&) = delete;
		~MenuOpenCloseEventSink() = default;
	};
}
