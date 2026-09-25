# Waning Glow rule files

A rule file changes how Waning Glow treats particular weapons: leave a weapon's light alone, give it its own fade, or
make a bound weapon follow its spell's time. You need one only when the Settings page, which applies to every weapon,
isn't enough.

## Where they go

`Data\SKSE\Plugins\WaningGlow\*.json`. Files are read in name order (case ignored), and the rules in each file in the
order written. **Every rule that matches a weapon applies, and a later rule's setting replaces an earlier one's.** So a
player's `zz_mine.json` can override a mod's file. Comments (`//` and `/* */`) are allowed.

The Debug page lists how many rules loaded and every problem found. **Reload rule files** reads them again without
restarting the game.

```json
{
  "rules": [
    { "name": "Dawnbreaker always glows", "weapons": ["Skyrim.esm|0x02ACD2"], "mode": "exempt" },
    { "name": "Fire enchantments sputter longer", "effectKeywords": ["MagicDamageFire"], "sputterBelow": 30 },
    { "name": "Staves fade gently", "staff": true, "curve": "gentle", "emptyBrightness": 20 }
  ]
}
```

## Which weapons a rule matches

A rule with no match fields matches every weapon. Within one field any entry matches; every field a rule gives must
match.

| Field | What it matches |
|---|---|
| `weapons` | the weapon: `"Plugin.esp\|0x123"`, SPID's `"0x123~Plugin.esp"`, or an editor ID |
| `enchantments` | the enchantment, in the same forms |
| `weaponKeywords` | a keyword on the weapon (its editor ID, any case) |
| `effectKeywords` | a keyword on any magic effect of the enchantment, such as `MagicDamageFire` |
| `bound` | `true`: bound weapons only; `false`: never bound weapons |
| `staff` | `true`: staves only; `false`: never staves |

A form ID may carry a load-order prefix copied from xEdit (`0x0102ACD2`); it is ignored. A rule whose `weapons` or
`enchantments` name only forms that aren't installed matches nothing (not everything); the log says so.

## What a rule sets

Anything a rule doesn't set keeps the value from the Settings page, or from an earlier matching rule.

| Key | Values | Meaning |
|---|---|---|
| `mode` | `"charge"`, `"exempt"`, `"bound"` | follow the charge; leave the light alone; follow a bound-weapon spell's time |
| `emptyBrightness` | 0 to 50 (percent) | what is left of the light at 0% charge |
| `curve` | `"linear"`, `"gentle"`, `"steep"` | how the light falls as the charge falls |
| `reachFollows` | 0 to 100 (percent) | how much the light's reach shrinks with its brightness |
| `sputter` | true / false | the stepped dips near empty |
| `sputterBelow` | 1 to 50 (percent) | the charge where the sputter starts |
| `sputterStrength` | 0 to 100 (percent) | the deepest dip, at empty |
| `emptySteady` | true / false | at exactly 0% the light holds still |
| `hitPulse`, `hitPulseStrength` | true / false; 0 to 200 | the flash when a hit spends charge |
| `rechargeFlare`, `rechargeFlareStrength` | true / false; 0 to 200 | the swell when a soul gem refills the weapon |
| `colorCooling`, `colorCoolingAmount` | true / false; 0 to 100 | the colour draining near empty |
| `colorCoolingTint` | `"grey"` (or `"gray"`), `"ember"` | what the colour drains toward |
| `boundFadeSeconds` | 1 to 60 | over how many of a bound spell's last seconds its light fades |
| `name`, `comment` | text | for the log and the Debug page |

Numbers outside a range are clamped to it. A value of the wrong type (`"sputter": "yes"`), a misspelt key or an unknown
mode is reported as a problem, and that one setting is ignored.

## Before any rule

- A weapon with the keyword `WaningGlow_NoFade` is always left alone, whatever the files say. KID or SkyPatcher can hand
  the keyword out.
- A bound weapon follows its spell's time if **Bound weapons** is on, and is left alone if it's off.
- A staff is left alone if **Staves** is off.
