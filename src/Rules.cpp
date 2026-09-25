// Waning Glow - SKSE plugin
// Copyright (C) 2026 izzydoingit
// GPL-3.0-or-later; see LICENSE.txt and the notice at the top of main.cpp.
//
// Rule files: Data\SKSE\Plugins\WaningGlow\*.json, read in file-name order (case ignored), rules in the order written.
// Every rule that matches a weapon applies, and a later rule's setting replaces an earlier one's - so a mod author's
// file can be overridden by a player's "zz_mine.json". The format is in docs/RULES.md.
//
//   { "rules": [ { "name": "Dawnbreaker always glows", "weapons": ["Skyrim.esm|0x02ACD2"], "mode": "exempt" } ] }
//
// Match fields (a rule with none matches everything):
//   weapons, enchantments   "Plugin.esp|0x123" (or SPID's "0x123~Plugin.esp") or an editor ID
//   weaponKeywords          keyword editor IDs on the weapon
//   effectKeywords          keyword editor IDs on any magic effect of the enchantment (MagicDamageFire, ...)
//   bound, staff            true or false
// Within one field any entry matches; every field given must match.
//
// Before any file: a weapon with the keyword WaningGlow_NoFade (KID or SkyPatcher can hand it out) is left alone, a
// bound weapon follows its spell's time if Bound weapons is on, and a staff is left alone if Staves is off.

#include "Plugin.h"

#include "FormText.h"

#include <nlohmann/json.hpp>

namespace Plugin
{
	namespace
	{
		constexpr const char*      kDir = "Data/SKSE/Plugins/WaningGlow";
		constexpr std::string_view kNoFadeKeyword = "waningglow_nofade";

		// a form named in a rule: resolved to a FormID at load, or kept as a lower-case editor ID
		struct FormRef
		{
			RE::FormID  id{ 0 };
			std::string editorID;
		};

		struct Rule
		{
			std::string              name;  // "file.json #3: name"
			std::vector<FormRef>     weapons, enchantments;
			std::vector<std::string> weaponKeywords, effectKeywords;  // lower case
			std::optional<bool>      bound, staff;

			std::optional<Mode>           mode;
			std::optional<float>          floor, reachFollows, sputterBelow, sputterStrength, pulseStrength, flareStrength,
				coolAmount, boundFadeSeconds;
			std::optional<Glow::Curve>    curve;
			std::optional<Glow::CoolTint> coolTint;
			std::optional<bool>           sputter, emptySteady, pulse, flare, cool;
		};

		// gRules is read and rewritten on the main thread only (the per-frame pass, and a reload queued as a task).
		// The menu, on the render thread, sees only the counts and the problems, under gInfoLock.
		std::vector<Rule>        gRules;
		std::mutex               gInfoLock;
		std::size_t              gRuleCount = 0, gFiles = 0;
		std::vector<std::string> gProblems;

		// which rules match a (weapon, enchantment) pair never changes while a game is loaded: remembered. Keyed by the
		// forms AND their IDs, and forgotten when a game loads: an enchantment made at the table is freed with the game it
		// belongs to, and another can be made at the same address.
		struct PairKey
		{
			const void* weapon;
			const void* ench;
			RE::FormID  weaponID, enchID;
			bool        operator==(const PairKey&) const = default;
		};
		struct PairHash
		{
			std::size_t operator()(const PairKey& a_k) const noexcept
			{
				return std::hash<const void*>{}(a_k.weapon) * 31u ^ std::hash<const void*>{}(a_k.ench) ^
				       (std::size_t{ a_k.enchID } * 0x9E3779B97F4A7C15ull);
			}
		};
		struct Match
		{
			bool                     noFade{ false };  // the weapon carries WaningGlow_NoFade
			std::vector<std::size_t> rules;            // the rules that match, in order
		};
		std::unordered_map<PairKey, Match, PairHash> gMatches;
		std::mutex                                                                                  gMatchLock;

		void Problem(std::string a_text)
		{
			SKSE::log::warn("rules: {}", a_text);
			std::lock_guard lock(gInfoLock);
			gProblems.push_back(std::move(a_text));
		}

		// "Plugin.esp|0x123", "0x123~Plugin.esp", or an editor ID (FormText.h)
		std::optional<FormRef> ParseForm(std::string_view a_text, std::string_view a_where)
		{
			const auto spec = FormText::Parse(a_text);
			switch (spec.kind) {
			case FormText::Spec::Kind::kEditorID:
				return FormRef{ .id = 0, .editorID = Lower(spec.editorID) };
			case FormText::Spec::Kind::kBad:
				Problem(std::format("{}: \"{}\" - not \"Plugin.esp|0x123\", \"0x123~Plugin.esp\" or an editor ID", a_where, a_text));
				return std::nullopt;
			default:
				break;
			}
			auto*       dh = RE::TESDataHandler::GetSingleton();
			const auto* file = dh ? dh->LookupModByName(spec.plugin) : nullptr;
			// LookupForm adds the local ID to the file's index, so it must be the plugin's own part only
			auto* form = file ? dh->LookupForm(FormText::LocalID(spec.id, file->IsLight()), spec.plugin) : nullptr;
			if (!form) {
				// not an error: the rule's plugin may simply not be installed
				SKSE::log::info("rules: {}: \"{}\" is not in this load order; that entry is skipped", a_where, a_text);
				return std::nullopt;
			}
			return FormRef{ .id = form->GetFormID(), .editorID = {} };
		}

		std::vector<FormRef> ParseForms(const nlohmann::json& a_rule, const char* a_key, std::string_view a_where, bool& a_hadAny)
		{
			std::vector<FormRef> out;
			if (!a_rule.contains(a_key)) {
				return out;
			}
			a_hadAny = true;
			for (const auto& v : a_rule[a_key]) {
				if (!v.is_string()) {
					Problem(std::format("{}: every entry of \"{}\" must be a string", a_where, a_key));
					continue;
				}
				if (auto f = ParseForm(v.get<std::string>(), a_where)) {
					out.push_back(std::move(*f));
				}
			}
			return out;
		}

		std::vector<std::string> ParseKeywords(const nlohmann::json& a_rule, const char* a_key, std::string_view a_where)
		{
			std::vector<std::string> out;
			if (a_rule.contains(a_key)) {
				for (const auto& v : a_rule[a_key]) {
					if (v.is_string()) {
						out.push_back(Lower(v.get<std::string>()));
					} else {
						Problem(std::format("{}: every entry of \"{}\" must be a string", a_where, a_key));
					}
				}
			}
			return out;
		}

		template <class T>
		std::optional<T> Get(const nlohmann::json& a_rule, const char* a_key, std::string_view a_where)
		{
			if (!a_rule.contains(a_key)) {
				return std::nullopt;
			}
			try {
				return a_rule[a_key].get<T>();
			} catch (const std::exception&) {
				Problem(std::format("{}: \"{}\" has the wrong type", a_where, a_key));
				return std::nullopt;
			}
		}

		// a percent field, 0..a_hi, as a fraction
		std::optional<float> Percent(const nlohmann::json& a_rule, const char* a_key, std::string_view a_where, float a_hi = 100.0f)
		{
			const auto v = Get<float>(a_rule, a_key, a_where);
			return v ? std::optional<float>(std::clamp(*v, 0.0f, a_hi) / 100.0f) : std::nullopt;
		}

		void ReadRule(const nlohmann::json& a_rule, std::string_view a_file, std::size_t a_index)
		{
			Rule r;
			const auto label = Get<std::string>(a_rule, "name", a_file);
			r.name = std::format("{} #{}{}{}", a_file, a_index + 1, label ? ": " : "", label ? *label : "");
			const std::string_view where = r.name;

			// a misspelt key would otherwise do nothing, silently
			static const std::unordered_set<std::string_view> kKnown{ "name", "weapons", "enchantments", "weaponKeywords",
				"effectKeywords", "bound", "staff", "mode", "emptyBrightness", "curve", "reachFollows", "sputter", "sputterBelow",
				"sputterStrength", "emptySteady", "hitPulse", "hitPulseStrength", "rechargeFlare", "rechargeFlareStrength",
				"colorCooling", "colorCoolingAmount", "colorCoolingTint", "boundFadeSeconds" };
			for (const auto& item : a_rule.items()) {
				if (!kKnown.contains(item.key())) {
					Problem(std::format("{}: unknown key \"{}\" (ignored)", where, item.key()));
				}
			}

			// a rule that names forms none of which are installed must match nothing, not everything
			bool namedWeapons = false, namedEnchantments = false;
			r.weapons = ParseForms(a_rule, "weapons", where, namedWeapons);
			r.enchantments = ParseForms(a_rule, "enchantments", where, namedEnchantments);
			if ((namedWeapons && r.weapons.empty()) || (namedEnchantments && r.enchantments.empty())) {
				SKSE::log::info("rules: {} names no installed form; skipped", where);
				return;
			}
			r.weaponKeywords = ParseKeywords(a_rule, "weaponKeywords", where);
			r.effectKeywords = ParseKeywords(a_rule, "effectKeywords", where);
			r.bound = Get<bool>(a_rule, "bound", where);
			r.staff = Get<bool>(a_rule, "staff", where);

			if (const auto m = Get<std::string>(a_rule, "mode", where)) {
				const auto mode = Lower(*m);
				if (mode == "charge") {
					r.mode = Mode::kCharge;
				} else if (mode == "exempt") {
					r.mode = Mode::kExempt;
				} else if (mode == "bound") {
					r.mode = Mode::kBound;
				} else {
					Problem(std::format("{}: mode \"{}\" is not charge, exempt or bound", where, *m));
				}
			}
			if (const auto c = Get<std::string>(a_rule, "curve", where)) {
				const auto curve = Lower(*c);
				if (curve == "linear") {
					r.curve = Glow::Curve::kLinear;
				} else if (curve == "gentle") {
					r.curve = Glow::Curve::kGentle;
				} else if (curve == "steep") {
					r.curve = Glow::Curve::kSteep;
				} else {
					Problem(std::format("{}: curve \"{}\" is not linear, gentle or steep", where, *c));
				}
			}
			if (const auto c = Get<std::string>(a_rule, "colorCoolingTint", where)) {
				const auto tint = Lower(*c);
				if (tint == "grey" || tint == "gray") {
					r.coolTint = Glow::CoolTint::kGrey;
				} else if (tint == "ember") {
					r.coolTint = Glow::CoolTint::kEmber;
				} else {
					Problem(std::format("{}: colorCoolingTint \"{}\" is not grey or ember", where, *c));
				}
			}
			r.floor = Percent(a_rule, "emptyBrightness", where, 50.0f);
			r.reachFollows = Percent(a_rule, "reachFollows", where);
			r.sputterBelow = Percent(a_rule, "sputterBelow", where, 50.0f);
			r.sputterStrength = Percent(a_rule, "sputterStrength", where);
			r.pulseStrength = Percent(a_rule, "hitPulseStrength", where, 200.0f);
			r.flareStrength = Percent(a_rule, "rechargeFlareStrength", where, 200.0f);
			r.coolAmount = Percent(a_rule, "colorCoolingAmount", where);
			if (const auto s = Get<float>(a_rule, "boundFadeSeconds", where)) {
				r.boundFadeSeconds = std::clamp(*s, 1.0f, 60.0f);
			}
			r.sputter = Get<bool>(a_rule, "sputter", where);
			r.emptySteady = Get<bool>(a_rule, "emptySteady", where);
			r.pulse = Get<bool>(a_rule, "hitPulse", where);
			r.flare = Get<bool>(a_rule, "rechargeFlare", where);
			r.cool = Get<bool>(a_rule, "colorCooling", where);
			gRules.push_back(std::move(r));
		}

		bool FormMatches(const std::vector<FormRef>& a_refs, const RE::TESForm* a_form)
		{
			if (a_refs.empty()) {
				return true;
			}
			if (!a_form) {
				return false;
			}
			std::string id;  // lower-cased once, only if an entry needs it
			for (const auto& ref : a_refs) {
				if (ref.id) {
					if (ref.id == a_form->GetFormID()) {
						return true;
					}
				} else {
					if (id.empty()) {
						id = Lower(EditorID(a_form));
					}
					if (!id.empty() && id == ref.editorID) {
						return true;
					}
				}
			}
			return false;
		}

		bool HasKeyword(const RE::BGSKeywordForm* a_form, const std::vector<std::string>& a_wanted)
		{
			if (!a_form) {
				return false;
			}
			for (const auto* kw : a_form->GetKeywords()) {
				const char* raw = kw ? kw->GetFormEditorID() : nullptr;
				if (raw && *raw) {
					const auto id = Lower(raw);
					if (std::ranges::find(a_wanted, id) != a_wanted.end()) {
						return true;
					}
				}
			}
			return false;
		}

		bool EffectsHaveKeyword(const RE::EnchantmentItem* a_ench, const std::vector<std::string>& a_wanted)
		{
			if (!a_ench) {
				return false;
			}
			for (const auto* eff : a_ench->effects) {
				if (eff && eff->baseEffect && HasKeyword(eff->baseEffect, a_wanted)) {
					return true;
				}
			}
			return false;
		}

		bool Matches(const Rule& a_r, const RE::TESObjectWEAP* a_weapon, const RE::EnchantmentItem* a_ench)
		{
			if (!FormMatches(a_r.weapons, a_weapon) || !FormMatches(a_r.enchantments, a_ench)) {
				return false;
			}
			if (!a_r.weaponKeywords.empty() && !HasKeyword(a_weapon, a_r.weaponKeywords)) {
				return false;
			}
			if (!a_r.effectKeywords.empty() && !EffectsHaveKeyword(a_ench, a_r.effectKeywords)) {
				return false;
			}
			if (a_r.bound && (!a_weapon || a_weapon->IsBound() != *a_r.bound)) {
				return false;
			}
			if (a_r.staff && (!a_weapon || a_weapon->IsStaff() != *a_r.staff)) {
				return false;
			}
			return true;
		}

		// what never changes for a (weapon, enchantment) pair after load, worked out once: keyword names are lower-cased
		// only here, not every frame. The returned reference stays valid: entries are never erased while the game runs.
		const Match& MatchesFor(const RE::TESObjectWEAP* a_weapon, const RE::EnchantmentItem* a_ench)
		{
			std::lock_guard lock(gMatchLock);
			const PairKey   key{ a_weapon, a_ench, a_weapon ? a_weapon->GetFormID() : 0, a_ench ? a_ench->GetFormID() : 0 };
			if (const auto it = gMatches.find(key); it != gMatches.end()) {
				return it->second;
			}
			static const std::vector<std::string> noFade{ std::string(kNoFadeKeyword) };
			Match                                 m;
			m.noFade = HasKeyword(a_weapon, noFade);
			for (std::size_t i = 0; i < gRules.size(); ++i) {
				if (Matches(gRules[i], a_weapon, a_ench)) {
					m.rules.push_back(i);
				}
			}
			if ((m.noFade || !m.rules.empty()) && Config().debugLog) {
				SKSE::log::info("rules: {} with {} - {} rule(s){}", Label(a_weapon), Label(a_ench), m.rules.size(),
					m.noFade ? ", keyword WaningGlow_NoFade" : "");
			}
			return gMatches.emplace(key, std::move(m)).first->second;
		}

		void Overlay(const Rule& a_r, Verdict& a_v)
		{
			auto& t = a_v.tuning;
			if (a_r.mode) a_v.mode = *a_r.mode;
			if (a_r.floor) t.floor = *a_r.floor;
			if (a_r.curve) t.curve = *a_r.curve;
			if (a_r.reachFollows) t.reachFollows = *a_r.reachFollows;
			if (a_r.sputter) t.sputter = *a_r.sputter;
			if (a_r.sputterBelow) t.sputterBelow = *a_r.sputterBelow;
			if (a_r.sputterStrength) t.sputterStrength = *a_r.sputterStrength;
			if (a_r.emptySteady) t.emptySteady = *a_r.emptySteady;
			if (a_r.pulse) t.pulse = *a_r.pulse;
			if (a_r.pulseStrength) t.pulseStrength = *a_r.pulseStrength;
			if (a_r.flare) t.flare = *a_r.flare;
			if (a_r.flareStrength) t.flareStrength = *a_r.flareStrength;
			if (a_r.cool) t.cool = *a_r.cool;
			if (a_r.coolAmount) t.coolAmount = *a_r.coolAmount;
			if (a_r.coolTint) t.coolTint = *a_r.coolTint;
			if (a_r.boundFadeSeconds) a_v.boundFadeSeconds = *a_r.boundFadeSeconds;
		}
	}

	void LoadRules()
	{
		gRules.clear();
		{
			std::lock_guard lock(gInfoLock);
			gProblems.clear();
			gRuleCount = gFiles = 0;
		}
		std::size_t filesRead = 0;
		{
			std::lock_guard lock(gMatchLock);
			gMatches.clear();
		}
		std::error_code ec;
		if (!std::filesystem::is_directory(kDir, ec)) {
			SKSE::log::info("rules: no {} folder; the settings apply to every weapon", kDir);
			return;
		}
		std::vector<std::filesystem::path> files;
		// increment(ec), not a range-for: the range-for's ++ throws on an error reading the folder
		for (auto it = std::filesystem::directory_iterator(kDir, ec); !ec && it != std::filesystem::directory_iterator(); it.increment(ec)) {
			std::error_code fileEc;
			if (it->is_regular_file(fileEc) && Lower(PathText(it->path().extension())) == ".json") {
				files.push_back(it->path());
			}
		}
		std::ranges::sort(files, {}, [](const auto& a_p) { return Lower(PathText(a_p.filename())); });
		for (const auto& path : files) {
			const auto    file = PathText(path.filename());
			std::ifstream in(path);
			if (!in) {
				Problem(std::format("{}: cannot be read", file));
				continue;
			}
			try {
				const auto doc = nlohmann::json::parse(in, nullptr, true, true);  // comments allowed
				if (!doc.is_object() || !doc.contains("rules") || !doc["rules"].is_array()) {
					Problem(std::format("{}: no \"rules\" list", file));
					continue;
				}
				const auto before = gRules.size();
				std::size_t i = 0;
				for (const auto& rule : doc["rules"]) {
					if (rule.is_object()) {
						ReadRule(rule, file, i);
					} else {
						Problem(std::format("{} #{}: a rule must be an object", file, i + 1));
					}
					++i;
				}
				++filesRead;
				SKSE::log::info("rules: {} - {} rule(s) in effect of {}", file, gRules.size() - before, i);
			} catch (const std::exception& e) {
				Problem(std::format("{}: not valid JSON ({})", file, e.what()));
			}
		}
		std::lock_guard lock(gInfoLock);
		gRuleCount = gRules.size();
		gFiles = filesRead;
		SKSE::log::info("rules: {} rule(s) from {} file(s), {} problem(s)", gRuleCount, gFiles, gProblems.size());
	}

	Verdict Judge(const RE::TESObjectWEAP* a_weapon, const RE::EnchantmentItem* a_ench)
	{
		const auto& s = Config();
		Verdict     v{ .mode = Mode::kCharge, .tuning = s.tuning, .boundFadeSeconds = s.boundFadeSeconds, .why = "settings" };
		if (a_weapon) {
			if (a_weapon->IsBound()) {
				v.mode = s.bound ? Mode::kBound : Mode::kExempt;
				v.why = s.bound ? "bound weapon" : "bound weapons off";
			} else if (a_weapon->IsStaff() && !s.staves) {
				v.mode = Mode::kExempt;
				v.why = "staves off";
			}
		}
		const auto& match = MatchesFor(a_weapon, a_ench);
		if (match.noFade) {
			v.mode = Mode::kExempt;
			v.why = "keyword WaningGlow_NoFade";
			return v;  // the keyword is the last word: no file overrides it
		}
		for (const auto i : match.rules) {
			Overlay(gRules[i], v);
			v.why = gRules[i].name;
		}
		return v;
	}

	void ForgetRuleMatches()
	{
		std::lock_guard lock(gMatchLock);
		gMatches.clear();
	}

	std::size_t RuleCount()
	{
		std::lock_guard lock(gInfoLock);
		return gRuleCount;
	}

	std::size_t RuleFileCount()
	{
		std::lock_guard lock(gInfoLock);
		return gFiles;
	}

	std::vector<std::string> RuleProblems()
	{
		std::lock_guard lock(gInfoLock);
		return gProblems;
	}
}
