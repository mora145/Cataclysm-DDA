#pragma once
#ifndef CATA_SRC_NPC_AI_GOAL_H
#define CATA_SRC_NPC_AI_GOAL_H

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

class npc;

namespace npc_ai
{

using ai_goal_id = std::uint64_t;

enum class ai_goal_kind {
    light_stove,
    batch_pickup,
    drop_equipment,
    recover_equipment,
    assist_ally,
    rescue_casualty,
    unload_vehicle,
    sort_storage,
    generic
};

enum class ai_goal_priority : int {
    low = 10,
    normal = 20,
    high = 30,
    emergency = 40
};

enum class ai_goal_status {
    pending,
    active,
    suspended,
    completed,
    failed
};

struct ai_goal {
    ai_goal_id id = 0;
    ai_goal_id parent_id = 0;
    ai_goal_kind kind = ai_goal_kind::generic;
    ai_goal_priority priority = ai_goal_priority::normal;
    ai_goal_status status = ai_goal_status::pending;
    std::string summary;
    std::string failure_reason;
};

ai_goal_id begin_goal( const npc &who, ai_goal_kind kind, ai_goal_priority priority,
                       const std::string &summary, ai_goal_id parent_id = 0 );
bool complete_goal( const npc &who, ai_goal_id id );
bool fail_goal( const npc &who, ai_goal_id id, const std::string &reason );
std::optional<ai_goal> active_goal( const npc &who );
std::vector<ai_goal> goal_history( const npc &who );
void clear_goals_for_test( const npc &who );
void reset_all_goals();

} // namespace npc_ai

#endif // CATA_SRC_NPC_AI_GOAL_H
