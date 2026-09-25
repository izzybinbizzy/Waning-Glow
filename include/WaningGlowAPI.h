// Waning Glow - API for other SKSE plugins
// Copyright (C) 2026 izzydoingit
// SPDX-License-Identifier: MIT
//
// Needs CommonLibSSE NG (for RE::Actor and REX::W32). This one header is MIT-licensed, unlike the rest of Waning
// Glow (GPL-3.0-or-later), so any plugin can copy it in whatever its own license.
//
// Waning Glow makes an enchanted weapon's lights follow the weapon's charge. This asks it how full a hand is, and
// how much it is dimming (or, during a pulse or flare, brightening) that hand's lights right now.
//
//   #include "WaningGlowAPI.h"
//   if (WaningGlowAPI::Available()) {                      // after SKSE's kPostLoad message
//       float f = WaningGlowAPI::ChargeFraction(actor, false);   // right hand; -1 if not tracked
//       float m = WaningGlowAPI::LightMultiplier(actor, false);  // 1 if not tracked
//   }
//
// Safe from any thread once the game's data has loaded; cheap enough to call every frame.

#pragma once

#include <cstdint>

// CommonLibSSE NG's own Windows wrappers, so this header works with or without <windows.h>
#include <REX/W32/KERNEL32.h>

namespace RE
{
	class Actor;
}

namespace WaningGlowAPI
{
	inline constexpr std::uint32_t kVersion = 1;

	using GetAPIVersion_t = std::uint32_t (*)();
	using GetChargeFraction_t = float (*)(RE::Actor*, bool);
	using GetLightMultiplier_t = float (*)(RE::Actor*, bool);

#ifndef WANINGGLOW_BUILDING
	namespace detail
	{
		template <class F>
		inline F Find(const char* a_name)
		{
			const auto module = REX::W32::GetModuleHandleA("WaningGlow.dll");
			return module ? reinterpret_cast<F>(REX::W32::GetProcAddress(module, a_name)) : nullptr;
		}
	}

	// true if Waning Glow is loaded and speaks this header's version (or a later one that keeps it)
	inline bool Available()
	{
		static const auto version = detail::Find<GetAPIVersion_t>("WaningGlow_GetAPIVersion");
		return version && version() >= kVersion;
	}

	// the charge fraction (0..1) in that hand, or -1 when Waning Glow is not following it
	inline float ChargeFraction(RE::Actor* a_actor, bool a_leftHand)
	{
		static const auto fn = detail::Find<GetChargeFraction_t>("WaningGlow_GetChargeFraction");
		return fn ? fn(a_actor, a_leftHand) : -1.0f;
	}

	// the multiplier on that hand's lights this frame, or 1 when Waning Glow is not following it
	inline float LightMultiplier(RE::Actor* a_actor, bool a_leftHand)
	{
		static const auto fn = detail::Find<GetLightMultiplier_t>("WaningGlow_GetLightMultiplier");
		return fn ? fn(a_actor, a_leftHand) : 1.0f;
	}
#endif
}
