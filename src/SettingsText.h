// Waning Glow - SKSE plugin
// Copyright (C) 2026 izzydoingit
// GPL-3.0-or-later; see LICENSE.txt and the notice at the top of main.cpp.
//
// The settings, and the settings file's text both ways, with nothing of the game in it (tests/test_glow.cpp reads and
// writes it off the game). Settings.cpp only opens the file and keeps the one shared copy.
//
//   [Settings]
//   Enabled=1
//   EmptyBrightness=10        percent, 0 to 50 - what is left of the light at 0% charge
//   Curve=1                   0 linear, 1 gentle (stays bright longer), 2 steep (drops early)
//   ReachFollows=50           percent, 0 to 100 - how much of the fade the reach follows too
//   Sputter=1
//   SputterBelow=15           percent of charge, 1 to 50
//   SputterStrength=60        percent - the deepest dip, at empty
//   EmptySteady=1             at exactly 0% the light holds still
//   HitPulse=1
//   HitPulseStrength=60       percent, 0 to 200
//   RechargeFlare=1
//   RechargeFlareStrength=80  percent, 0 to 200
//   ColorCooling=1
//   ColorCoolingAmount=50     percent
//   ColorCoolingTint=1        0 grey, 1 ember
//   Staves=1
//   BoundWeapons=1
//   BoundFadeSeconds=10       1 to 60: the last seconds of a bound weapon's spell, over which its light fades
//   Who=0                     0 the player, 1 the player and followers
//   DimShader=0               experimental: the enchantment's glow shader follows the charge too
//   DebugLog=0
//
// Every value is a whole number. The file is read the way people edit it: a byte order mark (Notepad's), any case in
// section and key names, spaces, Windows line ends, and ; or # comments, on their own line or after a value. A line
// that is not a whole number, or names no setting, keeps the default and is reported.

#pragma once

#include "Glow.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <istream>
#include <ostream>
#include <string>
#include <string_view>
#include <vector>

namespace Plugin
{
	enum class Who : int
	{
		kPlayer = 0,
		kPlayerAndFollowers = 1
	};

	struct Settings
	{
		bool         enabled{ true };
		Glow::Tuning tuning{};
		Who          who{ Who::kPlayer };
		bool         staves{ true };
		bool         bound{ true };
		float        boundFadeSeconds{ 10.0f };
		bool         dimShader{ false };  // experimental: the enchantment's glow shader follows the charge too
		bool         debugLog{ false };

		bool operator==(const Settings&) const = default;
	};

	// what a weapon ends up with once the rule files have had their say (Rules.cpp, RulesText.h)
	enum class Mode : int
	{
		kCharge = 0,  // the light follows the enchantment's charge
		kExempt = 1,  // the light is left alone
		kBound = 2    // the light follows the time a bound-weapon spell has left
	};

	struct Verdict
	{
		Mode         mode{ Mode::kCharge };
		Glow::Tuning tuning{};
		float        boundFadeSeconds{ 10.0f };
		std::string  why;  // the rule(s) that decided it, for the menu's debug page
	};

	namespace SettingsText
	{
		[[nodiscard]] inline float Pct(int a_v, int a_lo, int a_hi) { return static_cast<float>(std::clamp(a_v, a_lo, a_hi)) / 100.0f; }
		[[nodiscard]] inline int   ToPct(float a_v) { return static_cast<int>(std::lround(a_v * 100.0f)); }

		// every setting: its key in the file, how it reads back as a whole number, and how a whole number sets it (clamped to
		// what the menu allows). One table, so the file, the menu and DevBench can never disagree.
		struct Key
		{
			const char* name;
			int (*get)(const Settings&);
			void (*set)(Settings&, int);
		};

		inline constexpr Key kKeys[]{
			{ "Enabled", [](const Settings& s) { return s.enabled ? 1 : 0; }, [](Settings& s, int v) { s.enabled = v != 0; } },
			{ "EmptyBrightness", [](const Settings& s) { return ToPct(s.tuning.floor); }, [](Settings& s, int v) { s.tuning.floor = Pct(v, 0, 50); } },
			{ "Curve", [](const Settings& s) { return static_cast<int>(s.tuning.curve); },
				[](Settings& s, int v) { s.tuning.curve = static_cast<Glow::Curve>(std::clamp(v, 0, 2)); } },
			{ "ReachFollows", [](const Settings& s) { return ToPct(s.tuning.reachFollows); },
				[](Settings& s, int v) { s.tuning.reachFollows = Pct(v, 0, 100); } },
			{ "Sputter", [](const Settings& s) { return s.tuning.sputter ? 1 : 0; }, [](Settings& s, int v) { s.tuning.sputter = v != 0; } },
			{ "SputterBelow", [](const Settings& s) { return ToPct(s.tuning.sputterBelow); },
				[](Settings& s, int v) { s.tuning.sputterBelow = Pct(v, 1, 50); } },
			{ "SputterStrength", [](const Settings& s) { return ToPct(s.tuning.sputterStrength); },
				[](Settings& s, int v) { s.tuning.sputterStrength = Pct(v, 0, 100); } },
			{ "EmptySteady", [](const Settings& s) { return s.tuning.emptySteady ? 1 : 0; }, [](Settings& s, int v) { s.tuning.emptySteady = v != 0; } },
			{ "HitPulse", [](const Settings& s) { return s.tuning.pulse ? 1 : 0; }, [](Settings& s, int v) { s.tuning.pulse = v != 0; } },
			{ "HitPulseStrength", [](const Settings& s) { return ToPct(s.tuning.pulseStrength); },
				[](Settings& s, int v) { s.tuning.pulseStrength = Pct(v, 0, 200); } },
			{ "RechargeFlare", [](const Settings& s) { return s.tuning.flare ? 1 : 0; }, [](Settings& s, int v) { s.tuning.flare = v != 0; } },
			{ "RechargeFlareStrength", [](const Settings& s) { return ToPct(s.tuning.flareStrength); },
				[](Settings& s, int v) { s.tuning.flareStrength = Pct(v, 0, 200); } },
			{ "ColorCooling", [](const Settings& s) { return s.tuning.cool ? 1 : 0; }, [](Settings& s, int v) { s.tuning.cool = v != 0; } },
			{ "ColorCoolingAmount", [](const Settings& s) { return ToPct(s.tuning.coolAmount); },
				[](Settings& s, int v) { s.tuning.coolAmount = Pct(v, 0, 100); } },
			{ "ColorCoolingTint", [](const Settings& s) { return static_cast<int>(s.tuning.coolTint); },
				[](Settings& s, int v) { s.tuning.coolTint = static_cast<Glow::CoolTint>(std::clamp(v, 0, 1)); } },
			{ "Staves", [](const Settings& s) { return s.staves ? 1 : 0; }, [](Settings& s, int v) { s.staves = v != 0; } },
			{ "BoundWeapons", [](const Settings& s) { return s.bound ? 1 : 0; }, [](Settings& s, int v) { s.bound = v != 0; } },
			{ "BoundFadeSeconds", [](const Settings& s) { return static_cast<int>(std::lround(s.boundFadeSeconds)); },
				[](Settings& s, int v) { s.boundFadeSeconds = static_cast<float>(std::clamp(v, 1, 60)); } },
			{ "Who", [](const Settings& s) { return static_cast<int>(s.who); }, [](Settings& s, int v) { s.who = static_cast<Who>(std::clamp(v, 0, 1)); } },
			{ "DimShader", [](const Settings& s) { return s.dimShader ? 1 : 0; }, [](Settings& s, int v) { s.dimShader = v != 0; } },
			{ "DebugLog", [](const Settings& s) { return s.debugLog ? 1 : 0; }, [](Settings& s, int v) { s.debugLog = v != 0; } },
		};

		[[nodiscard]] inline bool SameText(std::string_view a, std::string_view b) noexcept
		{
			return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) {
				return std::tolower(static_cast<unsigned char>(x)) == std::tolower(static_cast<unsigned char>(y));
			});
		}

		[[nodiscard]] inline const Key* Find(std::string_view a_name) noexcept
		{
			for (const auto& k : kKeys) {
				if (SameText(a_name, k.name)) {
					return &k;
				}
			}
			return nullptr;
		}

		[[nodiscard]] inline std::string_view Trim(std::string_view a_s) noexcept
		{
			while (!a_s.empty() && std::isspace(static_cast<unsigned char>(a_s.front()))) {
				a_s.remove_prefix(1);
			}
			while (!a_s.empty() && std::isspace(static_cast<unsigned char>(a_s.back()))) {
				a_s.remove_suffix(1);
			}
			return a_s;
		}

		// a whole number and nothing else ("15abc" and "1.5" are not)
		[[nodiscard]] inline bool ReadInt(std::string_view a_v, int& a_out) noexcept
		{
			int        v = 0;
			const auto [end, ec] = std::from_chars(a_v.data(), a_v.data() + a_v.size(), v);
			if (a_v.empty() || ec != std::errc{} || end != a_v.data() + a_v.size()) {
				return false;
			}
			a_out = v;
			return true;
		}

		// one setting by its key (any case), clamped as the menu clamps it; false for a key it does not know
		inline bool Apply(Settings& a_s, std::string_view a_key, int a_value)
		{
			if (const auto* k = Find(a_key)) {
				k->set(a_s, a_value);
				return true;
			}
			return false;
		}

		struct ReadResult
		{
			int                      taken{ 0 };
			std::vector<std::string> problems;  // "Key=value: why", for the log
		};

		// reads the [Settings] lines into a_s; a line that cannot be used keeps the default and is reported
		inline ReadResult Read(std::istream& a_in, Settings& a_s)
		{
			ReadResult  r;
			std::string line;
			bool        inSettings = false, first = true;
			while (std::getline(a_in, line)) {
				std::string_view l = line;
				if (first && l.starts_with("\xEF\xBB\xBF")) {
					l.remove_prefix(3);  // a UTF-8 byte order mark
				}
				first = false;
				if (const auto c = l.find_first_of(";#"); c != std::string_view::npos) {
					l = l.substr(0, c);
				}
				l = Trim(l);
				if (l.empty()) {
					continue;
				}
				if (l.front() == '[' && l.back() == ']') {
					inSettings = SameText(Trim(l.substr(1, l.size() - 2)), "Settings");
					continue;
				}
				const auto eq = l.find('=');
				if (!inSettings || eq == std::string_view::npos) {
					continue;
				}
				const auto key = Trim(l.substr(0, eq));
				const auto val = Trim(l.substr(eq + 1));
				int        v = 0;
				if (!Find(key)) {
					r.problems.push_back(std::string(key) + ": not a setting");
				} else if (!ReadInt(val, v)) {
					r.problems.push_back(std::string(key) + "=" + std::string(val) + ": not a whole number; the default stays");
				} else {
					Apply(a_s, key, v);
					++r.taken;
				}
			}
			return r;
		}

		inline void Write(std::ostream& a_out, const Settings& a_s)
		{
			a_out << "; Waning Glow - written by its menu (SKSE Menu Framework). Percent values are whole numbers.\n[Settings]\n";
			for (const auto& k : kKeys) {
				a_out << k.name << '=' << k.get(a_s) << '\n';
			}
		}
	}
}
