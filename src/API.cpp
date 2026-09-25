// Waning Glow - SKSE plugin
// Copyright (C) 2026 izzydoingit
// GPL-3.0-or-later; see LICENSE.txt and the notice at the top of main.cpp.
//
// What other plugins can ask: plain C exports, found with GetProcAddress (include/WaningGlowAPI.h does it for them).
// Every function is safe to call from any thread once the game has loaded its data, and cheap enough for every frame.

#include "Plugin.h"

#include "../include/WaningGlowAPI.h"

extern "C" __declspec(dllexport) std::uint32_t WaningGlow_GetAPIVersion()
{
	return WaningGlowAPI::kVersion;
}

// the charge fraction (0..1) of what the actor holds in that hand; -1 when the hand is not tracked
// (nothing enchanted, exempt by a rule or keyword, the mod switched off, or the actor not followed)
extern "C" __declspec(dllexport) float WaningGlow_GetChargeFraction(RE::Actor* a_actor, bool a_leftHand)
{
	float fraction = 0.0f, brightness = 1.0f;
	return Plugin::Query(a_actor, a_leftHand, fraction, brightness) ? fraction : -1.0f;
}

// the multiplier this plugin put on that hand's lights this frame (pulse and flare included; over 1 during them);
// 1 when the hand is not tracked
extern "C" __declspec(dllexport) float WaningGlow_GetLightMultiplier(RE::Actor* a_actor, bool a_leftHand)
{
	float fraction = 0.0f, brightness = 1.0f;
	return Plugin::Query(a_actor, a_leftHand, fraction, brightness) ? brightness : 1.0f;
}
