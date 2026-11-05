// The hint in the world map's button bar. Own code, GPL-3.0-or-later (2026-11-04). See include/KeyHint.h.
//
// The way World Map Selector for Controller adds its "Select Map" hint (its src/MapMenuKeyHint.cpp): SkyUI's panel is
// _root.bottomBar.buttonPanel, its `buttons` array holds the clips, and addButton fills the next one with the same data SkyUI's
// own hints use, so the hint wears the active UI's skin. Two mods can do this side by side: addButton fills the next unused clip
// and never one already labelled. A mod's clip is only attached when no unused clip is left, under a name of its own, so neither
// mod's hint takes the other's place. Whether the hint is there is read from the labels rather than from this mod's own clip,
// because addButton may have used one of SkyUI's spare clips instead.
//
// TWO ATTACHED CLIPS OVERLAPPED ("Select M <- Marker Filter", the owner's test, 2026-11-05). SkyUI's map fills all seven of its clips, so this
// hint and World Map Selector's each attach one, in the same frame, as the map opens. A clip attached this frame is not initialized until its
// onLoad next frame: gfx Button's label setter only stores the text, so MappedButton measures the clip's placeholder and the panel places the
// NEXT clip by that width. onLoad then sets the real text and width, and nothing lays the panel out again, so whichever hint comes first in the
// array (the sinks' order) runs under the second. A last clip has nothing after it, so either mod alone looks right. MappedButton's onLoad ends
// with `_parent.onButtonLoad(this)`, undefined in SkyUI's map: answered here with the panel's own layout, for any fresh clip, either mod's.

#include "PCH.h"

#include "KeyHint.h"
#include "Settings.h"
#include "Strings.h"
#include "utils/Logger.h"

#include <array>
#include <bit>
#include <optional>
#include <string>
#include <string_view>

namespace keyhint
{
	namespace
	{
		constexpr const char* kClipName = "worldMapMarkerFilterButton";

		std::atomic<bool> lastInputWasGamepad{ false };

		const std::string& HintLabel()
		{
			static const std::string label = TR("MML_HintBar", "Marker Filter");  // static: outlives every GFxValue pointing at it
			return label;
		}

		// SkyUI's button art reads SKSE key codes: keyboard scan codes as they are, controller buttons as 266 + the button's
		// index (D-pad up 266 ... Y 279, LT 280, RT 281). 0 for a mask with no art.
		std::uint32_t GamepadMaskToSkseKeyCode(std::uint32_t a_mask)
		{
			switch (a_mask)
			{
			case 0x0009: return 280;  // left trigger
			case 0x000A: return 281;  // right trigger
			default: break;
			}
			if (a_mask == 0 || (a_mask & (a_mask - 1)) != 0) { return 0; }  // not exactly one button
			auto index = static_cast<std::uint32_t>(std::countr_zero(a_mask));
			if (index >= 12) { index -= 2; }  // XInput leaves bits 10 and 11 unused
			return index <= 13 ? 266 + index : 0;
		}

		// The controller button while the controller is in use, else the keyboard key; the other one if that one is off.
		std::uint32_t HintKeyCode()
		{
			const auto pad = GamepadMaskToSkseKeyCode(settings::togglePad.load());
			const auto key = settings::toggleKey.load();
			if (lastInputWasGamepad.load()) { return pad ? pad : key; }
			return key ? key : pad;
		}

		// What a clip's label holds, empty for a clip no hint has filled (SkyUI's clearButtons empties them).
		std::string LabelOf(const RE::GFxValue& a_button)
		{
			RE::GFxValue label;
			if (a_button.IsDisplayObject() && a_button.GetMember("label", &label) && label.IsString()) { return label.GetString(); }
			return {};
		}

		bool AttachClip(RE::GFxValue& a_panel, RE::GFxValue& a_buttons)
		{
			RE::GFxValue renderer;
			RE::GFxValue initializer;
			RE::GFxValue depth;
			if (!a_panel.GetMember("buttonRenderer", &renderer) || !renderer.IsString() ||
				!a_panel.GetMember("buttonInitializer", &initializer) ||
				!a_panel.Invoke("getNextHighestDepth", &depth) || !depth.IsNumber())
			{
				return false;
			}
			RE::GFxValue button;
			const RE::GFxValue* init = initializer.IsObject() ? &initializer : nullptr;
			if (!a_panel.AttachMovie(&button, renderer.GetString(), kClipName, static_cast<std::int32_t>(depth.GetSInt()), init) ||
				!button.IsDisplayObject() || !a_buttons.PushBack(button))
			{
				return false;
			}
			return a_panel.SetMember("maxButtons", a_buttons.GetArraySize());
		}

		// The panel's onButtonLoad: a fresh clip now has its real width. Runs on the panel's movie.
		class LayOutAgain final : public RE::GFxFunctionHandler
		{
		public:
			void Call(Params& a_params) override
			{
				if (!a_params.thisPtr) { return; }
				a_params.thisPtr->Invoke("updateButtons", nullptr, std::array{ RE::GFxValue(true) });
				logger::debug("a new clip in the map's button panel loaded; laid out again");
			}
		};

		void LayOutWhenClipsLoad(RE::GFxMovieView* a_movie, RE::GFxValue& a_panel)
		{
			RE::GFxValue present;
			if (a_panel.GetMember("onButtonLoad", &present) && !present.IsUndefined()) { return; }
			RE::GFxValue function;
			RE::GPtr<LayOutAgain> handler{ new LayOutAgain() };
			a_movie->CreateFunction(&function, handler.get());
			if (!a_panel.SetMember("onButtonLoad", function)) { logger::warn("could not set the button panel's onButtonLoad; a second added hint may overlap this one"); }
		}

		class InputSink final : public RE::BSTEventSink<RE::InputEvent*>
		{
		public:
			RE::BSEventNotifyControl ProcessEvent(RE::InputEvent* const* a_events, RE::BSTEventSource<RE::InputEvent*>*) override
			{
				if (!a_events) { return RE::BSEventNotifyControl::kContinue; }
				static std::optional<bool> previous;
				bool refresh = false;
				for (auto* event = *a_events; event; event = event->next)
				{
					const auto device = event->GetDevice();
					const bool pad = device == RE::INPUT_DEVICE::kGamepad;
					if ((pad || device == RE::INPUT_DEVICE::kKeyboard || device == RE::INPUT_DEVICE::kMouse) && (!previous || *previous != pad))
					{
						// SkyUI rebuilds the bar when the device changes; the hint goes back on after that, with that device's button.
						previous = pad;
						lastInputWasGamepad = pad;
						refresh = true;
					}
				}
				if (refresh)
				{
					auto* ui = RE::UI::GetSingleton();
					const auto* tasks = SKSE::GetTaskInterface();
					if (ui && tasks && ui->IsMenuOpen(RE::MapMenu::MENU_NAME)) { tasks->AddUITask(Show); }
				}
				return RE::BSEventNotifyControl::kContinue;
			}
		};
	}

	void Register()
	{
		static InputSink sink;
		if (auto* devices = RE::BSInputDeviceManager::GetSingleton()) { devices->AddEventSink(&sink); }
		else { logger::warn("no input device manager; the map's button hint will not follow the device in use"); }
	}

	void Show()
	{
		if (settings::showKeyHint.load() == 0) { return; }
		const auto keyCode = HintKeyCode();
		if (keyCode == 0) { return; }

		auto* ui = RE::UI::GetSingleton();
		const auto mapMenu = ui ? ui->GetMenu<RE::MapMenu>() : nullptr;
		if (!mapMenu || !mapMenu->uiMovie)
		{
			logger::debug("the map's movie is not there; no button hint");
			return;
		}
		auto* movie = mapMenu->uiMovie.get();

		RE::GFxValue panel;
		RE::GFxValue buttons;
		if (!movie->GetVariable(&panel, "_root.bottomBar.buttonPanel") || !panel.IsDisplayObject() ||
			!panel.GetMember("buttons", &buttons) || !buttons.IsArray())
		{
			logger::debug("this map has no SkyUI button panel; no button hint");
			return;
		}

		LayOutWhenClipsLoad(movie, panel);  // before the early return: World Map Selector's fresh clip needs it whether or not this hint is new

		bool unused = false;
		std::uint32_t filled = 0;
		for (std::uint32_t i = 0; i < buttons.GetArraySize(); ++i)
		{
			RE::GFxValue candidate;
			if (!buttons.GetElement(i, &candidate)) { continue; }
			const auto label = LabelOf(candidate);
			if (label == HintLabel()) { return; }
			if (label.empty()) { unused = true; }
			else { ++filled; }
		}

		if (!unused && !AttachClip(panel, buttons))
		{
			logger::warn("could not add a clip to the map's button panel; no button hint");
			return;
		}

		RE::GFxValue controls;
		movie->CreateObject(&controls);
		controls.SetMember("keyCode", keyCode);
		RE::GFxValue data;
		movie->CreateObject(&data);
		data.SetMember("text", HintLabel().c_str());
		data.SetMember("controls", controls);

		RE::GFxValue added;
		if (!panel.Invoke("addButton", &added, std::array{ data }) || !added.IsDisplayObject() ||
			!panel.Invoke("updateButtons", nullptr, std::array{ RE::GFxValue(true) }))
		{
			logger::warn("the map's button panel did not take the hint ({} hints already in it)", filled);
			return;
		}
		logger::info("button hint added beside {} other hint(s), key code {}", filled, keyCode);
	}
}
