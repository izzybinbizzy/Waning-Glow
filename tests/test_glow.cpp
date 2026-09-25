// Waning Glow - tests for src/Glow.h, src/FormText.h, src/SettingsText.h and src/RulesText.h (no game needed).
// Copyright (C) 2026 izzydoingit. GPL-3.0-or-later.
//
// Build and run on any C++23 compiler (build.bat does it through xmake, which fetches the JSON library):
//   g++ -std=c++23 -Wall -Wextra -I../src -I<nlohmann json include> test_glow.cpp -o test_glow && ./test_glow
//   cl /std:c++latest /EHsc /I..\src /I<nlohmann json include> test_glow.cpp && test_glow.exe

#include "FormText.h"
#include "Glow.h"
#include "RulesText.h"
#include "SettingsText.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <iterator>
#include <sstream>

namespace
{
	int gFailed = 0, gPassed = 0;

	void Check(bool a_ok, const char* a_what, int a_line)
	{
		if (a_ok) {
			++gPassed;
		} else {
			++gFailed;
			std::printf("FAIL line %d: %s\n", a_line, a_what);
		}
	}
#define CHECK(x) Check((x), #x, __LINE__)

	bool Near(float a, float b, float eps = 1e-4f) { return std::fabs(a - b) <= eps; }

	// run a hand at a fixed fraction for a while and return the last output
	Glow::Output Run(const Glow::Tuning& t, Glow::Hand& h, float fraction, float seconds, float dt = 1.0f / 60.0f)
	{
		Glow::Output o;
		for (float s = 0.0f; s < seconds; s += dt) {
			o = Glow::Step(t, h, fraction, dt);
		}
		return o;
	}

	void Curves()
	{
		using Glow::Curve;
		CHECK(Near(Glow::ApplyCurve(Curve::kLinear, 0.5f), 0.5f));
		CHECK(Near(Glow::ApplyCurve(Curve::kGentle, 0.5f), 0.75f));
		CHECK(Near(Glow::ApplyCurve(Curve::kSteep, 0.5f), 0.25f));
		for (auto c : { Curve::kLinear, Curve::kGentle, Curve::kSteep }) {
			CHECK(Near(Glow::ApplyCurve(c, 0.0f), 0.0f));
			CHECK(Near(Glow::ApplyCurve(c, 1.0f), 1.0f));
			CHECK(Near(Glow::ApplyCurve(c, -3.0f), 0.0f));  // clamped
			CHECK(Near(Glow::ApplyCurve(c, 7.0f), 1.0f));
			float prev = -1.0f;
			bool  rising = true;
			for (int i = 0; i <= 100; ++i) {
				const float v = Glow::ApplyCurve(c, i / 100.0f);
				rising &= v >= prev;
				prev = v;
			}
			CHECK(rising);
		}
	}

	void Levels()
	{
		Glow::Tuning t;
		t.floor = 0.1f;
		CHECK(Near(Glow::Level(t, 1.0f), 1.0f));
		CHECK(Near(Glow::Level(t, 0.0f), 0.1f));
		t.floor = 0.0f;
		CHECK(Near(Glow::Level(t, 0.0f), 0.0f));
		t.reachFollows = 0.0f;
		CHECK(Near(Glow::Reach(t, 0.0f), 1.0f));  // reach untouched
		t.reachFollows = 1.0f;
		t.minReach = 0.35f;
		CHECK(Near(Glow::Reach(t, 0.0f), 0.35f));  // floored
		CHECK(Near(Glow::Reach(t, 1.0f), 1.0f));
		t.reachFollows = 0.5f;
		CHECK(Near(Glow::Reach(t, 0.5f), 0.75f));
	}

	void FullChargeIsUntouched()
	{
		Glow::Tuning t;
		Glow::Hand   h;
		const auto   o = Run(t, h, 1.0f, 2.0f);
		CHECK(Near(o.brightness, 1.0f));
		CHECK(Near(o.reach, 1.0f));
		CHECK(Near(o.cool, 0.0f));
	}

	void EmptyHoldsTheFloorSteadily()
	{
		Glow::Tuning t;
		t.floor = 0.1f;
		Glow::Hand h;
		Run(t, h, 0.0f, 2.0f);
		float lo = 9.0f, hi = -9.0f;
		for (int i = 0; i < 120; ++i) {
			const auto o = Glow::Step(t, h, 0.0f, 1.0f / 60.0f);
			lo = std::min(lo, o.brightness);
			hi = std::max(hi, o.brightness);
		}
		CHECK(Near(lo, 0.1f, 1e-3f));
		CHECK(Near(hi, 0.1f, 1e-3f));  // emptySteady: no flicker at zero
	}

	void SputterOnlyBelowThreshold()
	{
		Glow::Tuning t;
		t.sputterBelow = 0.15f;
		t.sputterStrength = 0.6f;
		t.pulse = t.flare = false;
		// above the threshold: steady
		{
			Glow::Hand h;
			Run(t, h, 0.5f, 2.0f);
			float lo = 9.0f, hi = -9.0f;
			for (int i = 0; i < 240; ++i) {
				const auto o = Glow::Step(t, h, 0.5f, 1.0f / 60.0f);
				lo = std::min(lo, o.brightness);
				hi = std::max(hi, o.brightness);
			}
			CHECK(Near(lo, hi, 1e-5f));
		}
		// below it: it moves, never above the steady level, never below level * (1 - strength)
		{
			Glow::Hand h;
			Run(t, h, 0.03f, 2.0f);
			const float steady = Glow::Level(t, 0.03f);
			float       lo = 9.0f, hi = -9.0f;
			for (int i = 0; i < 600; ++i) {
				const auto o = Glow::Step(t, h, 0.03f, 1.0f / 60.0f);
				lo = std::min(lo, o.brightness);
				hi = std::max(hi, o.brightness);
			}
			CHECK(hi <= steady + 1e-4f);
			CHECK(lo >= steady * (1.0f - t.sputterStrength) - 1e-4f);
			CHECK(hi - lo > 0.05f * steady);  // visibly moving
		}
		// switched off: steady
		{
			t.sputter = false;
			Glow::Hand h;
			Run(t, h, 0.03f, 2.0f);
			const auto a = Glow::Step(t, h, 0.03f, 1.0f / 60.0f);
			const auto b = Glow::Step(t, h, 0.03f, 1.0f / 60.0f);
			CHECK(Near(a.brightness, b.brightness, 1e-5f));
		}
	}

	void HitPulses()
	{
		Glow::Tuning t;
		t.sputter = false;
		Glow::Hand h;
		Run(t, h, 0.8f, 1.0f);
		const float before = Glow::Level(t, 0.8f);
		const auto  o = Glow::Step(t, h, 0.79f, 1.0f / 60.0f);  // a hit spent 1%
		CHECK(o.pulsed);
		CHECK(o.brightness > before * 1.3f);
		const auto later = Run(t, h, 0.79f, 1.0f);
		CHECK(!later.pulsed);
		CHECK(Near(later.brightness, Glow::Level(t, 0.79f), 2e-3f));  // settled
		// no pulse when switched off
		t.pulse = false;
		Glow::Hand h2;
		Run(t, h2, 0.8f, 1.0f);
		CHECK(!Glow::Step(t, h2, 0.7f, 1.0f / 60.0f).pulsed);
	}

	void RechargeFlares()
	{
		Glow::Tuning t;
		t.sputter = false;
		Glow::Hand h;
		Run(t, h, 0.1f, 2.0f);
		const auto first = Glow::Step(t, h, 1.0f, 1.0f / 60.0f);
		CHECK(first.flared);
		float peak = 0.0f;
		for (int i = 0; i < 40; ++i) {
			peak = std::max(peak, Glow::Step(t, h, 1.0f, 1.0f / 60.0f).brightness);
		}
		CHECK(peak > 1.2f);  // swells past full
		const auto settled = Run(t, h, 1.0f, 2.0f);
		CHECK(Near(settled.brightness, 1.0f, 2e-3f));
	}

	void ResetSkipsEffects()
	{
		// a weapon swap from full to empty is a Reset, not a hit
		Glow::Tuning t;
		Glow::Hand   h;
		Run(t, h, 1.0f, 1.0f);
		h.Reset(0.0f, 42);
		const auto o = Glow::Step(t, h, 0.0f, 1.0f / 60.0f);
		CHECK(!o.pulsed && !o.flared);
		CHECK(Near(o.brightness, t.floor, 1e-3f));
	}

	void EasesDown()
	{
		Glow::Tuning t;
		t.pulse = t.sputter = false;
		Glow::Hand h;
		Run(t, h, 1.0f, 1.0f);
		(void)Glow::Step(t, h, 0.5f, 1.0f / 60.0f);
		CHECK(h.shown > 0.5f && h.shown < 1.0f);  // not a jump
		Run(t, h, 0.5f, 2.0f);
		CHECK(Near(h.shown, 0.5f, 1e-3f));
	}

	void CoolsOnlyNearEmpty()
	{
		Glow::Tuning t;
		t.coolAmount = 0.5f;
		Glow::Hand h;
		CHECK(Near(Run(t, h, 0.5f, 2.0f).cool, 0.0f));
		Glow::Hand h2;
		CHECK(Near(Run(t, h2, 0.0f, 2.0f).cool, 0.5f, 1e-3f));
		const Glow::Rgb fire{ 1.0f, 0.5f, 0.1f };
		const auto      grey = Glow::Cool(fire, Glow::CoolTint::kGrey, 1.0f);
		CHECK(Near(grey.r, grey.g) && Near(grey.g, grey.b));
		const auto same = Glow::Cool(fire, Glow::CoolTint::kEmber, 0.0f);
		CHECK(Near(same.r, fire.r) && Near(same.g, fire.g) && Near(same.b, fire.b));
		const auto ember = Glow::CoolTarget({ 0.2f, 0.4f, 1.0f }, Glow::CoolTint::kEmber);
		CHECK(ember.r > ember.g && ember.g > ember.b);  // a frost light cools to a dull orange
	}

	void BoundTime()
	{
		CHECK(Near(Glow::BoundFraction(0.0f, 120.0f, 10.0f), 1.0f));
		CHECK(Near(Glow::BoundFraction(110.0f, 120.0f, 10.0f), 1.0f));
		CHECK(Near(Glow::BoundFraction(115.0f, 120.0f, 10.0f), 0.5f));
		CHECK(Near(Glow::BoundFraction(130.0f, 120.0f, 10.0f), 0.0f));
		CHECK(Near(Glow::BoundFraction(5.0f, 0.0f, 10.0f), 1.0f));  // no duration
	}

	void ScaledNeverCompoundsWithItself()
	{
		Glow::Scaled s;
		float        v = 2.0f;
		for (int i = 0; i < 100; ++i) {
			v = s.Apply(v, 0.5f);  // a steady light: nobody else writes it
			v = s.Apply(v, 0.5f);  // twice in one frame
		}
		CHECK(Near(v, 1.0f));
		CHECK(Near(s.Restore(v), 2.0f));
	}

	void ScaledFollowsAFlicker()
	{
		// Light Placer rewrites the fade every frame; we must scale the new value, not our old one
		Glow::Scaled s;
		for (int i = 0; i < 200; ++i) {
			const float lp = 1.0f + 0.3f * std::sin(i * 0.7f);
			const float out = s.Apply(lp, 0.5f);
			CHECK(Near(out, lp * 0.5f));
		}
		CHECK(!s.frozen);
	}

	void ScaledStopsALoop()
	{
		// another plugin scaling by 0.9 the same way: without the guard the value falls toward 0
		Glow::Scaled ours, theirs;
		float        v = 1.0f;
		for (int i = 0; i < 400; ++i) {
			v = ours.Apply(v, 0.8f);
			v = theirs.Apply(v, 0.9f);
		}
		CHECK(ours.frozen);
		CHECK(v > 0.05f);  // held, not driven to zero
	}

	// a light's own smooth animation can look like a loop for a while; it must never leave the light pinned
	void ScaledReleasesAnAnimation()
	{
		// a 1 s fade from 0.2 to 1.0, then the owner holds 1.0 (writing it every frame)
		Glow::Scaled s;
		float        v = 0.0f;
		for (int i = 0; i <= 60; ++i) {
			v = s.Apply(0.2f + 0.8f * static_cast<float>(i) / 60.0f, 0.5f);
		}
		for (int i = 0; i < 10; ++i) {
			v = s.Apply(1.0f, 0.5f);
		}
		CHECK(Near(v, 0.5f));                 // owner x factor again
		CHECK(!s.frozen);
		CHECK(Near(s.Restore(v), 1.0f));      // let go: the owner's value, not a pinned one
		// a slow sine pulse (the owner rewriting every frame) at 30, 60 and 144 fps, 1 to 4 s periods
		for (const float fps : { 30.0f, 60.0f, 144.0f }) {
			for (const float period : { 1.0f, 2.0f, 4.0f }) {
				Glow::Scaled p;
				int          off = 0, frames = 0;
				for (float t = 0.0f; t < 20.0f; t += 1.0f / fps) {
					const float owner = 1.0f + 0.25f * std::sin(6.2831853f * t / period);
					const float out = p.Apply(owner, 0.5f);
					off += std::fabs(out - owner * 0.5f) > 0.01f ? 1 : 0;
					++frames;
				}
				CHECK(off * 20 < frames);  // on at least 95% of frames it is exactly owner x factor
			}
		}
	}

	// the shapes a light's owner animates it with - triangles, ramps from dark, a fade-in - at 30, 60 and 144 fps: the output is
	// the owner's value times ours on every frame, give or take the loop probe's dither (0.5%) while a probe runs
	void ScaledReleasesEveryShape()
	{
		auto triangle = [](float a_t, float a_lo, float a_hi, float a_ramp) {
			const float p = std::fmod(a_t, 2.0f * a_ramp) / a_ramp;
			return a_lo + (a_hi - a_lo) * (p < 1.0f ? p : 2.0f - p);
		};
		for (const float fps : { 30.0f, 60.0f, 144.0f }) {
			for (int shape = 0; shape < 4; ++shape) {
				Glow::Scaled s;
				int          off = 0, frames = 0;
				float        worst = 0.0f;
				for (float t = 0.0f; t < 20.0f; t += 1.0f / fps) {
					float owner = 0.0f;
					switch (shape) {
					case 0: owner = triangle(t, 0.5f, 1.0f, 2.0f); break;     // 2 s ramps between 0.5 and 1
					case 1: owner = triangle(t, 0.02f, 1.0f, 1.0f); break;    // 1 s ramps up from nearly dark
					case 2: owner = (std::min)(1.0f, 0.1f + t * 0.3f); break; // a 3 s fade-in, then steady
					default: owner = 0.6f + 0.35f * std::sin(t * 0.8f); break; // an 8 s swell
					}
					const float out = s.Apply(owner, 0.5f);
					const float err = std::fabs(out - owner * 0.5f) / (owner * 0.5f);
					off += err > 0.0051f ? 1 : 0;
					worst = (std::max)(worst, err);
					++frames;
				}
				if (std::getenv("GLOW_TRACE")) {
					std::printf("shape %d at %.0f fps: %d of %d frames off, worst %.3f\n", shape, fps, off, frames, worst);
				}
				CHECK(off == 0);  // never more than the dither: no animation is ever pinned
				CHECK(worst < 0.0051f);
				CHECK(!s.frozen);
			}
		}
	}

	// a bound weapon's light fades with its spell's time: a clock running down is not a hit
	void BoundFadeNeverPulses()
	{
		Glow::Tuning t;
		for (const float fps : { 60.0f, 144.0f }) {
			Glow::Hand h;
			int        pulses = 0, flares = 0;
			float      peak = 0.0f;
			for (float s = 0.0f; s < 12.0f; s += 1.0f / fps) {
				const float f = Glow::BoundFraction(s, 12.0f, 10.0f);  // the last 10 s of a 12 s spell
				const auto  o = Glow::Step(t, h, f, 1.0f / fps, true);
				pulses += o.pulsed ? 1 : 0;
				flares += o.flared ? 1 : 0;
				if (s > 2.2f) {
					peak = (std::max)(peak, o.brightness);
				}
			}
			CHECK(pulses == 0 && flares == 0);
			CHECK(peak <= 1.0f + 1e-4f);  // it only ever dims
		}
	}

	// a charge drained steadily (a concentration staff) is one pulse when it starts, not one every frame (WG-B1: at 60 fps
	// a 4%/s drain pulsed on 99% of frames and held the light 1.47x bright), at any frame rate and any drain rate; hits a
	// second apart still pulse each, and so does a hit while a slow drain runs
	void SteadyDrainPulsesOnce()
	{
		Glow::Tuning t;
		for (const float fps : { 20.0f, 30.0f, 60.0f, 144.0f }) {
			for (const float perSecond : { 0.01f, 0.02f, 0.04f, 0.1f, 0.3f }) {
				Glow::Hand h;
				int        pulses = 0;
				float      f = 1.0f, peak = 0.0f;
				for (float s = 0.0f; s < 3.0f; s += 1.0f / fps) {
					f = (std::max)(0.0f, f - perSecond / fps);
					const auto o = Glow::Step(t, h, f, 1.0f / fps);
					pulses += o.pulsed ? 1 : 0;
					if (s > 1.0f) {
						peak = (std::max)(peak, o.brightness);
					}
				}
				CHECK(pulses <= 1);
				CHECK(peak <= 1.0f + 1e-4f);  // past the first pulse it only dims
			}
			// the same drain applied in steps four times a second
			{
				Glow::Hand h;
				int        pulses = 0;
				float      f = 1.0f;
				int        frame = 0;
				for (float s = 0.0f; s < 5.0f; s += 1.0f / fps, ++frame) {
					if (frame % static_cast<int>(fps / 4.0f) == 0) {
						f -= 0.01f;
					}
					pulses += Glow::Step(t, h, f, 1.0f / fps).pulsed ? 1 : 0;
				}
				CHECK(pulses <= 1);
			}
			// a hit a second: every one pulses
			{
				Glow::Hand h;
				int        pulses = 0;
				float      f = 1.0f;
				for (int frame = 0; frame < static_cast<int>(fps) * 5; ++frame) {  // hits at 1, 2, 3 and 4 s
					if (frame > 0 && frame % static_cast<int>(fps) == 0) {
						f -= 0.05f;
					}
					pulses += Glow::Step(t, h, f, 1.0f / fps).pulsed ? 1 : 0;
				}
				CHECK(pulses == 4);
			}
		}
	}

	// WG-R5-1: a steady drain with the odd long frame (a hitch) is still one pulse; a hit while a drain runs still pulses
	int DrainPulses(float a_fps, float a_perSecond, int a_longEvery, float a_longSeconds, float a_seconds, std::uint32_t a_seed)
	{
		Glow::Tuning t;
		Glow::Hand   h;
		Glow::Rng    r{ a_seed };
		int          pulses = 0;
		float        f = 1.0f;
		for (float s = 0.0f; s < a_seconds;) {
			const bool  isLong = a_longEvery > 0 ? r.Next() < 1.0f / static_cast<float>(a_longEvery) : false;
			const float dt = isLong ? a_longSeconds : 1.0f / a_fps;
			s += dt;
			f = (std::max)(0.0f, f - a_perSecond * dt);
			pulses += Glow::Step(t, h, f, dt).pulsed ? 1 : 0;
		}
		return pulses;
	}

	void DrainWithHitchesPulsesOnce()
	{
		for (const float fps : { 30.0f, 60.0f, 144.0f }) {
			for (const float perSecond : { 0.005f, 0.01f, 0.02f, 0.04f, 0.1f }) {
				for (const int every : { 50, 20, 5 }) {  // 2%, 5% and 20% of frames
					for (const float hitch : { 0.05f, 0.1f, 0.25f }) {
						CHECK(DrainPulses(fps, perSecond, every, hitch, 30.0f, 1234u + static_cast<std::uint32_t>(every)) <= 1);
					}
				}
			}
		}
		// a hit of 5% every 0.8 s while a slow drain runs: each one pulses
		for (const float fps : { 30.0f, 60.0f, 144.0f }) {
			Glow::Tuning t;
			Glow::Hand   h;
			int          pulses = 0;
			float        f = 1.0f;
			const int    hitEvery = static_cast<int>(0.8f * fps);
			for (int frame = 0; frame < static_cast<int>(fps) * 4; ++frame) {
				f -= 0.01f / fps;
				if (frame > 0 && frame % hitEvery == 0) {
					f -= 0.05f;
				}
				pulses += Glow::Step(t, h, f, 1.0f / fps).pulsed ? 1 : 0;
			}
			CHECK(pulses == (static_cast<int>(fps) * 4 - 1) / hitEvery);  // every hit (the drain is too slow to be a fall worth one)
		}
	}

	// WG-R6-1: a drain on its own clock - spent a frame late after a hitch, or in steps with uneven frames - is not hits
	void DrainOnItsOwnClock()
	{
		Glow::Tuning t;
		for (const float perSecond : { 0.005f, 0.01f, 0.02f, 0.05f, 0.1f }) {
			for (const float hitch : { 0.1f, 0.25f, 1.0f, 2.0f }) {
				Glow::Hand h;
				Glow::Rng  r{ 99u };
				int        pulses = 0;
				float      f = 1.0f, owed = 0.0f;
				for (int i = 0; i < 60 * 30; ++i) {
					const float dt = r.Next() < 0.02f ? hitch : 1.0f / 60.0f;
					f = (std::max)(0.0f, f - owed);  // last frame's drain, spent after our read
					owed = perSecond * dt;
					pulses += Glow::Step(t, h, f, dt).pulsed ? 1 : 0;
				}
				CHECK(pulses <= 1);
			}
		}
		for (const float hz : { 3.5f, 4.0f, 4.5f, 5.0f, 6.0f, 10.0f }) {
			Glow::Hand h;
			Glow::Rng  r{ 7u };
			int        pulses = 0;
			float      f = 1.0f, clock = 0.0f;
			for (int i = 0; i < 60 * 30; ++i) {
				const float dt = 0.008f + 0.017f * r.Next();  // 8 to 25 ms frames
				clock += dt;
				if (clock >= 1.0f / hz) {
					clock -= 1.0f / hz;
					f = (std::max)(0.0f, f - 0.01f);
				}
				pulses += Glow::Step(t, h, f, dt).pulsed ? 1 : 0;
			}
			CHECK(pulses <= 1);
		}
		// WG-R7-1: hits half a second apart pulse each at a steady low frame rate too (a cap meant for hitches slowed the
		// let-go on every frame below 30 fps)
		for (const float fps : { 15.0f, 20.0f, 24.0f, 30.0f, 60.0f }) {
			Glow::Hand h;
			int        pulses = 0, hits = 0;
			float      f = 1.0f;
			const int  every = static_cast<int>(fps / 2.0f);
			for (int i = 1; i < static_cast<int>(fps) * 5; ++i) {
				if (i % every == 0) {
					f -= 0.02f;
					++hits;
				}
				pulses += Glow::Step(t, h, f, 1.0f / fps).pulsed ? 1 : 0;
			}
			CHECK(pulses == hits);
		}
		// WG-R8-1: a drain in 5 Hz steps with 250 ms hitches at a steady 20 fps is not hits (a hitch's let-go was a whole
		// 50 ms frame: up to 6 pulses). At 30-60 fps a hitch that swallows two steps can still read as a hit: see Glow.h
		for (const float fps : { 20.0f }) {
			for (std::uint32_t seed = 1; seed <= 20; ++seed) {
				Glow::Hand h;
				Glow::Rng  r{ seed * 2654435761u };
				int        pulses = 0;
				float      f = 1.0f, clock = 0.0f;
				for (float s = 0.0f; s < 30.0f;) {
					const float dt = r.Next() < 0.02f ? 0.25f : 1.0f / fps;
					s += dt;
					clock += dt;
					while (clock >= 0.2f) {
						clock -= 0.2f;
						f = (std::max)(0.0f, f - 0.004f);  // 0.02 a second in 5 Hz steps
					}
					pulses += Glow::Step(t, h, f, dt).pulsed ? 1 : 0;
				}
				CHECK(pulses <= 2);
			}
		}
		// hits of 5% half a second apart while a stepped drain runs: each one pulses
		{
			Glow::Hand h;
			int        pulses = 0, hits = 0;
			float      f = 1.0f;
			for (int i = 1; i < 60 * 5; ++i) {
				if (i % 12 == 0) {
					f -= 0.005f;  // 5 Hz steps
				}
				if (i % 30 == 0) {
					f -= 0.05f;
					++hits;
				}
				pulses += Glow::Step(t, h, f, 1.0f / 60.0f).pulsed ? 1 : 0;
			}
			CHECK(pulses >= hits);
			CHECK(pulses <= hits + 1);  // and at most the drain's start besides
		}
	}

	// the hand's light follows the right thing: a bound weapon a rule treats as bound is a clock (so it never pulses)
	void FollowOfBoundAndCharged()
	{
		const auto charged = Glow::FollowOf(false, false, 0.0f, 0.0f, 10.0f, 0.4f);
		CHECK(charged.fraction == 0.4f && !charged.timed);
		const auto boundOnCharge = Glow::FollowOf(true, false, 5.0f, 60.0f, 10.0f, 0.4f);
		CHECK(Glow::FollowOf(true, true, 30.0f, 60.0f, 10.0f, 0.4f).fraction == 1.0f);  // not yet in the fade window
		CHECK(boundOnCharge.fraction == 1.0f && !boundOnCharge.timed);
		const auto bound = Glow::FollowOf(true, true, 5.0f, 60.0f, 10.0f, 0.4f);  // 5 s left, halfway through a 10 s fade
		CHECK(bound.timed);
		CHECK(std::abs(bound.fraction - 0.5f) < 1e-5f);
		const auto chargedRule = Glow::FollowOf(false, true, 55.0f, 60.0f, 10.0f, 0.7f);  // a charged weapon a rule calls bound
		CHECK(chargedRule.fraction == 0.7f && !chargedRule.timed);
	}

	// a real loop stays caught for as long as it lasts, even while the other plugin's factor drifts slowly
	void ScaledHoldsALongLoop()
	{
		Glow::Scaled ours, theirs;
		float        v = 1.0f;
		for (int i = 0; i < 3000; ++i) {
			const float f = 0.9f - 0.1f * static_cast<float>(i) / 3000.0f;  // the partner's charge falling slowly
			v = ours.Apply(v, 0.8f);
			v = theirs.Apply(v, f);
		}
		CHECK(ours.frozen || theirs.frozen);
		CHECK(v > 0.05f);
	}

	// a rule file: what it reads, what it reports, and how matching rules lay over each other
	void RuleFiles()
	{
		using namespace Plugin::RulesText;
		// the load order: Skyrim.esm is installed (its forms keep their IDs), Missing.esp is not
		const Resolve resolve = [](std::string_view a_plugin, std::uint32_t a_id) -> std::optional<std::uint32_t> {
			if (a_plugin == "Skyrim.esm") {
				return FormText::LocalID(a_id, false);
			}
			return std::nullopt;
		};
		{
			Loaded l;
			CHECK(ReadFile(R"({ "rules": [
				{ "name": "a", "weapons": ["Skyrim.esm|0x0102ACD2", "DA01Dawnbreaker"], "mode": "exempt" },
				{ "name": "b", "effectKeywords": "MagicDamageFire", "curve": "Steep", "sputterBelow": 90, "emptyBrightness": -5,
				  "colorCoolingTint": "gray", "boundFadeSeconds": 0 },
				// a comment
				{ "name": "c", "weapons": ["Missing.esp|0x800"] }
			] })", "mod.json", resolve, l));
			CHECK(l.problems.empty());
			CHECK(l.rules.size() == 2);  // "c" names only a form that is not installed: it matches nothing, so it is skipped
			CHECK(l.notes.size() == 2);  // ...and the log says why (the entry, then the rule)
			const auto& a = l.rules[0];
			CHECK(a.name == "mod.json #1: a" && a.weapons.size() == 2 && a.weapons[0].id == 0x02ACD2 && a.weapons[1].editorID == "da01dawnbreaker");
			CHECK(a.mode == Plugin::Mode::kExempt);
			const auto& b = l.rules[1];
			CHECK(b.effectKeywords == std::vector<std::string>{ "magicdamagefire" });  // one string on its own is a list of one
			CHECK(b.curve == Glow::Curve::kSteep && b.coolTint == Glow::CoolTint::kGrey);
			CHECK(Near(*b.sputterBelow, 0.5f) && Near(*b.floor, 0.0f) && Near(*b.boundFadeSeconds, 1.0f));  // clamped
		}
		{
			// what is wrong is reported, and only that setting is dropped
			Loaded l;
			CHECK(ReadFile(R"({ "rules": [ { "name": "x", "sputter": "yes", "hitPulseStrength": true, "mode": "sometimes",
				"curvee": "linear", "weapons": [7, "Skyrim.esm|0xZZ"], "reachFollows": 40 }, 3 ] })", "bad.json", resolve, l));
			CHECK(l.problems.size() == 7);  // sputter, hitPulseStrength, mode, curvee, 7, 0xZZ, the rule that is 3
			CHECK(l.rules.size() == 0);    // it named weapons, none of them usable: skipped, never "every weapon"
			Loaded m;
			CHECK(ReadFile(R"({ "rules": [ { "sputter": "yes", "reachFollows": 40 } ] })", "bad2.json", resolve, m));
			CHECK(m.rules.size() == 1 && !m.rules[0].sputter && Near(*m.rules[0].reachFollows, 0.4f));
		}
		{
			// not a rule file at all: a problem, never an exception
			for (const char* text : { "not json", R"({ "rules": 5 })", R"([1, 2])", R"({ "rules": [ { "reachFollows": 1e400 } ] })" }) {
				Loaded l;
				bool   threw = false;
				try {
					CHECK(!ReadFile(text, "f.json", resolve, l));
				} catch (...) {
					threw = true;
				}
				CHECK(!threw && l.problems.size() == 1);
			}
		}
		{
			// a later rule's setting replaces an earlier one's; what it does not set is kept
			Loaded l;
			ReadFile(R"({ "rules": [ { "name": "first", "curve": "linear", "hitPulse": false, "mode": "exempt" },
				{ "name": "second", "curve": "steep", "mode": "charge" } ] })", "o.json", resolve, l);
			Plugin::Verdict v;
			for (const auto& r : l.rules) {
				Overlay(r, v);
			}
			CHECK(v.tuning.curve == Glow::Curve::kSteep && !v.tuning.pulse && v.mode == Plugin::Mode::kCharge);
			CHECK(v.why == "o.json #2: second");
		}
	}

	void ScaledRestoreLeavesOthersWrites()
	{
		Glow::Scaled s;
		const float  w = s.Apply(1.0f, 0.5f);
		CHECK(Near(s.Restore(w), 1.0f));
		CHECK(Near(s.Restore(0.77f), 0.77f));  // someone else wrote since: theirs stays
	}

	void PreviewTriggers()
	{
		Glow::Tuning t;
		t.sputter = false;
		Glow::Hand h;
		Run(t, h, 0.6f, 1.0f);
		const float steady = Glow::Level(t, 0.6f);
		h.TriggerPulse(t);
		CHECK(Glow::Step(t, h, 0.6f, 1.0f / 60.0f).brightness > steady * 1.2f);
		CHECK(Near(Run(t, h, 0.6f, 1.0f).brightness, steady, 2e-3f));
		h.TriggerFlare();
		float peak = 0.0f;
		for (int i = 0; i < 40; ++i) {
			peak = std::max(peak, Glow::Step(t, h, 0.6f, 1.0f / 60.0f).brightness);
		}
		CHECK(peak > steady * 1.3f);
		CHECK(Near(Run(t, h, 0.6f, 2.0f).brightness, steady, 2e-3f));
		Glow::Hand fresh;  // a trigger before the first frame must not break the first frame
		fresh.TriggerPulse(t);
		CHECK(std::isfinite(Glow::Step(t, fresh, 0.5f, 1.0f / 60.0f).brightness));
	}

	void FormSpecs()
	{
		using K = FormText::Spec::Kind;
		auto a = FormText::Parse("Skyrim.esm|0x04E4EE");
		CHECK(a.kind == K::kPluginAndID && a.plugin == "Skyrim.esm" && a.id == 0x04E4EE);
		auto b = FormText::Parse("0x04E4EE~Skyrim.esm");
		CHECK(b.kind == K::kPluginAndID && b.plugin == "Skyrim.esm" && b.id == 0x04E4EE);
		auto c = FormText::Parse("  Dawnguard.esm | 4E4EE ");  // spaces, no 0x
		CHECK(c.kind == K::kPluginAndID && c.plugin == "Dawnguard.esm" && c.id == 0x4E4EE);
		auto d = FormText::Parse("DA01Dawnbreaker");
		CHECK(d.kind == K::kEditorID && d.editorID == "DA01Dawnbreaker");
		CHECK(FormText::Parse("Skyrim.esm|0xZZ").kind == K::kBad);
		CHECK(FormText::Parse("|0x123").kind == K::kBad);
		CHECK(FormText::Parse("Skyrim.esm|").kind == K::kBad);
		CHECK(FormText::Parse("").kind == K::kBad);
		CHECK(FormText::Parse("Skyrim.esm|0x123456789").kind == K::kBad);  // more than 8 digits
		CHECK(FormText::LocalID(0x0104E4EE, false) == 0x04E4EE);  // a load-order prefix is dropped
		CHECK(FormText::LocalID(0xFE012345, true) == 0x345);      // a light plugin keeps 12 bits
	}
	// a NaN from the game (a charge read as NaN, a broken frame time) must not stick in the hand's eased level
	void NaNNeverSticks()
	{
		Glow::Tuning t;
		Glow::Hand   h;
		(void)Glow::Step(t, h, 0.5f, 1.0f / 60.0f);
		const auto a = Glow::Step(t, h, std::nanf(""), 1.0f / 60.0f);
		CHECK(std::isfinite(a.brightness) && std::isfinite(a.reach) && std::isfinite(a.cool));
		const auto b = Glow::Step(t, h, 0.5f, std::nanf(""));
		CHECK(std::isfinite(b.brightness));
		const auto c = Run(t, h, 0.5f, 2.0f);  // and the next real frames settle where they would have
		Glow::Hand clean;
		const auto d = Run(t, clean, 0.5f, 2.0f);
		CHECK(Near(c.reach, d.reach, 1e-3f));
	}

	// a darkness light's fade is negative: it must be scaled and put back like any other
	void ScaledHandlesNegativeValues()
	{
		Glow::Scaled s;
		CHECK(!s.Written());
		CHECK(Near(s.Apply(-2.0f, 0.5f), -1.0f));
		CHECK(s.Written());
		CHECK(Near(s.Apply(-1.0f, 0.5f), -1.0f));  // our own write read back: the base stays -2
		CHECK(Near(s.base, -2.0f));
		CHECK(Near(s.Restore(-1.0f), -2.0f));
		CHECK(Near(s.Apply(-3.0f, 0.5f), -1.5f));  // the owner set it again: a new base
		CHECK(Near(s.Restore(-7.0f), -7.0f));      // someone else's write is left alone
		Glow::Scaled never;
		CHECK(Near(never.Restore(0.0f), 0.0f));    // never written: nothing to put back, even for 0
	}

	int Taken(const std::string& a_text, Plugin::Settings& a_s, std::size_t* a_problems = nullptr)
	{
		std::istringstream in(a_text);
		const auto         r = Plugin::SettingsText::Read(in, a_s);
		if (a_problems) {
			*a_problems = r.problems.size();
		}
		return r.taken;
	}

	void SettingsFile()
	{
		using Plugin::Settings;
		{
			// defaults round-trip, and every value changed round-trips
			Settings           d;
			std::ostringstream out;
			Plugin::SettingsText::Write(out, d);
			Settings back;
			back.enabled = false;
			CHECK(Taken(out.str(), back) == static_cast<int>(std::size(Plugin::SettingsText::kKeys)));
			CHECK(back == d);
			Settings c;
			c.enabled = false;
			c.tuning.floor = 0.25f;
			c.tuning.curve = Glow::Curve::kSteep;
			c.tuning.sputterBelow = 0.3f;
			c.tuning.coolTint = Glow::CoolTint::kGrey;
			c.who = Plugin::Who::kPlayerAndFollowers;
			c.boundFadeSeconds = 42.0f;
			c.dimShader = true;
			std::ostringstream out2;
			Plugin::SettingsText::Write(out2, c);
			Settings back2;
			Taken(out2.str(), back2);
			CHECK(back2 == c);
		}
		{
			// as people edit it: Notepad's byte order mark, Windows line ends, any case, spaces, trailing comments
			Settings    s;
			std::size_t problems = 0;
			const int   n = Taken("\xEF\xBB\xBF[settings]\r\n  enabled = 0 ; off for now\r\nEMPTYBRIGHTNESS=20\r\nCurve=2 # steep\r\n", s, &problems);
			CHECK(n == 3 && problems == 0);
			CHECK(!s.enabled && Near(s.tuning.floor, 0.2f) && s.tuning.curve == Glow::Curve::kSteep);
		}
		{
			// what cannot be used keeps the default and is reported: junk after a number, a fraction, an unknown key, another section
			Settings    s;
			std::size_t problems = 0;
			const int   n = Taken("[Settings]\nSputterBelow=15abc\nCurve=1.5\nNoSuchKey=1\nHitPulse=0\n[Other]\nEnabled=0\n", s, &problems);
			CHECK(n == 1 && problems == 3);
			CHECK(Near(s.tuning.sputterBelow, Glow::Tuning{}.sputterBelow) && s.tuning.curve == Glow::Tuning{}.curve);
			CHECK(!s.tuning.pulse && s.enabled);
		}
		{
			// clamped to what the menu allows
			Settings s;
			Taken("[Settings]\nEmptyBrightness=90\nSputterBelow=0\nBoundFadeSeconds=999\nWho=7\nCurve=-4\n", s);
			CHECK(Near(s.tuning.floor, 0.5f) && Near(s.tuning.sputterBelow, 0.01f) && Near(s.boundFadeSeconds, 60.0f));
			CHECK(s.who == Plugin::Who::kPlayerAndFollowers && s.tuning.curve == Glow::Curve::kLinear);
		}
		{
			// one name per setting, and a key matches in any case
			for (const auto& a : Plugin::SettingsText::kKeys) {
				int same = 0;
				for (const auto& b : Plugin::SettingsText::kKeys) {
					same += Plugin::SettingsText::SameText(a.name, b.name) ? 1 : 0;
				}
				CHECK(same == 1);
			}
			Settings s;
			CHECK(Plugin::SettingsText::Apply(s, "debuglog", 1) && s.debugLog);
			CHECK(!Plugin::SettingsText::Apply(s, "Debug_Log", 1));
		}
	}

}

int main()
{
	Curves();
	Levels();
	FullChargeIsUntouched();
	EmptyHoldsTheFloorSteadily();
	SputterOnlyBelowThreshold();
	HitPulses();
	RechargeFlares();
	ResetSkipsEffects();
	EasesDown();
	CoolsOnlyNearEmpty();
	BoundTime();
	ScaledNeverCompoundsWithItself();
	ScaledFollowsAFlicker();
	ScaledStopsALoop();
	ScaledRestoreLeavesOthersWrites();
	FormSpecs();
	PreviewTriggers();
	NaNNeverSticks();
	RuleFiles();
	ScaledReleasesAnAnimation();
	ScaledHoldsALongLoop();
	ScaledReleasesEveryShape();
	BoundFadeNeverPulses();
	SteadyDrainPulsesOnce();
	DrainWithHitchesPulsesOnce();
	DrainOnItsOwnClock();
	FollowOfBoundAndCharged();
	ScaledHandlesNegativeValues();
	SettingsFile();
	std::printf("%d passed, %d failed\n", gPassed, gFailed);
	return gFailed ? EXIT_FAILURE : EXIT_SUCCESS;
}
