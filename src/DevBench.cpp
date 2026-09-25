// Waning Glow - SKSE plugin
// Copyright (C) 2026 izzydoingit
// GPL-3.0-or-later; see LICENSE.txt and the notice at the top of main.cpp.
//
// DevBench, when it is in the load order: `inspect kind=waningglow` returns the Debug page as JSON - each tracked hand
// and the fade each of its lights holds right now next to the base it was scaled from - and `menu action=invoke
// name=waningglow` drives the Debug page's preview (set = preview with value = charge 0-1, or -1 to stop; pulse; flare),
// its rule reload (set=reloadrules), and any Settings-page switch (set=setting key=<ini key> value=<n>), never saved.
// Nothing here runs unless DevBench asks.

#include "Plugin.h"

#include "DevBenchAPI.h"

namespace Plugin
{
	namespace
	{
		constexpr const char* kKey = "waningglow";
		constexpr unsigned    kNeedsBuild = 10500;  // DevBench 1.5.0: RegisterToolExtension

		constexpr const char* kInspect =
			R"({"description":"Waning Glow - each tracked hand (weapon, enchantment, charge, brightness, reach, cooling, lights found), and every light being scaled with the fade it holds now and the base it was scaled from. Read only.","inputSchema":{"type":"object","properties":{}},"readOnly":true})";

		constexpr const char* kMenu =
			R"({"description":"Waning Glow - the Debug page's preview: set=preview value=charge 0..1 (-1 stops), set=pulse, set=flare; set=reloadrules; set=setting key=<WaningGlow.ini key> value=<whole number> flips a Settings-page switch or slider. Never saved.","inputSchema":{"type":"object","properties":{"set":{"type":"string"},"key":{"type":"string"},"value":{"type":"number"}}}})";

		std::string Escaped(std::string_view a_s)
		{
			std::string out;
			for (const char c : a_s) {
				if (c == '"' || c == '\\') {
					out += '\\';
				}
				if (static_cast<unsigned char>(c) >= 0x20) {
					out += c;
				}
			}
			return out;
		}

		void Inspect(void*, const char*, void* a_sink, DevBenchAPI::WriteFn a_write)
		{
			if (!a_write) {
				return;
			}
			std::string json = R"({"hands":[)";
			bool        first = true;
			for (const auto& h : Snapshot()) {
				json += std::format(R"({}{{"actor":"{}","left":{},"weapon":"{}","enchantment":"{}","bound":{},"exempt":{},"fraction":{:.3f},"current":{:.1f},"max":{:.1f},"brightness":{:.3f},"reach":{:.3f},"cool":{:.3f},"lights":{},"roots":{},"why":"{}"}})",
					first ? "" : ",", Escaped(h.actor), h.left, Escaped(h.weapon), Escaped(h.enchantment), h.bound, h.exempt, h.fraction,
					h.current, h.max, h.brightness, h.reach, h.cool, h.lights, h.roots, Escaped(h.why));
				first = false;
			}
			json += R"(],"lights":[)";
			first = true;
			for (const auto& l : LightsNow()) {
				json += std::format(R"({}{{"fade":{:.4f},"base":{:.4f},"radius":{:.1f},"frozen":{}}})", first ? "" : ",", l.fade, l.base,
					l.radius, l.frozen);
				first = false;
			}
			const auto& p = PreviewState();
			const auto  s = Config();
			json += std::format(
				R"(],"preview":{},"previewCharge":{:.2f},"rules":{},"ruleFiles":{},"problems":{},"enabled":{},"floor":{:.2f},"curve":{},"reachFollows":{:.2f},"sputter":{},"cool":{},"pulse":{},"flare":{},"staves":{},"bound":{},"who":{},"dimShader":{}}})",
				p.on.load(), p.fraction.load(), RuleCount(), RuleFileCount(), RuleProblems().size(), s.enabled, s.tuning.floor,
				static_cast<int>(s.tuning.curve), s.tuning.reachFollows, s.tuning.sputter, s.tuning.cool, s.tuning.pulse, s.tuning.flare,
				s.staves, s.bound, static_cast<int>(s.who), s.dimShader);
			a_write(a_sink, json.c_str());
		}

		std::string Field(std::string_view a_json, std::string_view a_key)
		{
			const auto k = a_json.find(std::format("\"{}\"", a_key));
			if (k == std::string_view::npos) {
				return {};
			}
			auto i = a_json.find(':', k);
			if (i == std::string_view::npos) {
				return {};
			}
			++i;
			while (i < a_json.size() && (a_json[i] == ' ' || a_json[i] == '"')) {
				++i;
			}
			auto e = i;
			while (e < a_json.size() && a_json[e] != '"' && a_json[e] != ',' && a_json[e] != '}') {
				++e;
			}
			return std::string(a_json.substr(i, e - i));
		}

		void Menu(void*, const char* a_args, void* a_sink, DevBenchAPI::WriteFn a_write)
		{
			const std::string_view args{ a_args ? a_args : "" };
			const auto             set = Field(args, "set");
			auto&                  p = PreviewState();
			bool                   ok = true;
			if (set == "preview") {
				const float v = static_cast<float>(std::atof(Field(args, "value").c_str()));
				const bool  on = std::isfinite(v) && v >= 0.0f;
				if (on) {
					p.fraction = Glow::Clamp01(v);
				}
				p.on = on;
			} else if (set == "pulse") {
				p.pulse = true;
			} else if (set == "flare") {
				p.flare = true;
			} else if (set == "setting") {
				// the Settings page's switch, slider or choice, through the same ApplySetting the file uses; never saved
				ok = ApplySetting(Field(args, "key"), std::atoi(Field(args, "value").c_str()));
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
