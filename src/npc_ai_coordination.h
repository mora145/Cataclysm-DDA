#pragma once
#ifndef CATA_SRC_NPC_AI_COORDINATION_H
#define CATA_SRC_NPC_AI_COORDINATION_H

#include <optional>
#include <string>
#include <vector>

#include "npc_ai_goal.h"

class npc;

namespace npc_ai
{

enum class cooperative_task_kind {
    logistics,
    heavy_transport,
    sorting,
    medical,
    guard,
    mechanics,
    construction
};

struct helper_evaluation {
    npc *candidate = nullptr;
    bool eligible = false;
    int suitability = 0;
    std::string rejection_reason;
};

struct delegated_assignment {
    int requester_id = -1;
    int helper_id = -1;
    cooperative_task_kind task = cooperative_task_kind::logistics;
    ai_goal_id requester_goal = 0;
    ai_goal_id helper_goal = 0;
    std::string summary;
};

helper_evaluation evaluate_helper( const npc &requester, npc &candidate,
                                   cooperative_task_kind task, int max_range = 12 );
std::vector<helper_evaluation> rank_nearby_helpers( const npc &requester,
        cooperative_task_kind task, int max_range = 12 );
npc *select_best_helper( const npc &requester, cooperative_task_kind task,
                         int max_range = 12 );
std::optional<delegated_assignment> delegate_to_helper( const npc &requester, npc &helper,
        cooperative_task_kind task, ai_goal_id requester_goal, const std::string &summary );
std::optional<delegated_assignment> assignment_for( const npc &helper );
bool complete_assignment( const npc &helper );
bool fail_assignment( const npc &helper, const std::string &reason );
void clear_assignments_for_test();
void reset_all_assignments();

} // namespace npc_ai

#endif // CATA_SRC_NPC_AI_COORDINATION_H
