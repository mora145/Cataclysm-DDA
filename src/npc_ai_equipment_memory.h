#pragma once
#ifndef CATA_SRC_NPC_AI_EQUIPMENT_MEMORY_H
#define CATA_SRC_NPC_AI_EQUIPMENT_MEMORY_H

#include <string>
#include <vector>

#include "calendar.h"
#include "coordinates.h"
#include "npc_ai_goal.h"

class item;
class Character;
class npc;

namespace npc_ai
{

enum class equipment_memory_status {
    dropped,
    recovering,
    recovered,
    missing
};

struct dropped_equipment_memory {
    std::string item_uid;
    std::string item_type;
    std::string item_name;
    int owner_id = 0;
    tripoint_abs_ms location;
    std::string reason;
    time_point dropped_at = calendar::turn_zero;
    bool retrieval_expected = false;
    bool wear_after_recovery = false;
    equipment_memory_status status = equipment_memory_status::dropped;
    ai_goal_id recovery_goal_id = 0;
};

std::string remember_dropped_equipment( npc &who, item &it,
                                        const tripoint_abs_ms &location,
                                        const std::string &reason,
                                        bool retrieval_expected );

// Adapter for explicit physical drop sites in vanilla code.  This deliberately
// does not hook Character::remove_weapon(), because many removals are voluntary
// transfers or destruction rather than recoverable ground drops.
std::string remember_involuntary_weapon_drop( Character &who, item &it,
        const tripoint_abs_ms &location, const std::string &reason );

std::vector<dropped_equipment_memory> get_dropped_equipment_memories( const npc &who );

bool request_equipment_recovery( npc &who, const std::string &item_uid,
                                 bool wear_after_recovery, std::string &error );

void clear_dropped_equipment_memories_for_test( const npc &who );
void clear_runtime_equipment_memory_cache_for_test( const npc &who );
void reset_all_equipment_memory_cache();

// Returns true only when it performed a physical recovery movement or pickup action.
bool process_equipment_recovery( npc &who );

} // namespace npc_ai

#endif // CATA_SRC_NPC_AI_EQUIPMENT_MEMORY_H
