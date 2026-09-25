// Waning Glow - SKSE plugin
// Copyright (C) 2026 izzydoingit
// GPL-3.0-or-later; see LICENSE.txt and the notice at the top of main.cpp.
//
// The lights on a weapon, and scaling them. This plugin makes no light: it dims the ones other mods hang on a weapon -
// Light Placer's (Vibrant Weapons EAE - Enchantment Lights, Let There Be Glow), lights inside Enchantment Art Extender
// art or a weapon's own mesh, anything that is a NiPointLight in the scene graph under the weapon.
//
// Where a hand's lights hang, all of which are searched every frame:
//   - the weapon's 3D in the actor's biped: the right hand's in its weapon-type slot, the left hand's in the shield slot
//     (the game puts a left-hand weapon there); the player's first-person biped too;
//   - the attach root of every enchantment effect on the weapon (WeaponEnchantmentController::attachRoot), handed over
//     by the reference-effect hooks in main.cpp. Light Placer hangs an enchantment art's lights under that root - and for
//     the player in first person it moves them to the third-person weapon, which the biped search also covers.
//
// When: Light Placer rewrites a flickering or animated light's fade in ReferenceEffect::UpdatePosition and in the cell's
// animation pass; a steady light it sets once. So the scaling runs twice a frame - right after each enchantment effect's
// update (AfterReferenceEffect) and in the player's update (UpdateHands) - and each light keeps Glow::Scaled's
// "what we wrote / what it was" pair, so running twice, or on a light nobody rewrites, never compounds.
//
// When a light stops being under a tracked hand (weapon put away into the inventory, swapped, charge refilled on an
// exempt weapon) it is put back as we found it, unless someone else has written it since.

#include "Plugin.h"

namespace Plugin
{
	namespace
	{
		constexpr std::uint32_t kKeepFrames = 2;      // a light, root or shader not seen for this many frames is let go
		constexpr std::uint32_t kForgetHand = 600;    // a hand not seen for this many frames (~10 s) is forgotten

		struct ColorKeep
		{
			RE::NiColor base{}, written{};  // written: what we wrote last (meaningful once `touched`)
			bool        touched{ false };

			[[nodiscard]] static bool Same(const RE::NiColor& a, const RE::NiColor& b) noexcept
			{
				return a.red == b.red && a.green == b.green && a.blue == b.blue;
			}
			RE::NiColor Apply(const RE::NiColor& a_current, Glow::CoolTint a_tint, float a_amount)
			{
				if (!touched || !Same(a_current, written)) {
					base = a_current;
				}
				const auto c = Glow::Cool({ base.red, base.green, base.blue }, a_tint, a_amount);
				written = { c.r, c.g, c.b };
				touched = true;
				return written;
			}
			[[nodiscard]] RE::NiColor Restore(const RE::NiColor& a_current) const
			{
				return touched && Same(a_current, written) ? base : a_current;
			}
		};

		struct LightSeen
		{
			RE::NiPointer<RE::NiPointLight> light;
			Glow::Scaled               fade, radius;
			ColorKeep                  color;
			std::uint32_t              frame{ 0 };
			std::uint64_t              hand{ 0 };  // the hand whose numbers it last took, for ReapplyAll
		};

		struct ShaderSeen
		{
			RE::NiPointer<RE::ShaderReferenceEffect> effect;
			Glow::Scaled                             fill, rim;
			std::uint32_t                            frame{ 0 };
		};

		struct Root
		{
			RE::NiPointer<RE::NiAVObject> node;
			std::uint32_t                 frame{ 0 };
		};

		struct HandTrack
		{
			RE::ActorHandle          actor;
			bool                     left{ false };
			bool                     active{ false };
			std::uint32_t            frame{ 0 };
			const void*              weapon{ nullptr };
			const void*              instance{ nullptr };
			Glow::Hand               glow{};
			Glow::Output             out{};
			Verdict                  verdict{};
			Reading                  reading{};
			float                    fraction{ 1.0f };
			std::vector<Root>        effectRoots;  // enchantment effects' attach roots, from the reference-effect hooks
			std::size_t              lights{ 0 }, roots{ 0 };
			std::string              actorName;
		};

		std::mutex                                     gLock;  // the player update and the effect hooks may be on different threads
		std::uint32_t                                  gFrame = 1;
		std::unordered_map<std::uint64_t, HandTrack>   gHands;
		std::unordered_map<RE::NiPointLight*, LightSeen> gLights;
		std::unordered_map<const void*, ShaderSeen>    gShaders;
		// how many hands are active, read without the lock by the effect hooks: every art and shader effect in the world
		// calls them each frame, and with no enchanted weapon out they need not look up (RTTI) whose effect it is
		std::atomic<std::size_t>                       gActiveHands{ 0 };

		[[nodiscard]] std::uint64_t Key(RE::ActorHandle a_actor, bool a_left)
		{
			return (static_cast<std::uint64_t>(a_actor.native_handle()) << 1) | (a_left ? 1u : 0u);
		}

		// the weapon's 3D in a biped: a left-hand weapon sits in the shield slot, a right-hand one in its type's slot
		RE::NiAVObject* WeaponPart(RE::Actor* a_actor, bool a_firstPerson, bool a_left, const RE::TESForm* a_weapon)
		{
			const auto& biped = a_actor->GetBiped(a_firstPerson);
			if (!biped || !a_weapon) {
				return nullptr;
			}
			using B = RE::BIPED_OBJECT;
			if (a_left) {
				const auto& shield = biped->objects[B::kShield];
				if (shield.item == a_weapon && shield.partClone) {
					return shield.partClone.get();
				}
				return nullptr;
			}
			for (auto slot = static_cast<std::uint32_t>(B::kHandToHandMelee); slot <= static_cast<std::uint32_t>(B::kCrossbow); ++slot) {
				const auto& obj = biped->objects[slot];
				if (obj.item == a_weapon && obj.partClone) {
					return obj.partClone.get();
				}
			}
			return nullptr;
		}

		void ApplyLight(RE::NiPointLight* a_light, const HandTrack& a_hand)
		{
			auto [it, added] = gLights.try_emplace(a_light);
			auto& seen = it->second;
			if (added) {
				seen.light.reset(a_light);
			}
			seen.frame = gFrame;
			seen.hand = Key(a_hand.actor, a_hand.left);
			auto&       data = a_light->GetLightRuntimeData();
			const auto& t = a_hand.verdict.tuning;
			data.fade = seen.fade.Apply(data.fade, a_hand.out.brightness);
			if (t.reachFollows > 0.0f || seen.radius.Written()) {
				const float r = seen.radius.Apply(data.radius.x, a_hand.out.reach);
				data.radius.x = r;  // x and y are the reach, z is the size
				data.radius.y = r;
			}
			if (a_hand.out.cool > 0.0f || seen.color.touched) {
				data.diffuse = seen.color.Apply(data.diffuse, t.coolTint, a_hand.out.cool);
			}
		}

		void RestoreLight(LightSeen& a_seen)
		{
			if (!a_seen.light) {
				return;
			}
			auto& data = a_seen.light->GetLightRuntimeData();
			data.fade = a_seen.fade.Restore(data.fade);
			if (a_seen.radius.Written()) {
				const float r = a_seen.radius.Restore(data.radius.x);
				data.radius.x = r;
				data.radius.y = r;
			}
			data.diffuse = a_seen.color.Restore(data.diffuse);
		}

		void RestoreShader(ShaderSeen& a_seen)
		{
			auto* data = a_seen.effect ? a_seen.effect->effectShaderData : nullptr;
			if (data) {
				data->fillColor.alpha = a_seen.fill.Restore(data->fillColor.alpha);
				data->rimColor.alpha = a_seen.rim.Restore(data->rimColor.alpha);
			}
		}

		// every NiPointLight under a root takes this hand's numbers; returns how many
		std::size_t ApplyUnder(RE::NiAVObject* a_root, const HandTrack& a_hand)
		{
			std::size_t n = 0;
			if (!a_root) {
				return n;
			}
			RE::BSVisit::TraverseScenegraphLights(a_root, [&](RE::NiPointLight* a_light) {
				if (a_light) {
					ApplyLight(a_light, a_hand);
					++n;
				}
				return RE::BSVisit::BSVisitControl::kContinue;
			});
			return n;
		}

		void ApplyHand(RE::Actor* a_actor, HandTrack& a_hand)
		{
			a_hand.lights = a_hand.roots = 0;
			const auto* weapon = a_hand.reading.weapon;
			std::array<RE::NiAVObject*, 2> parts{ WeaponPart(a_actor, false, a_hand.left, weapon), nullptr };
			if (a_actor->IsPlayerRef()) {
				parts[1] = WeaponPart(a_actor, true, a_hand.left, weapon);
			}
			for (auto* part : parts) {
				if (part) {
					++a_hand.roots;
					a_hand.lights += ApplyUnder(part, a_hand);
				}
			}
			std::erase_if(a_hand.effectRoots, [](const Root& r) { return !r.node || gFrame - r.frame > kKeepFrames; });
			for (auto& root : a_hand.effectRoots) {
				// an effect root under the weapon part was covered above; searching it again changes nothing
				++a_hand.roots;
				a_hand.lights += ApplyUnder(root.node.get(), a_hand);
			}
		}

		std::vector<RE::NiPointer<RE::Actor>> Actors(Who a_who)
		{
			std::vector<RE::NiPointer<RE::Actor>> out;
			auto*                                 player = RE::PlayerCharacter::GetSingleton();
			if (player && player->Is3DLoaded()) {
				out.emplace_back(player);
			}
			if (a_who == Who::kPlayerAndFollowers) {
				if (auto* lists = RE::ProcessLists::GetSingleton()) {
					for (auto& handle : lists->highActorHandles) {
						auto actor = handle.get();
						if (actor && actor.get() != player && actor->IsPlayerTeammate() && actor->Is3DLoaded() && !actor->IsDead()) {
							out.push_back(std::move(actor));
						}
					}
				}
			}
			return out;
		}

		void Sweep()
		{
			for (auto it = gLights.begin(); it != gLights.end();) {
				auto& seen = it->second;
				const bool gone = !seen.light || seen.light->GetRefCount() <= 1;  // only we still hold it: it has left the game
				if (gone || gFrame - seen.frame > kKeepFrames) {
					if (!gone) {
						RestoreLight(seen);
					}
					it = gLights.erase(it);
				} else {
					++it;
				}
			}
			for (auto it = gShaders.begin(); it != gShaders.end();) {
				if (gFrame - it->second.frame > kKeepFrames) {
					RestoreShader(it->second);
					it = gShaders.erase(it);
				} else {
					++it;
				}
			}
			std::erase_if(gHands, [](const auto& kv) { return gFrame - kv.second.frame > kForgetHand; });
		}

		const HandTrack* ActiveHand(RE::Actor* a_actor, bool a_left)
		{
			if (!a_actor) {
				return nullptr;
			}
			const auto it = gHands.find(Key(a_actor->GetHandle(), a_left));
			return it != gHands.end() && it->second.active ? &it->second : nullptr;
		}

		// the hand an enchantment effect belongs to, and its actor; false when it is not a weapon enchantment's
		bool HandOfEffect(RE::ReferenceEffect* a_effect, RE::Actor*& a_actor, bool& a_left, RE::NiAVObject*& a_root)
		{
			auto* ctrl = a_effect ? skyrim_cast<RE::WeaponEnchantmentController*>(a_effect->controller) : nullptr;
			if (!ctrl || !ctrl->target || !ctrl->caster) {
				return false;
			}
			a_actor = ctrl->target;
			a_left = ctrl->caster->castingSource == RE::MagicSystem::CastingSource::kLeftHand;
			a_root = ctrl->attachRoot.get();
			return true;
		}

		// with the debug log on, when a weapon starts being tracked: every light hanging anywhere on the actor, with the nodes
		// above it - so a hand that finds 0 lights says where the lights ARE (a lighting mod hanging them somewhere new)
		void LogActorLights(RE::Actor* a_actor)
		{
			std::size_t n = 0;
			for (const bool first : { false, true }) {
				auto* root = a_actor->Get3D(first);
				if (!root || (first && !a_actor->IsPlayerRef())) {
					continue;
				}
				RE::BSVisit::TraverseScenegraphLights(root, [&](RE::NiPointLight* a_light) {
					if (a_light && n < 16) {
						++n;
						std::string chain;
						for (auto* p = a_light->parent; p && chain.size() < 160; p = p->parent) {
							chain += std::format(" < {}", p->name.empty() ? "(unnamed)" : p->name.c_str());
						}
						const auto& d = a_light->GetLightRuntimeData();
						SKSE::log::info("  light {} ({} view): {}{} | fade {:.2f} radius {:.0f}", n, first ? "first" : "third",
							a_light->name.empty() ? "(unnamed)" : a_light->name.c_str(), chain, d.fade, d.radius.x);
					}
					return RE::BSVisit::BSVisitControl::kContinue;
				});
			}
			SKSE::log::info("  {} light(s) on {} in all", n, a_actor->GetName());
		}

		void LogHand(const HandTrack& a_h, std::string_view a_what)
		{
			if (Config().debugLog) {
				SKSE::log::info("{} {} hand: {} | {} | {} - charge {:.0f}/{:.0f} ({:.0f}%) - {}", a_h.actorName, a_h.left ? "left" : "right",
					a_what, Label(a_h.reading.weapon), Label(a_h.reading.ench), a_h.reading.current, a_h.reading.max,
					a_h.fraction * 100.0f, a_h.verdict.why);
			}
		}
	}

	void UpdateHands(float a_delta)
	{
		std::lock_guard lock(gLock);
		++gFrame;
		const Settings s = Config();  // one copy for the frame
		if (!s.enabled) {
			for (auto& [light, seen] : gLights) {
				RestoreLight(seen);
			}
			for (auto& [key, seen] : gShaders) {
				RestoreShader(seen);
			}
			gLights.clear();
			gShaders.clear();
			gHands.clear();
			gActiveHands = 0;
			return;
		}
		auto&      preview = PreviewState();
		const bool pulseNow = preview.pulse.exchange(false);
		const bool flareNow = preview.flare.exchange(false);
		for (auto& actor : Actors(s.who)) {
			for (const bool left : { false, true }) {
				auto& h = gHands[Key(actor->GetHandle(), left)];
				h.actor = actor->GetHandle();
				h.left = left;
				h.frame = gFrame;
				h.reading = ReadHand(actor.get(), left);
				if (!h.reading.tracked) {
					h.active = false;
					h.weapon = nullptr;
					continue;
				}
				h.verdict = Judge(h.reading.weapon, h.reading.ench);
				if (h.verdict.mode == Mode::kExempt) {
					if (h.active || h.weapon != h.reading.weapon) {
						h.actorName = actor->GetName();
						LogHand(h, "left alone");
					}
					h.active = false;
					h.weapon = h.reading.weapon;
					continue;
				}
				// a bound weapon's fraction is its spell's time left, over the rule's fade window; a bound weapon a rule
				// puts on charge has none, so it stays full, and a charged weapon a rule calls bound follows its charge
				if (h.reading.bound) {
					h.fraction = h.verdict.mode == Mode::kBound ?
					                 Glow::BoundFraction(h.reading.max - h.reading.current, h.reading.max, h.verdict.boundFadeSeconds) :
					                 1.0f;
				} else {
					h.fraction = h.reading.fraction;
				}
				if (!h.active || h.weapon != h.reading.weapon || h.instance != h.reading.instance) {
					h.weapon = h.reading.weapon;
					h.instance = h.reading.instance;
					h.glow.Reset(h.fraction, static_cast<std::uint32_t>(Key(h.actor, left) * 2654435761u));
					h.actorName = actor->GetName();
					LogHand(h, "now tracked");
					if (s.debugLog) {
						LogActorLights(actor.get());
					}
				}
				if (preview.on) {
					h.fraction = Glow::Clamp01(preview.fraction);
				}
				if (pulseNow) {
					h.glow.TriggerPulse(h.verdict.tuning);
				}
				if (flareNow) {
					h.glow.TriggerFlare();
				}
				h.active = true;
				h.out = Glow::Step(h.verdict.tuning, h.glow, h.fraction, a_delta);
				if ((h.out.pulsed || h.out.flared) && s.debugLog) {
					LogHand(h, h.out.pulsed ? "spent charge (pulse)" : "recharged (flare)");
				}
				ApplyHand(actor.get(), h);
			}
		}
		// a hand not read this frame (a follower dismissed, an actor unloaded) is no longer active: the effect hooks must not
		// keep putting last frame's numbers on its lights
		for (auto& [key, hand] : gHands) {
			if (hand.frame != gFrame) {
				hand.active = false;
			}
		}
		Sweep();
		gActiveHands = static_cast<std::size_t>(std::ranges::count_if(gHands, [](const auto& kv) { return kv.second.active; }));
	}

	void AfterReferenceEffect(RE::ReferenceEffect* a_effect)
	{
		if (gActiveHands.load(std::memory_order_relaxed) == 0) {
			return;
		}
		RE::Actor*      actor = nullptr;
		bool            left = false;
		RE::NiAVObject* root = nullptr;
		if (!HandOfEffect(a_effect, actor, left, root)) {
			return;
		}
		std::lock_guard lock(gLock);
		if (!Config().enabled) {
			return;
		}
		auto it = gHands.find(Key(actor->GetHandle(), left));
		if (it == gHands.end() || !it->second.active) {
			return;
		}
		auto& hand = it->second;
		// the effect's own 3D (the art model) and the root it hangs on; Light Placer hangs its lights under the root
		std::array<RE::NiAVObject*, 2> nodes{ root, a_effect->Get3D() };
		for (auto* node : nodes) {
			if (!node) {
				continue;
			}
			auto found = std::ranges::find_if(hand.effectRoots, [node](const Root& r) { return r.node.get() == node; });
			if (found == hand.effectRoots.end()) {
				hand.effectRoots.push_back({ RE::NiPointer<RE::NiAVObject>(node), gFrame });
			} else {
				found->frame = gFrame;
			}
			ApplyUnder(node, hand);  // right after Light Placer wrote this frame's values for this effect
		}
	}

	void AfterShaderEffect(RE::ShaderReferenceEffect* a_effect)
	{
		// the cheap test first: every effect shader in the loaded world comes through here every frame
		if (gActiveHands.load(std::memory_order_relaxed) == 0 || !Config().dimShader) {
			return;
		}
		RE::Actor*      actor = nullptr;
		bool            left = false;
		RE::NiAVObject* root = nullptr;
		if (!a_effect || !a_effect->effectShaderData || !HandOfEffect(a_effect, actor, left, root)) {
			return;
		}
		std::lock_guard lock(gLock);
		const auto*     hand = ActiveHand(actor, left);
		if (!hand) {
			return;
		}
		auto [it, added] = gShaders.try_emplace(a_effect);
		auto& seen = it->second;
		if (added) {
			seen.effect.reset(a_effect);
		}
		seen.frame = gFrame;
		// a shader's alpha tops out at 1: a pulse or flare can brighten a dimmed glow back up, not past its own
		const float k = (std::min)(hand->out.brightness, 1.0f);
		auto*       data = a_effect->effectShaderData;
		data->fillColor.alpha = std::clamp(seen.fill.Apply(data->fillColor.alpha, k), 0.0f, 1.0f);
		data->rimColor.alpha = std::clamp(seen.rim.Apply(data->rimColor.alpha, k), 0.0f, 1.0f);
	}

	void ReapplyAll()
	{
		std::lock_guard lock(gLock);
		if (!Config().enabled) {
			return;
		}
		for (auto& [light, seen] : gLights) {
			if (!seen.light || gFrame - seen.frame > 1) {
				continue;
			}
			const auto it = gHands.find(seen.hand);
			if (it != gHands.end() && it->second.active) {
				ApplyLight(light, it->second);
			}
		}
	}

	void ReleaseAll()
	{
		std::lock_guard lock(gLock);
		for (auto& [light, seen] : gLights) {
			RestoreLight(seen);
		}
		for (auto& [key, seen] : gShaders) {
			RestoreShader(seen);
		}
		gLights.clear();
		gShaders.clear();
		gHands.clear();
		gActiveHands = 0;
	}

	std::vector<HandView> Snapshot()
	{
		std::vector<HandView> out;
		std::lock_guard       lock(gLock);
		for (const auto& [key, h] : gHands) {
			if (!h.reading.tracked || gFrame - h.frame > kKeepFrames) {
				continue;
			}
			HandView v;
			v.actor = h.actorName;
			v.left = h.left;
			v.weapon = Label(h.reading.weapon);
			v.enchantment = h.reading.ench ? Label(h.reading.ench) : std::string(h.reading.bound ? "(bound weapon)" : "(none)");
			v.why = h.verdict.why;
			v.bound = h.reading.bound;
			v.exempt = !h.active;
			v.fraction = h.fraction;
			v.current = h.reading.current;
			v.max = h.reading.max;
			v.brightness = h.active ? h.out.brightness : 1.0f;
			v.reach = h.active ? h.out.reach : 1.0f;
			v.cool = h.active ? h.out.cool : 0.0f;
			v.lights = h.lights;
			v.roots = h.roots;
			if (auto actor = h.actor.get()) {
				if (auto* av = actor->AsActorValueOwner()) {
					v.chargeAV = av->GetActorValue(h.left ? RE::ActorValue::kLeftItemCharge : RE::ActorValue::kRightItemCharge);
				}
			}
			out.push_back(std::move(v));
		}
		std::ranges::sort(out, {}, [](const HandView& v) { return std::make_pair(v.actor, v.left); });
		return out;
	}

	std::vector<LightNow> LightsNow()
	{
		std::vector<LightNow> out;
		std::lock_guard       lock(gLock);
		out.reserve(gLights.size());
		for (const auto& [light, seen] : gLights) {
			if (seen.light) {
				const auto& data = seen.light->GetLightRuntimeData();
				out.push_back({ data.fade, seen.fade.base, data.radius.x, seen.fade.frozen });
			}
		}
		return out;
	}

	std::size_t ScaledLightCount()
	{
		std::lock_guard lock(gLock);
		return gLights.size();
	}

	std::size_t FrozenLightCount()
	{
		std::lock_guard lock(gLock);
		return static_cast<std::size_t>(std::ranges::count_if(gLights, [](const auto& kv) { return kv.second.fade.frozen; }));
	}

	bool Query(RE::Actor* a_actor, bool a_left, float& a_fraction, float& a_brightness)
	{
		std::lock_guard lock(gLock);
		const auto*     hand = ActiveHand(a_actor, a_left);
		if (!hand) {
			return false;
		}
		a_fraction = hand->fraction;
		a_brightness = hand->out.brightness;
		return true;
	}
}
