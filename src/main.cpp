// Waning Glow - SKSE plugin
// Copyright (C) 2026 izzydoingit
//
// This program is free software: you can redistribute it and/or modify it under the terms of the GNU
// General Public License as published by the Free Software Foundation, either version 3 of the License,
// or (at your option) any later version. See LICENSE.txt.
//
// An enchanted weapon's light follows its charge: full when charged, dimmer as it is used, a sputter when nearly empty,
// a pulse when a hit spends charge, a flare when a soul gem refills it, and a cooler colour near empty. A bound weapon's
// light follows the time its spell has left. A weapon no lighting mod lights gets a simple light of its own, and the
// enchantment's glow can dim with the charge too. It needs no plugin (.esp) and no script.
//
// THE FILES, AND WHAT EACH ONE IS FOR
//   main.cpp       this file - the hooks (the HUD's charge bar among them), and the order things start in
//   FormText.h     how a rule file names a form, as plain text - tests/test_glow.cpp
//   Glow.h         the light's behaviour as plain numbers (curves, sputter, pulse, flare, cooling) - tests/test_glow.cpp
//   Charge.cpp     what a hand holds and how full it is
//   Lights.cpp     the lights on a weapon, found and scaled every frame
//   OwnLight.cpp   our own light, on a weapon no other mod lights (stands down when one does)
//   Rules.cpp      rule files: Data\SKSE\Plugins\WaningGlow\*.json (docs/RULES.md)
//   SettingsText.h the settings and their file's lines, as plain text - tests/test_glow.cpp
//   Settings.cpp   the settings file, Data\SKSE\Plugins\WaningGlow.ini, and the one shared copy
//   EditorIDs.cpp  editor IDs the game throws away, recorded for the rule files
//   Menu.cpp       the settings and debug pages, in SKSE Menu Framework's Mod Control Panel
//   API.cpp        what other plugins can ask (include/WaningGlowAPI.h)
//   Plugin.h       what they share      PCH.h  what they all include

#include "Plugin.h"

namespace
{
	// ------------------------------------------------------------------ every frame, last: the player's update
	// RELight Spell Addon runs its brightness pass here for the same reason: Light Placer and RE::Light have written
	// this frame's values by the time the player's update returns.
	struct PlayerUpdate
	{
		static void thunk(RE::PlayerCharacter* a_this, float a_delta)
		{
			func(a_this, a_delta);
			Plugin::UpdateHands(a_delta);
		}
		static inline REL::Relocation<decltype(thunk)> func;
		static void                                    Install()
		{
			REL::Relocation<std::uintptr_t> vtbl{ RE::PlayerCharacter::VTABLE[0] };
			func = vtbl.write_vfunc(REL::Relocate(0xAD, 0xAD, 0xAF), thunk);  // Actor::Update: 0xAD SE/AE, 0xAF VR
		}
	};

	// ------------------------------------------------------------------ after each enchantment effect's update
	// Light Placer updates a reference effect's lights in the same slot (UpdatePosition, 0x3B) after the game's own.
	// Installed at data load, long after Light Placer installed its hook at plugin load, so this wraps Light Placer's:
	// ours runs after it, on the values it just wrote.
	template <class T>
	struct UpdatePosition
	{
		static void thunk(T* a_this)
		{
			func(a_this);
			Plugin::AfterReferenceEffect(a_this, std::is_same_v<T, RE::ModelReferenceEffect>);  // an art model, not a shader's actor
		}
		static inline REL::Relocation<decltype(thunk)> func;
		static void                                    Install()
		{
			REL::Relocation<std::uintptr_t> vtbl{ T::VTABLE[0] };
			func = vtbl.write_vfunc(0x3B, thunk);
		}
	};

	// ------------------------------------------------------------------ after the game animates an effect shader
	// ShaderReferenceEffect::Update (0x28) writes this frame's fill and rim alpha; the experimental shader dimming runs
	// after it.
	struct ShaderUpdate
	{
		static bool thunk(RE::ShaderReferenceEffect* a_this, float a_delta)
		{
			const bool result = func(a_this, a_delta);
			Plugin::AfterShaderEffect(a_this);
			return result;
		}
		static inline REL::Relocation<decltype(thunk)> func;
		static void                                    Install()
		{
			REL::Relocation<std::uintptr_t> vtbl{ RE::ShaderReferenceEffect::VTABLE[0] };
			func = vtbl.write_vfunc(0x28, thunk);
		}
	};

	// ------------------------------------------------------------------ after the cell animates its lights
	// Light Placer's other per-frame pass: at this call in TESObjectCELL::RunAnimations it updates the lights it hangs on
	// objects and actors (a weapon's own model lights among them). Wrapping the same call after Light Placer did makes
	// our numbers the last word there too; ReapplyAll re-applies this frame's numbers without advancing any timer.
	// SE/AE only: the site is Light Placer's (powerof3/LightPlacer, Hooks/Update.cpp), which is known for those two;
	// in VR the player-update and effect passes carry it alone.
	struct CellAnimations
	{
		static void thunk(RE::TESObjectCELL* a_cell)
		{
			func(a_cell);
			Plugin::ReapplyAll();
		}
		static inline REL::Relocation<decltype(thunk)> func;
		static void                                    Install()
		{
			if (REL::Module::IsVR()) {
				SKSE::log::info("VR: no cell animation pass");
				return;
			}
			REL::Relocation<std::uintptr_t> target{ RELOCATION_ID(18458, 18889), 0x52 };  // TESObjectCELL::RunAnimations
			if (*reinterpret_cast<const std::uint8_t*>(target.address()) != 0xE8) {
				SKSE::log::warn("cell animation pass: the expected call is not there (another plugin rewrote it?); skipped");
				return;
			}
			func = SKSE::GetTrampoline().write_call<5>(target.address(), thunk);
			SKSE::log::info("cell animation pass hooked, after Light Placer's if it is there");
		}
	};

	// ------------------------------------------------------------------ the HUD's enchantment charge bar
	// HUDChargeMeter::Update hands the HUD movie its numbers in two calls to SetChargeMeterPercent(percent, force,
	// leftHand, show) - the two calls TrueHUD also reads for its own bar (ersh1/TrueHUD, Hooks.h). With HideChargeBar on,
	// both go out with show = false, which the vanilla HUD, SkyHUD and TrueHUD all take as "hide the bar". Installed at
	// data load, after TrueHUD hooked the same calls at plugin load, so TrueHUD is handed what we pass on.
	// SE/AE only: the call sites are known for those two.
	template <int N>
	struct ChargeBar
	{
		static bool thunk(RE::GFxValue::ObjectInterface* a_this, void* a_data, RE::GFxValue* a_result, const char* a_name,
			const RE::GFxValue* a_args, RE::UPInt a_count, bool a_isDisplayObject)
		{
			const auto settings = Plugin::Config();
			if (settings.debugLog && a_args && a_count == 4 && a_args[0].IsNumber() && a_args[2].IsBool() && a_args[3].IsBool()) {
				// the debug log: each change of what the game hands the HUD (it calls this every frame)
				static std::tuple<int, bool, bool> last[2]{ { -1, false, false }, { -1, false, false } };
				const std::tuple<int, bool, bool> now{ static_cast<int>(a_args[0].GetNumber()), a_args[2].GetBool(), a_args[3].GetBool() };
				if (std::exchange(last[N], now) != now) {
					SKSE::log::info("charge bar ({}): {}% {} hand, show {}{}", N, std::get<0>(now), std::get<1>(now) ? "left" : "right",
						std::get<2>(now), settings.hideChargeBar ? " - hidden by Waning Glow" : "");
				}
			}
			if (a_args && a_count == 4 && settings.hideChargeBar) {
				RE::GFxValue args[4]{ a_args[0], a_args[1], a_args[2], a_args[3] };
				args[3].SetBoolean(false);
				return func(a_this, a_data, a_result, a_name, args, a_count, a_isDisplayObject);
			}
			return func(a_this, a_data, a_result, a_name, a_args, a_count, a_isDisplayObject);
		}
		static inline REL::Relocation<decltype(thunk)> func;
		static bool                                    Install(std::ptrdiff_t a_offset)
		{
			REL::Relocation<std::uintptr_t> target{ RELOCATION_ID(50771, 51666), a_offset };  // HUDChargeMeter::Update
			if (*reinterpret_cast<const std::uint8_t*>(target.address()) != 0xE8) {
				return false;
			}
			func = SKSE::GetTrampoline().write_call<5>(target.address(), thunk);
			return true;
		}
	};

	void InstallChargeBar()
	{
		if (REL::Module::IsVR()) {
			SKSE::log::info("VR: the charge bar cannot be hidden");
			return;
		}
		const bool ok = ChargeBar<0>::Install(0x168) && ChargeBar<1>::Install(0x2B3);
		SKSE::log::info("{}", ok ? "the charge bar's two updates hooked" :
		                           "the charge bar's updates are not where expected (another plugin rewrote them?); it cannot be hidden");
	}

	void OnDataLoaded()
	{
		Plugin::LoadSettings();
		Plugin::LoadRules();
		PlayerUpdate::Install();
		UpdatePosition<RE::ModelReferenceEffect>::Install();
		UpdatePosition<RE::ShaderReferenceEffect>::Install();
		ShaderUpdate::Install();
		CellAnimations::Install();
		InstallChargeBar();
		SKSE::log::info("hooked the player update, enchantment effects' updates (after Light Placer's) and effect shaders");
		// Light Placer ships as po3_LightPlacer.dll (read off his install 2026-09-24); the plain name is kept for any other build
		const bool lightPlacer = REX::W32::GetModuleHandleA("po3_LightPlacer.dll") != nullptr ||
		                         REX::W32::GetModuleHandleA("LightPlacer.dll") != nullptr;
		SKSE::log::info("Light Placer {}", lightPlacer ? "is loaded" :
		                                                 "is not loaded - only lights in weapon and art meshes will fade");
		Plugin::RegisterMenu();
	}
}

SKSEPluginLoad(const SKSE::LoadInterface* a_skse)
{
	SKSE::Init(a_skse, { .trampoline = true, .trampolineSize = 64 });
	SKSE::log::info("Waning Glow {} loading", SKSE::PluginDeclaration::GetSingleton()->GetVersion().string());
	// before the game reads its plugins: the rule files may name forms by editor ID
	Plugin::InstallEditorIDHooks();
	SKSE::GetMessagingInterface()->RegisterListener([](SKSE::MessagingInterface::Message* a_msg) {
		if (!a_msg) {
			return;
		}
		switch (a_msg->type) {
		case SKSE::MessagingInterface::kPostLoad:
			Plugin::OfferToDevBench();
			break;
		case SKSE::MessagingInterface::kDataLoaded:
			OnDataLoaded();
			break;
		case SKSE::MessagingInterface::kPreLoadGame:
		case SKSE::MessagingInterface::kNewGame:
			// the weapons about to load (or a new game's) are new copies: forget the old ones, putting their lights back
			// first, and the rule matches of enchantments made in play
			Plugin::ReleaseAll();
			Plugin::ForgetRuleMatches();
			break;
		default:
			break;
		}
	});
	return true;
}
