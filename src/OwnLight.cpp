// Waning Glow - SKSE plugin
// Copyright (C) 2026 izzydoingit
// GPL-3.0-or-later; see LICENSE.txt and the notice at the top of main.cpp.
//
// Waning Glow's own light, for an enchanted weapon no other mod lights (a fire, frost or shock enchantment with only a
// glow shader and no art has nothing for a lighting mod to hang a light on). Off unless the Settings page's "Light
// weapons nothing else lights" is on. The light hangs on the weapon's model, so Lights.cpp finds and dims it like any
// other: it follows the charge, sputters, pulses and flares.
//
// How a light is made and registered follows ReLight by Truman (github.com/TrumanGIT/ReLight, GPL-3.0-or-later): one
// master NiPointLight made once and cloned for every use (a freshly made light attached straight away crashes), the
// create parameters a non-shadow light needs, and handing the light to the shadow scene node, which renders it.
// Everything here runs on the main thread, from the player's update.

#include "Plugin.h"

namespace Plugin
{
	namespace
	{
		// Community Shaders' inverse-square lighting: its flag, and the constant its cutoff is worked out with
		constexpr std::uint32_t kInverseSquare = 1u << 10;
		constexpr float         kK = 3918.88f;
		constexpr float         kSize = 1.414f;
		constexpr float         kFade = 1.0f;

		struct Own
		{
			RE::NiPointer<RE::NiPointLight> light;
			RE::NiPointer<RE::BSLight>      registered;
			RE::NiPointer<RE::NiAVObject>   model;
			bool                            asked{ false };  // this frame's hand still wants it
		};

		std::unordered_map<std::uint64_t, Own> gOwn;    // main thread only (Lights.cpp holds its lock around every call)
		std::atomic<std::size_t>               gCount{ 0 };  // gOwn's size, for the menu's thread
		RE::NiPointer<RE::NiPointLight>        gMaster;

		// the color our own light takes from the enchantment: its costliest effect's element, else its glow shader's color
		Glow::Rgb ColorOf(const RE::EnchantmentItem* a_ench)
		{
			const auto* top = a_ench ? a_ench->GetCostliestEffectItem() : nullptr;
			const auto* base = top ? top->baseEffect : nullptr;
			if (!base) {
				return Glow::OwnLightColor(0, nullptr);
			}
			int element = 0;
			switch (base->data.resistVariable) {
			case RE::ActorValue::kResistFire:
				element = 1;
				break;
			case RE::ActorValue::kResistFrost:
				element = 2;
				break;
			case RE::ActorValue::kResistShock:
				element = 3;
				break;
			default:
				break;
			}
			const auto* shader = base->data.enchantShader;
			const auto  c = shader ? shader->data.fillTextureEffectColorKey1 : RE::Color{};
			const Glow::Rgb fill{ static_cast<float>(c.red), static_cast<float>(c.green), static_cast<float>(c.blue) };
			return Glow::OwnLightColor(element, shader ? &fill : nullptr);
		}

		RE::ShadowSceneNode* Scene() { return RE::BSShaderManager::State::GetSingleton().shadowSceneNode[0]; }

		bool Isl()
		{
			static const bool isl = [] {
				std::error_code ec;
				return std::filesystem::exists("Data/Shaders/InverseSquareLighting/InverseSquareLighting.hlsli", ec);
			}();
			return isl;
		}

		RE::NiPointLight* CloneMaster()
		{
			if (!gMaster) {
				const RE::NiPointer<RE::NiPointLight> fresh(RE::NiPointLight::Create());
				auto* clone = fresh ? netimmerse_cast<RE::NiPointLight*>(fresh->Clone()) : nullptr;
				if (!clone) {
					return nullptr;
				}
				gMaster.reset(clone);
			}
			return netimmerse_cast<RE::NiPointLight*>(gMaster->Clone());
		}

		void Drop(Own& a_own)
		{
			if (a_own.registered) {
				if (auto* scene = Scene()) {
					scene->RemoveLight(a_own.registered);
				}
			}
			if (a_own.light && a_own.light->parent) {
				a_own.light->parent->DetachChild(a_own.light.get());
			}
			a_own = {};
		}

		bool Hang(Own& a_own, RE::NiAVObject* a_model, const Glow::Rgb& a_color, float a_reach)
		{
			auto* scene = Scene();
			auto* node = a_model ? a_model->AsNode() : nullptr;
			if (!scene || !node) {
				return false;
			}
			RE::NiPointer<RE::NiPointLight> light(CloneMaster());
			if (!light) {
				return false;
			}
			light->name = kOwnLightName;
			auto& data = light->GetLightRuntimeData();
			data.diffuse = { a_color.r, a_color.g, a_color.b };
			data.fade = kFade;
			data.radius = { a_reach, a_reach, kSize };  // z is the light's size, not a third radius
			light->SetLightAttenuation(a_reach);
			if (Isl()) {
				// Inverse Square Lighting reads its flag from the ambient color's first word and its cutoff from the second
				data.ambient.red = std::bit_cast<float>(std::bit_cast<std::uint32_t>(data.ambient.red) | kInverseSquare);
				data.ambient.green = std::clamp(kK * kFade / (a_reach * a_reach + kSize * kSize), 0.01f, 0.99f);
			}
			node->AttachChild(light.get(), true);
			RE::NiUpdateData update{};
			light->Update(update);
			RE::ShadowSceneNode::LIGHT_CREATE_PARAMS params{};
			params.dynamic = true;
			params.shadowLight = false;
			params.portalStrict = true;
			params.affectLand = true;
			params.affectWater = true;
			params.neverFades = true;
			params.fov = 90.0f;
			params.falloff = 1.0f;
			params.nearDistance = 5.0f;
			params.depthBias = 1.0f;
			auto* registered = scene->AddLight(light.get(), params);
			if (!registered) {
				node->DetachChild(light.get());
				return false;
			}
			a_own.light = std::move(light);
			a_own.registered.reset(registered);
			a_own.model.reset(a_model);
			return true;
		}
	}

	RE::NiPointLight* KeepOwnLight(std::uint64_t a_key, RE::NiAVObject* a_model, const RE::EnchantmentItem* a_ench, float a_reach)
	{
		if (!a_model) {
			if (const auto it = gOwn.find(a_key); it != gOwn.end()) {
				Drop(it->second);
				gOwn.erase(it);
			}
			return nullptr;
		}
		auto& own = gOwn[a_key];
		own.asked = true;
		// still on this model, and still attached (a model rebuilt by a draw or a view change is a new one)
		if (own.light && own.model.get() == a_model && own.light->parent == a_model) {
			return nullptr;
		}
		Drop(own);
		own.asked = true;
		return Hang(own, a_model, ColorOf(a_ench), std::clamp(a_reach, 50.0f, 600.0f)) ? own.light.get() : nullptr;
	}

	void SweepOwnLights()
	{
		for (auto it = gOwn.begin(); it != gOwn.end();) {
			if (!it->second.asked || !it->second.light) {
				Drop(it->second);
				it = gOwn.erase(it);
			} else {
				it->second.asked = false;
				++it;
			}
		}
		gCount = gOwn.size();
	}

	void DropOwnLights()
	{
		for (auto& [key, own] : gOwn) {
			Drop(own);
		}
		gOwn.clear();
		gCount = 0;
	}

	std::size_t OwnLightCount() { return gCount.load(); }
}
