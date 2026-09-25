// Waning Glow - tests for src/Glow.h and src/FormText.h (no game needed).
// Copyright (C) 2026 izzydoingit. GPL-3.0-or-later.
//
// Build and run on any C++20 compiler:
//   g++ -std=c++20 -Wall -Wextra -I../src test_glow.cpp -o test_glow && ./test_glow
//   cl /std:c++20 /EHsc /I..\src test_glow.cpp && test_glow.exe

#include "FormText.h"
#include "Glow.h"

#include <cstdio>
#include <cstdlib>

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
	std::printf("%d passed, %d failed\n", gPassed, gFailed);
	return gFailed ? EXIT_FAILURE : EXIT_SUCCESS;
}
