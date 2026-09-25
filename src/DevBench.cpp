// Waning Glow - SKSE plugin
// Copyright (C) 2026 izzydoingit
// GPL-3.0-or-later; see LICENSE.txt and the notice at the top of main.cpp.
//
// DevBench, when it is in the load order: `inspect kind=waningglow` returns the Debug page as JSON - each tracked hand
// and the fade each of its lights holds right now next to the base it was scaled from - and `menu action=invoke
// name=waningglow` drives the Debug page's preview (set = preview with value = charge 0-1, or -1 to stop; pulse; flare),
// its rule reload (set=reloadrules), and any Settings-page switch (set=setting key=<ini key> value=<n>). DevBench never saves;
// a later change made in the menu saves the settings as they are then.
// Nothing here runs unless DevBench asks.

#include "Plugin.h"

#include "DevBenchAPI.h"

#include <nlohmann/json.hpp>

namespace Plugin
{
	namespace
	{
		constexpr const char* kKey = "waningglow";
		constexpr unsigned    kNeedsBuild = 10500;  // DevBench 1.5.0: RegisterToolExtension

		constexpr const char* kInspect =
			R"({"description":"Waning Glow - each tracked hand (weapon, enchantment, charge, brightness, reach, cooling, lights found), and every light being scaled with the fade it holds now and the base it was scaled from. Read only.","inputSchema":{"type":"object","properties":{}},"readOnly":true})";

		constexpr const char* kMenu =
			R"({"description":"Waning Glow - the Debug page's preview: set=preview value=charge 0..1 (-1 stops), set=pulse, set=flare; set=reloadrules; set=setting key=<WaningGlow.ini key> value=<whole number> flips a Settings-page switch or slider. Not saved from here.","inputSchema":{"type":"object","properties":{"set":{"type":"string"},"key":{"type":"string"},"value":{"type":"number"}}}})";

		using json = nlohmann::json;

		// what Inspect replies, built as JSON so every string is escaped (a name in the ANSI code page is replaced, not
		// passed through) and a number that is not finite is null, never "nan": the reply is always valid
		std::string Dump(const json& a_j) { return a_j.dump(-1, ' ', false, json::error_handler_t::replace); }

		void InspectNow(void*, const char*, void* a_sink, DevBenchAPI::WriteFn a_write)
		{
			if (!a_write) {
				return;
			}
			json out;
			out["hands"] = json::array();
			for (const auto& h : Snapshot()) {
				out["hands"].push_back({ { "actor", h.actor }, { "left", h.left }, { "weapon", h.weapon }, { "enchantment", h.enchantment },
					{ "bound", h.bound }, { "exempt", h.exempt }, { "fraction", h.fraction }, { "current", h.current }, { "max", h.max },
					{ "brightness", h.brightness }, { "reach", h.reach }, { "cool", h.cool }, { "lights", h.lights }, { "roots", h.roots },
					{ "why", h.why } });
			}
			out["lights"] = json::array();
			for (const auto& l : LightsNow()) {
				out["lights"].push_back({ { "fade", l.fade }, { "base", l.base }, { "radius", l.radius }, { "frozen", l.frozen } });
			}
			const auto& p = PreviewState();
			const auto  s = Config();
			out["preview"] = p.on.load();
			out["previewCharge"] = p.fraction.load();
			out["rules"] = RuleCount();
			out["ruleFiles"] = RuleFileCount();
			out["problems"] = RuleProblems().size();
			out["enabled"] = s.enabled;
			out["floor"] = s.tuning.floor;
			out["curve"] = static_cast<int>(s.tuning.curve);
			out["reachFollows"] = s.tuning.reachFollows;
			out["sputter"] = s.tuning.sputter;
			out["cool"] = s.tuning.cool;
			out["pulse"] = s.tuning.pulse;
			out["flare"] = s.tuning.flare;
			out["staves"] = s.staves;
			out["bound"] = s.bound;
			out["who"] = static_cast<int>(s.who);
			out["dimShader"] = s.dimShader;
			a_write(a_sink, Dump(out).c_str());
		}

		// one field of DevBench's arguments, as text ("" when it is missing or not a string or number)
		std::string Field(const json& a_args, const char* a_key)
		{
			if (!a_args.is_object() || !a_args.contains(a_key)) {
				return {};
			}
			const auto& v = a_args[a_key];
			if (v.is_number_float()) {
				// a whole number sent as 5.0 is still 5 (an int setting reads "5", not "5.0")
				const double d = v.get<double>();
				if (std::isfinite(d) && d == std::trunc(d) && std::abs(d) < 1e9) {
					return std::to_string(static_cast<long long>(d));
				}
			}
			return v.is_string() ? v.get<std::string>() : v.is_number() ? v.dump() : std::string();
		}

		void MenuNow(void*, const char* a_args, void* a_sink, DevBenchAPI::WriteFn a_write)
		{
			const auto args = json::parse(a_args ? a_args : "", nullptr, false);  // a discarded value (not an exception) if bad
			const auto             set = Field(args, "set");
			auto&                  p = PreviewState();
			bool                   ok = true;
			if (set == "preview") {
				// value: the charge to preview, 0 to 1; a negative number turns the preview off; no number is refused
				const std::string text = Field(args, "value");
				float             v = 0.0f;
				const auto [end, ec] = std::from_chars(text.data(), text.data() + text.size(), v);  // not the C locale's
				if (text.empty() || ec != std::errc{} || end != text.data() + text.size() || !std::isfinite(v)) {
					ok = false;
				} else {
					if (v >= 0.0f) {
						p.fraction = Glow::Clamp01(v);
					}
					p.on = v >= 0.0f;
				}
			} else if (set == "pulse") {
				p.pulse = true;
			} else if (set == "flare") {
				p.flare = true;
			} else if (set == "setting") {
				// the Settings page's switch, slider or choice, through the same ApplySetting the file uses; not saved from here
				int value = 0;
				ok = SettingsText::ReadInt(Field(args, "value"), value) && ApplySetting(Field(args, "key"), value);
			} else if (set == "reloadrules") {
				// the Debug page's "Reload rule files": on the main thread, between frames
				if (auto* tasks = SKSE::GetTaskInterface()) {
					tasks->AddTask([]() { LoadRules(); });
				}
			} else {
				ok = false;
			}
			if (a_write) {
				a_write(a_sink, ok ? R"({"queued":true})" :
				                     R"({"queued":false,"error":"set is preview, pulse, flare, setting (key, value) or reloadrules"})");
			}
		}

		// DevBench calls these across the DLL boundary: nothing may be thrown back through it
		void Inspect(void* a_ctx, const char* a_args, void* a_sink, DevBenchAPI::WriteFn a_write) noexcept
		{
			try {
				InspectNow(a_ctx, a_args, a_sink, a_write);
			} catch (...) {
				if (a_write) {
					a_write(a_sink, R"({"error":"Waning Glow could not build its report"})");
				}
			}
		}

		void Menu(void* a_ctx, const char* a_args, void* a_sink, DevBenchAPI::WriteFn a_write) noexcept
		{
			try {
				MenuNow(a_ctx, a_args, a_sink, a_write);
			} catch (...) {
				if (a_write) {
					a_write(a_sink, R"({"queued":false,"error":"Waning Glow could not handle that"})");
				}
			}
		}
	}

	void OfferToDevBench()
	{
		auto* devbench = DevBenchAPI::GetDevBenchInterface001();
		if (!devbench || devbench->GetBuildNumber() < kNeedsBuild) {
			return;  // DevBench is not in this load order, or too old; nothing depends on it
		}
		devbench->RegisterToolExtension("inspect", kKey, kInspect, Inspect, nullptr);
		devbench->RegisterMenuHandler(kKey, kMenu, Menu, nullptr);
		SKSE::log::info("DevBench: inspect kind={} and the preview (menu invoke name={}) registered", kKey, kKey);
	}
}
