#pragma once

namespace ot {

struct WeaponDef {
    const char* name;     // display name (uppercase for the HUD font)
    int damage;           // per-hit damage (server-authoritative)
    float fireInterval;   // seconds between shots
    float range;          // hitscan range in world units
};

// Weapon list. Index 0 is the default/starting weapon.
inline const WeaponDef kWeapons[] = {
    {"RIFLE", 34, 0.12f, 20000.0f},
};

inline int weaponCount() {
    return static_cast<int>(sizeof(kWeapons) / sizeof(kWeapons[0]));
}

inline const WeaponDef& weaponDef(int index) {
    return kWeapons[(index >= 0 && index < weaponCount()) ? index : 0];
}

} // namespace ot
