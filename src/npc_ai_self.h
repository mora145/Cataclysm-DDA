#pragma once
#ifndef CATA_SRC_NPC_AI_SELF_H
#define CATA_SRC_NPC_AI_SELF_H

#include <cstdint>
#include <string>
#include <vector>

class npc;

namespace npc_ai
{

struct self_need_observation {
    int value = 0;
    bool active = false;
};

struct self_bodypart_observation {
    std::string id;
    std::string name;
    int hp_current = 0;
    int hp_max = 0;
    int temperature_c = 0;
    int encumbrance = 0;
    int bleeding_intensity = 0;
    bool cold = false;
    bool hot = false;
    bool bitten = false;
    bool infected = false;
    bool broken = false;
};

struct self_effect_observation {
    std::string id;
    std::string name;
    std::string bodypart_id;
    int intensity = 0;
};

struct self_item_observation {
    std::string id;
    std::string name;
    int count = 0;
    bool accessible = false;
    bool sealed = false;
    bool wielded = false;
    bool worn = false;
    bool contained = false;
    bool container = false;
    bool food = false;
    bool drink = false;
    bool medicine = false;
    bool weapon = false;
    bool ammunition = false;
    bool tool = false;
    bool firestarter = false;
};

struct self_resource_summary {
    bool available = false;
    int item_count = 0;
    std::vector<std::string> item_ids;
};

struct npc_self_snapshot {
    self_need_observation hunger;
    self_need_observation thirst;
    self_need_observation sleepiness;
    int sleep_deprivation = 0;
    int perceived_pain = 0;
    int health_percent = 0;
    int morale = 0;

    int stamina = 0;
    int stamina_max = 0;
    std::int64_t carried_weight_gram = 0;
    std::int64_t weight_capacity_gram = 0;
    std::int64_t carried_volume_ml = 0;
    std::int64_t volume_capacity_ml = 0;

    bool cold = false;
    bool hot = false;
    bool bleeding = false;
    bool infected = false;
    bool broken_limb = false;
    bool inventory_scanned = false;

    std::vector<self_bodypart_observation> bodyparts;
    std::vector<self_effect_observation> effects;
    std::vector<self_item_observation> items;

    self_resource_summary usable_food;
    self_resource_summary potable_drink;
    self_resource_summary medicine;
    self_resource_summary weapons;
    self_resource_summary ammunition;
    self_resource_summary tools;
    self_resource_summary firestarters;
};

enum class self_snapshot_scope : int {
    physical_state,
    full_inventory
};

npc_self_snapshot build_self_snapshot( const npc &who,
                                       self_snapshot_scope scope = self_snapshot_scope::full_inventory );
std::string render_self_snapshot( const npc_self_snapshot &snapshot );

} // namespace npc_ai

#endif // CATA_SRC_NPC_AI_SELF_H
