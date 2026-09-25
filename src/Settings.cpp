// Waning Glow - SKSE plugin
// Copyright (C) 2026 izzydoingit
// GPL-3.0-or-later; see LICENSE.txt and the notice at the top of main.cpp.
//
// The settings file, Data\SKSE\Plugins\WaningGlow.ini. Read once at data load, written whenever the menu changes one.
// Percent values are whole numbers; a missing line keeps its default.
//
//   [Settings]
//   Enabled=1
//   EmptyBrightness=10        percent, 0 to 50 - what is left of the light at 0% charge
//   Curve=1                   0 linear, 1 gentle (stays bright longer), 2 steep (drops early)
//   ReachFollows=50           percent, 0 to 100 - how much of the fade the reach follows too
//   Sputter=1
//   SputterBelow=15           percent of charge
//   SputterStrength=60        percent - the deepest dip, at empty
//   EmptySteady=1             at exactly 0% the light holds still
//   HitPulse=1
//   HitPulseStrength=60       percent
//   RechargeFlare=1
//   RechargeFlareStrength=80  percent
//   ColorCooling=1
//   ColorCoolingAmount=50     percent
//   ColorCoolingTint=1        0 grey, 1 ember
//   Staves=1
//   BoundWeapons=1
//   BoundFadeSeconds=10       the last seconds of a bound weapon's spell, over which its light fades
//   Who=0                     0 the player, 1 the player and followers
//   DimShader=0               experimental: the enchantment's glow shader follows the charge too
//   DebugLog=0
//
// Threads: the menu writes these on the render thread while the main thread reads them. Every field is a lone bool,
// int or float, written whole, so a frame reads either the old value or the new one - the same trade RELight makes.

#include "Plugin.h"

namespace Plugin
{
	namespace
	{
		constexpr const char* kPath = "Data/SKSE/Plugins/WaningGlow.ini";

		Settings gSettings;

		std::string Trim(std::string s)
		{
			while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) {
				s.pop_back();
			}
			std::size_t i = 0;
			while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i]))) {
				++i;
			}
			return s.substr(i);
		}

		float Pct(int a_v, int a_lo, int a_hi) { return static_cast<float>(std::clamp(a_v, a_lo, a_hi)) / 100.0f; }
		int   ToPct(float a_v) { return static_cast<int>(std::lround(a_v * 100.0f)); }
	}

	std::string Lower(std::string_view a_text)
	{
		std::string out(a_text);
		for (auto& c : out) {
			c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
		}
		return out;
	}

	Settings& Config() { return gSettings; }

	Preview& PreviewState()
	{
		static Preview preview;
		return preview;
	}

	// one [Settings] line, as the file and the menu name it; false for a key it does not know. The menu, the file and
	// DevBench all come through here, so a value is clamped the same way whichever way it arrives.
	bool ApplySetting(std::string_view key, int v)
	{
		auto& s = gSettings;
		auto& t = s.tuning;
		if (key == "Enabled") {
			s.enabled = v != 0;
		} else if (key == "EmptyBrightness") {
			t.floor = Pct(v, 0, 50);
		} else if (key == "Curve") {
			t.curve = static_cast<Glow::Curve>(std::clamp(v, 0, 2));
		} else if (key == "ReachFollows") {
			t.reachFollows = Pct(v, 0, 100);
		} else if (key == "Sputter") {
			t.sputter = v != 0;
		} else if (key == "SputterBelow") {
			t.sputterBelow = Pct(v, 1, 50);
		} else if (key == "SputterStrength") {
			t.sputterStrength = Pct(v, 0, 100);
		} else if (key == "EmptySteady") {
			t.emptySteady = v != 0;
		} else if (key == "HitPulse") {
			t.pulse = v != 0;
		} else if (key == "HitPulseStrength") {
			t.pulseStrength = Pct(v, 0, 200);
		} else if (key == "RechargeFlare") {
			t.flare = v != 0;
		} else if (key == "RechargeFlareStrength") {
			t.flareStrength = Pct(v, 0, 200);
		} else if (key == "ColorCooling") {
			t.cool = v != 0;
		} else if (key == "ColorCoolingAmount") {
			t.coolAmount = Pct(v, 0, 100);
		} else if (key == "ColorCoolingTint") {
			t.coolTint = static_cast<Glow::CoolTint>(std::clamp(v, 0, 1));
		} else if (key == "Staves") {
			s.staves = v != 0;
		} else if (key == "BoundWeapons") {
			s.bound = v != 0;
		} else if (key == "BoundFadeSeconds") {
			s.boundFadeSeconds = static_cast<float>(std::clamp(v, 1, 60));
		} else if (key == "Who") {
			s.who = static_cast<Who>(std::clamp(v, 0, 1));
		} else if (key == "DimShader") {
			s.dimShader = v != 0;
		} else if (key == "DebugLog") {
			s.debugLog = v != 0;
		} else {
			return false;
		}
		return true;
	}

	void LoadSettings()
	{
		std::ifstream in(kPath);
		std::string   line, section;
		std::size_t   read = 0;
		auto&         s = gSettings;
		auto&         t = s.tuning;
		while (in && std::getline(in, line)) {
			line = Trim(line);
			if (line.empty() || line[0] == ';' || line[0] == '#') {
				continue;
			}
			if (line.front() == '[' && line.back() == ']') {
				section = line.substr(1, line.size() - 2);
				continue;
			}
			const auto eq = line.find('=');
			if (eq == std::string::npos || section != "Settings") {
				continue;
			}
			const auto key = Trim(line.substr(0, eq));
			const auto val = Trim(line.substr(eq + 1));
			int        v = 0;
			if (std::from_chars(val.data(), val.data() + val.size(), v).ec != std::errc{}) {
				SKSE::log::warn("settings: {}={} is not a whole number; the default stays", key, val);
				continue;
			}
			if (ApplySetting(key, v)) {
				++read;
			} else {
				SKSE::log::warn("settings: unknown key {}", key);
			}
		}
		SKSE::log::info("settings: {} line(s) read from {}; enabled {}, empty brightness {}%, curve {}, reach follows {}%, "
						"sputter {} below {}%, pulse {}, flare {}, cooling {}, staves {}, bound {} ({} s), who {}, shader {}",
			read, kPath, s.enabled, ToPct(t.floor), static_cast<int>(t.curve), ToPct(t.reachFollows), t.sputter,
			ToPct(t.sputterBelow), t.pulse, t.flare, t.cool, s.staves, s.bound, s.boundFadeSeconds, static_cast<int>(s.who),
			s.dimShader);
	}

	void SaveSettings()
	{
		std::ofstream out(kPath, std::ios::trunc);
		if (!out) {
			SKSE::log::warn("settings: {} could not be written", kPath);
			return;
		}
		const auto& s = gSettings;
		const auto& t = s.tuning;
		out << "; Waning Glow - written by its menu (SKSE Menu Framework). Percent values are whole numbers.\n"
			<< "[Settings]\n"
			<< "Enabled=" << (s.enabled ? 1 : 0) << "\n"
			<< "EmptyBrightness=" << ToPct(t.floor) << "\n"
			<< "Curve=" << static_cast<int>(t.curve) << "\n"
			<< "ReachFollows=" << ToPct(t.reachFollows) << "\n"
			<< "Sputter=" << (t.sputter ? 1 : 0) << "\n"
			<< "SputterBelow=" << ToPct(t.sputterBelow) << "\n"
			<< "SputterStrength=" << ToPct(t.sputterStrength) << "\n"
			<< "EmptySteady=" << (t.emptySteady ? 1 : 0) << "\n"
			<< "HitPulse=" << (t.pulse ? 1 : 0) << "\n"
			<< "HitPulseStrength=" << ToPct(t.pulseStrength) << "\n"
			<< "RechargeFlare=" << (t.flare ? 1 : 0) << "\n"
			<< "RechargeFlareStrength=" << ToPct(t.flareStrength) << "\n"
			<< "ColorCooling=" << (t.cool ? 1 : 0) << "\n"
			<< "ColorCoolingAmount=" << ToPct(t.coolAmount) << "\n"
			<< "ColorCoolingTint=" << static_cast<int>(t.coolTint) << "\n"
			<< "Staves=" << (s.staves ? 1 : 0) << "\n"
			<< "BoundWeapons=" << (s.bound ? 1 : 0) << "\n"
			<< "BoundFadeSeconds=" << static_cast<int>(s.boundFadeSeconds) << "\n"
			<< "Who=" << static_cast<int>(s.who) << "\n"
			<< "DimShader=" << (s.dimShader ? 1 : 0) << "\n"
			<< "DebugLog=" << (s.debugLog ? 1 : 0) << "\n";
	}
}
