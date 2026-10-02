// Waning Glow - SKSE plugin
// Copyright (C) 2026 izzydoingit
// GPL-3.0-or-later; see LICENSE.txt and the notice at the top of main.cpp.
//
// Two pages in SKSE Menu Framework's Mod Control Panel, under their own section. Settings: the everyday settings, saved
// at once and live on the next frame (finer tuning stays in WaningGlow.ini and the rule files). Debug: the debug log,
// each tracked hand's weapon, charge, the numbers on its lights and how many lights were found, plus the rule files and
// their problems - what a bug report needs.
// The look is RELight Spell Addon's: warm amber on the menu's dark, glowing headings, gold checks and grips.

#ifndef WIN32_LEAN_AND_MEAN
#	define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#	define NOMINMAX
#endif
#include "Plugin.h"

#include "SKSEMenuFramework.h"
#include "Translation.h"

namespace Plugin
{
	namespace
	{
		constexpr ImGuiMCP::ImVec4 kGold{ 1.0f, 0.86f, 0.55f, 1.0f };
		constexpr ImGuiMCP::ImVec4 kEmber{ 0.93f, 0.72f, 0.45f, 0.85f };
		constexpr ImGuiMCP::ImVec4 kDim{ 0.62f, 0.58f, 0.52f, 1.0f };
		using Translation::T;
#define TR_MARK(x) x  // a line Translation.json carries, though it is not written inside T( ) here

		class GlowStyle
		{
		public:
			GlowStyle()
			{
				using namespace ImGuiMCP;
				PushStyleColor(ImGuiCol_CheckMark, ImVec4{ 1.0f, 0.80f, 0.42f, 1.0f });
				PushStyleColor(ImGuiCol_SliderGrab, ImVec4{ 1.0f, 0.74f, 0.38f, 0.90f });
				PushStyleColor(ImGuiCol_SliderGrabActive, ImVec4{ 1.0f, 0.86f, 0.55f, 1.0f });
				PushStyleColor(ImGuiCol_FrameBg, ImVec4{ 0.12f, 0.10f, 0.08f, 0.75f });
				PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4{ 0.30f, 0.21f, 0.10f, 0.75f });
				PushStyleColor(ImGuiCol_Separator, ImVec4{ 1.0f, 0.78f, 0.45f, 0.22f });
				PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);
			}
			~GlowStyle()
			{
				ImGuiMCP::PopStyleVar(1);
				ImGuiMCP::PopStyleColor(6);
			}
			GlowStyle(const GlowStyle&) = delete;
			GlowStyle& operator=(const GlowStyle&) = delete;
		};

		void GlowHeading(const char* a_text)
		{
			using namespace ImGuiMCP;
			Spacing();
			auto*        dl = GetWindowDrawList();
			const ImVec2 at = GetCursorScreenPos();
			const float  w = GetContentRegionAvail().x;
			const float  h = GetTextLineHeight() + 6.0f;
			ImDrawListManager::AddRectFilledMultiColor(dl, at, ImVec2{ at.x + w, at.y + h }, IM_COL32(255, 186, 90, 46),
				IM_COL32(255, 186, 90, 0), IM_COL32(255, 186, 90, 0), IM_COL32(255, 186, 90, 46));
			ImDrawListManager::AddLine(dl, ImVec2{ at.x, at.y + h }, ImVec2{ at.x + w * 0.55f, at.y + h }, IM_COL32(255, 205, 120, 110), 1.0f);
			Dummy(ImVec2{ 0.0f, 3.0f });
			TextColored(kGold, "  %s", T(a_text));
			Dummy(ImVec2{ 0.0f, 4.0f });
		}

		void Tip(const char* a_text) { ImGuiMCP::SetItemTooltip("%s", T(a_text)); }

		// The settings page edits a copy of the settings (Config), which goes back whole when anything changed (SetConfig):
		// the main thread reads them every frame, so the shared copy is never written half-way. A switch or a choice saves
		// the file at once; a slider when it is let go.
		void Toggle(const char* a_label, bool& a_value, const char* a_tip, bool& a_save)
		{
			a_save |= ImGuiMCP::Checkbox(T(a_label), &a_value);
			Tip(a_tip);
		}

		// a percent slider over a 0..1 (or more) float
		void Percent(const char* a_label, float& a_value, int a_lo, int a_hi, const char* a_tip, bool& a_save)
		{
			int v = static_cast<int>(std::lround(a_value * 100.0f));
			if (ImGuiMCP::SliderInt(T(a_label), &v, a_lo, a_hi, "%d%%")) {
				a_value = static_cast<float>(std::clamp(v, a_lo, a_hi)) / 100.0f;
			}
			a_save |= ImGuiMCP::IsItemDeactivatedAfterEdit();
			Tip(a_tip);
		}

		template <class E>
		void Choice(const char* a_label, E& a_value, const char* const* a_items, int a_count, const char* a_tip, bool& a_save)
		{
			int                      v = static_cast<int>(a_value);
			std::vector<const char*> items;
			for (int i = 0; i < a_count; ++i) {
				items.push_back(T(a_items[i]));
			}
			if (ImGuiMCP::Combo(T(a_label), &v, items.data(), a_count)) {
				a_value = static_cast<E>(std::clamp(v, 0, a_count - 1));
				a_save = true;
			}
			Tip(a_tip);
		}

		void __stdcall RenderSettings()
		{
			const GlowStyle style;
			const Settings  before = Config();
			Settings        s = before;
			auto&           t = s.tuning;
			bool            save = false;
			const float     column = ImGuiMCP::GetContentRegionAvail().x * 0.5f;  // where a paired switch starts
			ImGuiMCP::PushItemWidth(column * 0.9f);

			Toggle("Enabled", s.enabled, "Off: every weapon light and glow is put back exactly as the other mods set it.", save);
			ImGuiMCP::BeginDisabled(!s.enabled);

			GlowHeading("Fading");
			Percent("Brightness when empty", t.floor, 0, 50,
				"What is left of the light at 0% charge. 0% puts it out; 10% (the default) keeps a faint glow so you can "
				"tell the weapon is enchanted but spent.",
				save);
			{
				static const char* const kCurves[] = { TR_MARK("Linear"), TR_MARK("Gentle - stays bright longer") };
				Choice("Curve", t.curve, kCurves, 2, "How the light falls as the charge falls. Gentle holds most of its brightness until the charge is low.",
					save);
			}
			Toggle("Dim the enchantment glow too", s.dimShader,
				"The glow on the blade (the enchantment's shader and its art, VAER's swirls among them) fades with the charge, "
				"like the lights.",
				save);

			GlowHeading("Nearly empty");
			Toggle("Sputter", t.sputter, "Below the level set here, the light sputters: short, irregular dips, deeper toward empty.", save);
			ImGuiMCP::BeginDisabled(!t.sputter);
			Percent("Sputter below", t.sputterBelow, 1, 50, "The charge level where the sputter starts.", save);
			Percent("Sputter strength", t.sputterStrength, 0, 100, "How deep the deepest dip goes, at empty.", save);
			ImGuiMCP::EndDisabled();
			Toggle("Color cooling", t.cool, "Below the sputter level the light's color drains toward grey or a dull ember.", save);
			ImGuiMCP::BeginDisabled(!t.cool);
			{
				static const char* const kTints[] = { TR_MARK("Grey"), TR_MARK("Ember") };
				Choice("Cools toward", t.coolTint, kTints, 2, "Grey: the color drains out. Ember: it turns a dull orange, like a dying fire.", save);
			}
			ImGuiMCP::EndDisabled();

			GlowHeading("Moments");
			Toggle("Hit pulse", t.pulse, "A quick flash when a hit spends charge.", save);
			ImGuiMCP::SameLine(column);
			Toggle("Recharge flare", t.flare, "When a soul gem refills the weapon, the light swells past full and settles.", save);

			GlowHeading("What fades");
			Toggle("Weapon enchantments", s.weapons, "An enchanted weapon's lights and glow follow its charge (staves too).", save);
			ImGuiMCP::SameLine(column);
			Toggle("Spells", s.spells,
				"A spell in hand: the lights and glow on the casting hand follow your magicka, dimming as it runs low and "
				"coming back as it refills.",
				save);
			Toggle("Bound weapons", s.bound,
				"A bound weapon has no charge: its light stays full, then fades over the last seconds of its spell.", save);
			ImGuiMCP::BeginDisabled(!s.bound);
			{
				int secs = static_cast<int>(s.boundFadeSeconds);
				if (ImGuiMCP::SliderInt(T("Bound fade"), &secs, 1, 60, T("last %d s"))) {
					s.boundFadeSeconds = static_cast<float>(std::clamp(secs, 1, 60));
				}
				save |= ImGuiMCP::IsItemDeactivatedAfterEdit();
				Tip("Over how many of the spell's last seconds a bound weapon's light fades.");
			}
			ImGuiMCP::EndDisabled();
			{
				static const char* const kWho[] = { TR_MARK("Player"), TR_MARK("Player and followers") };
				Choice("Whose", s.who, kWho, 2,
					"Followers' weapons usually never lose charge in the base game, so theirs stay full unless another mod "
					"makes them spend it. Their spells follow their magicka.",
					save);
			}

			GlowHeading("HUD");
			Toggle("Hide the charge bar", s.hideChargeBar,
				"The HUD's enchantment charge bar is hidden: the weapon's light shows the charge instead. Works with the "
				"vanilla HUD, SkyHUD and TrueHUD.",
				save);

			ImGuiMCP::EndDisabled();
			ImGuiMCP::PopItemWidth();
			ImGuiMCP::Spacing();
			ImGuiMCP::TextDisabled("%s", T("Finer tuning (staves, each kind of magic by element and school, reach, pulse and flare strength, cooling "
			                             "amount) lives in WaningGlow.ini and the rule files."));

			// only what changed on this page goes back, key by key: a change DevBench made meanwhile is kept
			for (const auto& k : SettingsText::kKeys) {
				if (k.get(s) != k.get(before)) {
					ApplySetting(k.name, k.get(s));
				}
			}
			if (save) {
				SaveSettings();
			}
		}

		void __stdcall RenderDebug()
		{
			const GlowStyle style;
			const auto      hands = Snapshot();
			{
				Settings s = Config();
				bool     save = false;
				Toggle("Debug log", s.debugLog, "Writes each tracked weapon, rule match, pulse and flare to WaningGlow.log.", save);
				if (save) {
					ApplySetting("DebugLog", s.debugLog ? 1 : 0);
					SaveSettings();
				}
			}

			GlowHeading("Preview");
			auto& preview = PreviewState();
			bool  on = preview.on;
			if (ImGuiMCP::Checkbox(T("Pretend the charge is"), &on)) {
				preview.on = on;
			}
			Tip("Every tracked weapon shows this charge instead of its own, to see the fade, sputter and cooling without "
				"fighting. Not saved: it switches off when the game restarts.");
			ImGuiMCP::SameLine();
			{
				int v = static_cast<int>(std::lround(preview.fraction.load() * 100.0f));
				ImGuiMCP::SetNextItemWidth(180.0f);
				if (ImGuiMCP::SliderInt("##previewCharge", &v, 0, 100, "%d%%")) {
					preview.fraction = static_cast<float>(std::clamp(v, 0, 100)) / 100.0f;
				}
			}
			if (ImGuiMCP::Button(T("Pulse"))) {
				preview.pulse = true;
			}
			Tip("The flash a spending hit makes, now.");
			ImGuiMCP::SameLine();
			if (ImGuiMCP::Button(T("Flare"))) {
				preview.flare = true;
			}
			Tip("The swell a recharge makes, now.");

			GlowHeading("Hands");
			if (hands.empty()) {
				ImGuiMCP::TextDisabled("%s", T("No enchanted or bound weapon, and no spell, in hand right now."));
			}
			for (std::size_t i = 0; i < hands.size(); ++i) {
				const auto& h = hands[i];
				ImGuiMCP::PushID(static_cast<int>(i));
				ImGuiMCP::TextColored(kEmber, T("%s - %s hand"), h.actor.c_str(), h.left ? T("left") : T("right"));
				ImGuiMCP::Indent();
				ImGuiMCP::Text("%s", h.weapon.c_str());
				ImGuiMCP::TextDisabled("%s", h.enchantment.c_str());
				if (h.exempt) {
					ImGuiMCP::Text(T("left alone - %s"), h.why.c_str());
				} else {
					if (h.spell) {
						ImGuiMCP::Text(T("magicka %.0f / %.0f - %.0f%%"), h.current, h.max, h.fraction * 100.0f);
					} else if (h.bound) {
						ImGuiMCP::Text(T("%.1f s of %.0f s left - %.0f%%"), h.current, h.max, h.fraction * 100.0f);
					} else {
						ImGuiMCP::Text(T("charge %.0f / %.0f - %.0f%%"), h.current, h.max, h.fraction * 100.0f);
					}
					ImGuiMCP::ProgressBar(h.fraction, ImGuiMCP::ImVec2{ -1.0f, 0.0f }, "");
					ImGuiMCP::Text(T("brightness x%.2f   reach x%.2f   cooling %.0f%%"), h.brightness, h.reach, h.cool * 100.0f);
					ImGuiMCP::TextColored(h.lights ? kDim : kGold, T("%zu light(s) under %zu root(s)%s"), h.lights, h.roots,
						h.lights ? "" : T(" - nothing to dim: no lighting mod lights this weapon?"));
					ImGuiMCP::TextDisabled(T("decided by: %s"), h.why.c_str());
				}
				if (h.chargeAV >= 0.0f && !h.bound && !h.spell) {
					ImGuiMCP::TextDisabled(T("the game's own %s actor value: %.1f"), h.left ? "LeftItemCharge" : "RightItemCharge", h.chargeAV);
				}
				ImGuiMCP::Unindent();
				ImGuiMCP::PopID();
			}

			GlowHeading("Lights");
			ImGuiMCP::Text(T("%zu light(s) being scaled, %zu of them Waning Glow's own; %zu glow(s) dimmed"), ScaledLightCount(), OwnLightCount(),
				DimmedGlowCount());
			if (const auto frozen = FrozenLightCount()) {
				ImGuiMCP::TextColored(kGold, T("%zu light(s) held steady: another plugin scales them the same way"), frozen);
				Tip("Two plugins each scaling the other's output would drive the light to black or white. Waning Glow noticed "
					"and holds its base value; the light still fades, but check which other mod touches weapon lights.");
			}

			GlowHeading("Rule files");
			ImGuiMCP::Text(T("%zu rule(s) from %zu file(s) in Data\\SKSE\\Plugins\\WaningGlow"), RuleCount(), RuleFileCount());
			if (ImGuiMCP::Button(T("Reload rule files"))) {
				// on the game's main thread, between frames: the per-frame pass reads the rules there
				if (auto* tasks = SKSE::GetTaskInterface()) {
					tasks->AddTask([]() { LoadRules(); });
				}
			}
			Tip("Reads the .json files again, so a rule can be edited without restarting the game.");
			for (const auto& p : RuleProblems()) {
				ImGuiMCP::TextColored(kGold, "%s", p.c_str());
			}
		}
	}

	void RegisterMenu()
	{
		if (!SKSEMenuFramework::IsInstalled()) {
			SKSE::log::warn("SKSE Menu Framework is not installed, so there is no settings page; the settings file still applies");
			return;
		}
		SKSEMenuFramework::SetSection(T("Waning Glow"));
		SKSEMenuFramework::AddSectionItem(T("Settings"), RenderSettings);
		SKSEMenuFramework::AddSectionItem(T("Debug"), RenderDebug);
		SKSE::log::info("settings pages added to SKSE Menu Framework {}", SKSEMenuFramework::GetMenuFrameworkVersion());
	}
}
