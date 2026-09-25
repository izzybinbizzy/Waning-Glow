// Waning Glow - SKSE plugin
// Copyright (C) 2026 izzydoingit
// GPL-3.0-or-later; see LICENSE.txt and the notice at the top of main.cpp.
//
// The light's behaviour as plain numbers: no game types, so tests/test_glow.cpp builds and runs it on any compiler.
// A hand's charge fraction goes in; a brightness multiplier, a reach multiplier and a colour blend come out.
//
//   brightness = floor + (1 - floor) * curve(shown)      shown eases toward the real fraction
//              * sputter                                 below the threshold, stepped dips (not a smooth wave)
//              * (1 + pulse)                             a hit that spent charge
//              * (1 + flare)                             a soul gem that refilled it
//   colour     = lerp(base, cool target, amount)         below the threshold, toward grey or an ember tint

#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <numbers>

namespace Glow
{
	enum class Curve : int
	{
		kLinear = 0,
		kGentle = 1,  // stays bright longer, drops near the end
		kSteep = 2    // drops early
	};

	enum class CoolTint : int
	{
		kGrey = 0,
		kEmber = 1
	};

	struct Tuning
	{
		float    floor{ 0.10f };          // brightness left at 0% charge
		Curve    curve{ Curve::kGentle };
		float    reachFollows{ 0.5f };    // 0: reach never changes, 1: reach follows the brightness curve fully
		float    minReach{ 0.35f };       // reach never drops below this share of the light's own
		bool     sputter{ true };
		float    sputterBelow{ 0.15f };   // fraction under which the light sputters
		float    sputterStrength{ 0.6f }; // deepest dip, at the bottom of the sputter band
		bool     emptySteady{ true };     // at exactly 0 the light holds still at the floor
		bool     pulse{ true };
		float    pulseStrength{ 0.6f };
		float    pulseSeconds{ 0.30f };
		bool     flare{ true };
		float    flareStrength{ 0.8f };
		float    flareSeconds{ 0.70f };
		bool     cool{ true };
		float    coolAmount{ 0.5f };
		CoolTint coolTint{ CoolTint::kEmber };
		float    fallSeconds{ 0.15f };    // how fast the shown level follows a drop
		float    riseSeconds{ 0.35f };    // and a rise

		bool operator==(const Tuning&) const = default;
	};

	[[nodiscard]] inline float Clamp01(float a_v) noexcept { return std::clamp(a_v, 0.0f, 1.0f); }

	[[nodiscard]] inline float ApplyCurve(Curve a_curve, float a_f) noexcept
	{
		a_f = Clamp01(a_f);
		switch (a_curve) {
		case Curve::kGentle:
			return 1.0f - (1.0f - a_f) * (1.0f - a_f);
		case Curve::kSteep:
			return a_f * a_f;
		default:
			return a_f;
		}
	}

	// the steady brightness a fraction gives, before sputter, pulse and flare
	[[nodiscard]] inline float Level(const Tuning& a_t, float a_fraction) noexcept
	{
		const float floor = Clamp01(a_t.floor);
		return floor + (1.0f - floor) * ApplyCurve(a_t.curve, a_fraction);
	}

	// reach follows the level by `reachFollows`, never under `minReach`
	[[nodiscard]] inline float Reach(const Tuning& a_t, float a_level) noexcept
	{
		const float r = 1.0f + (Clamp01(a_level) - 1.0f) * Clamp01(a_t.reachFollows);
		return std::clamp(r, Clamp01(a_t.minReach), 1.0f);
	}

	// how far below the sputter threshold a fraction sits: 0 at the threshold, 1 at empty
	[[nodiscard]] inline float Depth(const Tuning& a_t, float a_fraction) noexcept
	{
		if (a_t.sputterBelow <= 0.0f || a_fraction >= a_t.sputterBelow) {
			return 0.0f;
		}
		return Clamp01((a_t.sputterBelow - a_fraction) / a_t.sputterBelow);
	}

	// a small deterministic generator, so a hand's sputter needs no shared state and tests are repeatable
	struct Rng
	{
		std::uint32_t state{ 0x9E3779B9u };
		[[nodiscard]] float Next() noexcept  // [0, 1)
		{
			state ^= state << 13;
			state ^= state >> 17;
			state ^= state << 5;
			return static_cast<float>(state >> 8) * (1.0f / 16777216.0f);
		}
	};

	// Everything one hand remembers from frame to frame.
	struct Hand
	{
		float shown{ -1.0f };  // the eased fraction; < 0 until the first frame
		float lastFraction{ -1.0f };
		float pulseAge{ 1e9f };
		float pulseSize{ 0.0f };
		float flareAge{ 1e9f };
		// sputter: time into the current step, how long it lasts, the level it holds
		float stepAge{ 0.0f }, stepLength{ 0.0f }, stepLevel{ 1.0f };
		Rng   rng{};

		// the menu's preview buttons: the same pulse a spending hit starts, the same flare a refill starts
		void TriggerPulse(const Tuning& a_t) noexcept
		{
			pulseAge = 0.0f;
			pulseSize = a_t.pulseStrength * (std::max)(shown < 0.0f ? 1.0f : shown, 0.25f);
		}
		void TriggerFlare() noexcept { flareAge = 0.0f; }

		// a new weapon (or none): no pulse, no flare, the level jumps straight to the new fraction
		void Reset(float a_fraction, std::uint32_t a_seed) noexcept
		{
			shown = a_fraction;
			lastFraction = a_fraction;
			pulseAge = flareAge = 1e9f;
			pulseSize = 0.0f;
			stepAge = stepLength = 0.0f;
			stepLevel = 1.0f;
			rng.state = a_seed ? a_seed : 0x9E3779B9u;
		}
	};

	struct Output
	{
		float brightness{ 1.0f };  // multiplies the light's fade
		float reach{ 1.0f };       // multiplies its radius
		float cool{ 0.0f };        // 0..1 blend toward the cool colour
		bool  pulsed{ false };     // this frame started a pulse
		bool  flared{ false };     // this frame started a flare
	};

	// charge that fell by more than this counts as spent on a hit; rose by more, as a recharge
	inline constexpr float kSpendStep = 0.0005f;
	inline constexpr float kRefillStep = 0.01f;

	[[nodiscard]] inline float Ease(float a_from, float a_to, float a_dt, float a_seconds) noexcept
	{
		if (a_seconds <= 0.0f) {
			return a_to;
		}
		const float k = 1.0f - std::exp(-a_dt / a_seconds);
		return a_from + (a_to - a_from) * k;
	}

	// One frame of one hand. `a_fraction` is the charge fraction (0..1), `a_dt` the frame time in seconds.
	[[nodiscard]] inline Output Step(const Tuning& a_t, Hand& a_h, float a_fraction, float a_dt) noexcept
	{
		Output out;
		// a NaN would pass through std::clamp and stay in the hand's eased level for good: a fraction the game could not
		// give counts as full (no dimming), a frame time it could not give as no time
		a_fraction = std::isfinite(a_fraction) ? Clamp01(a_fraction) : 1.0f;
		a_dt = std::isfinite(a_dt) ? std::clamp(a_dt, 0.0f, 0.25f) : 0.0f;
		if (a_h.shown < 0.0f) {
			a_h.Reset(a_fraction, a_h.rng.state);
		}

		// what changed since last frame
		const float delta = a_fraction - a_h.lastFraction;
		if (delta < -kSpendStep && a_t.pulse) {
			a_h.pulseAge = 0.0f;
			a_h.pulseSize = a_t.pulseStrength * (std::max)(a_h.shown, 0.25f);  // a near-empty hit still shows
			out.pulsed = true;
		} else if (delta > kRefillStep && a_t.flare) {
			a_h.flareAge = 0.0f;
			out.flared = true;
		}
		a_h.lastFraction = a_fraction;

		a_h.shown = Ease(a_h.shown, a_fraction, a_dt, a_fraction < a_h.shown ? a_t.fallSeconds : a_t.riseSeconds);
		const float shown = a_h.shown;

		float level = Level(a_t, shown);

		// sputter: stepped dips, deeper the closer to empty; empty itself holds still if emptySteady
		const float depth = a_t.sputter ? Depth(a_t, shown) : 0.0f;
		const bool  empty = shown <= 0.001f;
		if (depth > 0.0f && !(empty && a_t.emptySteady)) {
			a_h.stepAge += a_dt;
			if (a_h.stepAge >= a_h.stepLength) {
				a_h.stepAge = 0.0f;
				// short, irregular steps: most near full, some deep - a failing enchantment, not a breathing one
				a_h.stepLength = 0.04f + 0.14f * a_h.rng.Next();
				const float roll = a_h.rng.Next();
				const float dip = roll < 0.35f ? (0.55f + 0.45f * a_h.rng.Next()) : 0.15f * a_h.rng.Next();
				a_h.stepLevel = 1.0f - Clamp01(a_t.sputterStrength) * depth * dip;
			}
			level *= a_h.stepLevel;
		} else {
			a_h.stepLevel = 1.0f;
			a_h.stepAge = a_h.stepLength = 0.0f;
		}

		// pulse: a quick spike that falls off (square of the time left)
		float boost = 0.0f;
		if (a_h.pulseAge < a_t.pulseSeconds) {
			const float left = 1.0f - a_h.pulseAge / a_t.pulseSeconds;
			boost += a_h.pulseSize * left * left;
			a_h.pulseAge += a_dt;
		}
		// flare: a swell and settle (half a sine)
		if (a_h.flareAge < a_t.flareSeconds) {
			boost += a_t.flareStrength * std::sin(std::numbers::pi_v<float> * a_h.flareAge / a_t.flareSeconds);
			a_h.flareAge += a_dt;
		}

		out.brightness = level * (1.0f + boost);
		out.reach = Reach(a_t, Level(a_t, shown));  // reach ignores the flicker and spikes: only the steady level
		out.cool = a_t.cool ? Clamp01(a_t.coolAmount) * Depth(Tuning{ .sputterBelow = a_t.sputterBelow }, shown) : 0.0f;
		return out;
	}

	// Bound weapons have no charge: the fraction is the time the spell has left, full until the last `a_fadeSeconds`.
	[[nodiscard]] inline float BoundFraction(float a_elapsed, float a_duration, float a_fadeSeconds) noexcept
	{
		if (a_duration <= 0.0f) {
			return 1.0f;  // no duration: it lasts until dispelled
		}
		const float left = a_duration - a_elapsed;
		if (a_fadeSeconds <= 0.0f) {
			return left > 0.0f ? 1.0f : 0.0f;
		}
		return Clamp01(left / a_fadeSeconds);
	}

	struct Rgb
	{
		float r{ 1.0f }, g{ 1.0f }, b{ 1.0f };
	};

	// the colour a light cools toward: its own brightness in grey, or that brightness in a dull ember
	[[nodiscard]] inline Rgb CoolTarget(const Rgb& a_c, CoolTint a_tint) noexcept
	{
		const float luma = 0.2126f * a_c.r + 0.7152f * a_c.g + 0.0722f * a_c.b;
		if (a_tint == CoolTint::kGrey) {
			return { luma, luma, luma };
		}
		return { std::min(luma * 1.45f, 1.0f), luma * 0.62f, luma * 0.30f };
	}

	[[nodiscard]] inline Rgb Cool(const Rgb& a_c, CoolTint a_tint, float a_amount) noexcept
	{
		const Rgb   t = CoolTarget(a_c, a_tint);
		const float k = Clamp01(a_amount);
		return { a_c.r + (t.r - a_c.r) * k, a_c.g + (t.g - a_c.g) * k, a_c.b + (t.b - a_c.b) * k };
	}

	// A scaler that multiplies a value some other code sets (Light Placer rewrites a flickering light's fade every
	// frame, and leaves a steady one alone). Whatever the value holds that we did not write is the new base; we write
	// base * factor. Calling it twice in one frame writes the same number: it never compounds with itself.
	//
	// It CAN compound with another plugin that scales the same value the same way: each sees the other's write as a
	// new base, so the base shrinks (or grows) by the same ratio every frame. So a base that moves by one steady ratio
	// (not 1) for 30 frames running is taken as that loop, and the base is frozen at its value before the drift.
	//
	// A smooth animation of the light's own (a fade-in, a slow pulse) can look like that loop for a while, so a freeze is
	// checked every frame after: in a real loop what we read back is what we wrote times the other plugin's factor, the
	// same factor frame after frame; when it is not (the owner wrote a value of its own), the freeze ends at once and the
	// value read is the base again. Values may be negative (a darkness light's fade).
	struct Scaled
	{
		float base{ 0.0f };
		float written{ 0.0f };      // what we wrote last (meaningful once `touched`)
		bool  touched{ false };     // we have written this value
		float driftFrom{ 0.0f };    // the base when the current run of steady drift began
		float lastRatio{ 1.0f };
		int   drift{ 0 };           // consecutive frames the base moved by about the same ratio
		bool  frozen{ false };
		float loopFactor{ 1.0f };   // while frozen: what the other plugin multiplies our write by

		static constexpr int   kDriftFrames = 30;
		static constexpr float kLoopTolerance = 0.05f;  // a partner's factor moving more than this in a frame ends the freeze

		[[nodiscard]] float Apply(float a_current, float a_factor) noexcept
		{
			if (!touched) {
				base = a_current;
			} else if (a_current != written) {
				if (frozen) {
					const float f = written != 0.0f ? a_current / written : 0.0f;
					if (written != 0.0f && std::abs(f - loopFactor) <= kLoopTolerance * std::abs(loopFactor)) {
						loopFactor = f;  // still the loop (following a partner whose own factor moves slowly)
					} else {
						Thaw(a_current);  // the owner wrote a value of its own: it is the base
					}
				} else {
					const float ratio = base != 0.0f ? a_current / base : 1.0f;
					const bool  moving = std::abs(ratio - 1.0f) > 0.005f;
					const bool  steady = moving && std::abs(ratio - lastRatio) < 0.02f * std::abs(lastRatio);
					if (!steady || drift == 0) {
						driftFrom = base;
					}
					drift = steady ? drift + 1 : (moving ? 1 : 0);
					lastRatio = ratio;
					if (drift >= kDriftFrames && written != 0.0f) {
						frozen = true;  // another scaler feeds our output back to us: keep the base from before the loop
						loopFactor = a_current / written;
						base = driftFrom;
					} else {
						base = a_current;
					}
				}
			}
			const float v = base * a_factor;
			written = v;
			touched = true;
			return v;
		}

		[[nodiscard]] bool Written() const noexcept { return touched; }

		// what to put back when we let go: our base if the value still holds our write, else leave it
		[[nodiscard]] float Restore(float a_current) const noexcept { return touched && a_current == written ? base : a_current; }

	private:
		void Thaw(float a_current) noexcept
		{
			frozen = false;
			drift = 0;
			lastRatio = 1.0f;
			base = a_current;
		}
	};
}
