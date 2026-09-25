// Waning Glow - SKSE plugin
// Copyright (C) 2026 izzydoingit
// GPL-3.0-or-later; see LICENSE.txt and the notice at the top of main.cpp.
//
// What the files share. The file map is at the top of main.cpp.

#pragma once

#include "Glow.h"
#include "SettingsText.h"

namespace Plugin
{
	// std::string keys looked up by std::string_view without a temporary string
	struct StringHash
	{
		using is_transparent = void;
		[[nodiscard]] std::size_t operator()(std::string_view a_text) const noexcept { return std::hash<std::string_view>{}(a_text); }
	};
	template <class T>
	using StringMap = std::unordered_map<std::string, T, StringHash, std::equal_to<>>;

	[[nodiscard]] std::string Lower(std::string_view a_text);

	// A path as UTF-8 text. path::string() converts to the ANSI code page on Windows and THROWS for a name that code
	// page cannot show (a Japanese file name on a Western system); this cannot.
	[[nodiscard]] inline std::string PathText(const std::filesystem::path& a_path)
	{
		const auto u = a_path.generic_u8string();
		return std::string(u.begin(), u.end());
	}

	// ------------------------------------------------------------------ Settings.cpp: the settings (SettingsText.h)
	// The one shared copy lives in Settings.cpp behind a lock: the menu (render thread) and DevBench (its own thread)
	// change it while the main thread reads it every frame, so everyone works on a copy.
	[[nodiscard]] Settings Config();                             // a copy, safe from any thread
	void                   SetConfig(const Settings& a_settings);  // replaces it whole; live on the next frame
	bool                   ApplySetting(std::string_view a_key, int a_value);  // one WaningGlow.ini line; false if unknown
	void                   LoadSettings();
	void                   SaveSettings();

	// the Debug page's preview: never saved. The menu (or DevBench) sets it; the next frame reads it.
	struct Preview
	{
		std::atomic<bool>  on{ false };
		std::atomic<float> fraction{ 0.5f };               // every tracked hand shows this charge instead of its own
		std::atomic<bool>  pulse{ false }, flare{ false };  // one-shot: start a pulse / flare on every tracked hand
	};
	[[nodiscard]] Preview& PreviewState();

	// ------------------------------------------------------------------ EditorIDs.cpp: names the game throws away
	void                           InstallEditorIDHooks();  // at plugin load, before the game reads its plugins
	[[nodiscard]] std::string      EditorID(const RE::TESForm* a_form);  // "" when unknown (a copy: the table may be rewritten)
	[[nodiscard]] std::string      Label(const RE::TESForm* a_form);     // "Name [EditorID] Plugin.esp|0x001234" for the log

	// ------------------------------------------------------------------ Rules.cpp: rule files
	void                                          LoadRules();  // at data load, after the settings
	[[nodiscard]] Verdict                         Judge(const RE::TESObjectWEAP* a_weapon, const RE::EnchantmentItem* a_ench, const Settings& a_settings);
	void                                          ForgetRuleMatches();  // a game loads: the forms made in play are new ones
	[[nodiscard]] std::uint32_t                   RulesGeneration();    // changes whenever the rule files are read again
	[[nodiscard]] std::size_t                     RuleCount();
	[[nodiscard]] std::size_t                     RuleFileCount();
	[[nodiscard]] std::vector<std::string>        RuleProblems();  // a copy: the menu reads it while a reload may run

	// ------------------------------------------------------------------ Charge.cpp: what a hand holds
	struct Reading
	{
		const RE::TESObjectWEAP*    weapon{ nullptr };
		const RE::EnchantmentItem*  ench{ nullptr };
		const RE::ExtraDataList*    instance{ nullptr };  // which copy of the weapon: a swap to an identical one is still a swap
		bool                        tracked{ false };     // false: nothing of ours to do with this hand
		bool                        bound{ false };
		float                       fraction{ 1.0f };
		float                       current{ 0.0f }, max{ 0.0f };  // charge points, or seconds for a bound weapon
	};

	[[nodiscard]] Reading ReadHand(RE::Actor* a_actor, bool a_left);

	// ------------------------------------------------------------------ Lights.cpp: the lights on a weapon
	void UpdateHands(float a_delta);                               // every frame, from the player's update
	void AfterReferenceEffect(RE::ReferenceEffect* a_effect, bool a_own3D);  // after Light Placer's per-effect update;
	                                                                          // a_own3D: search the effect's own model too
	void AfterShaderEffect(RE::ShaderReferenceEffect* a_effect);   // after the game animates an enchantment's shader
	void ReapplyAll();                                              // after the cell's light animation: this frame's numbers again
	void ReleaseAll();                                              // every light back as we found it

	struct HandView
	{
		std::string actor, weapon, enchantment, why;
		bool        left{ false }, bound{ false }, exempt{ false };
		float       fraction{ 1.0f }, current{ 0.0f }, max{ 0.0f };
		float       brightness{ 1.0f }, reach{ 1.0f }, cool{ 0.0f };
		std::size_t lights{ 0 }, roots{ 0 };
		float       chargeAV{ -1.0f };  // the game's own RightItemCharge / LeftItemCharge, to compare
	};
	[[nodiscard]] std::vector<HandView> Snapshot();
	struct LightNow
	{
		float fade{ 0.0f }, base{ 0.0f }, radius{ 0.0f };
		bool  frozen{ false };
	};
	[[nodiscard]] std::vector<LightNow> LightsNow();  // every light being scaled: what it held after our last write, and its base
	[[nodiscard]] std::size_t           ScaledLightCount();
	[[nodiscard]] std::size_t           FrozenLightCount();

	// what the API hands other plugins: the charge fraction and the multiplier on this hand's lights; false if untracked
	[[nodiscard]] bool Query(RE::Actor* a_actor, bool a_left, float& a_fraction, float& a_brightness);

	// ------------------------------------------------------------------ Menu.cpp
	void RegisterMenu();

	// ------------------------------------------------------------------ DevBench.cpp
	void OfferToDevBench();
}
