#pragma once
#ifndef CATA_SRC_NPC_AI_SURVIVAL_H
#define CATA_SRC_NPC_AI_SURVIVAL_H

#include <cstdint>

class npc;

namespace npc_ai
{

enum class basic_survival_result {
    no_action,
    warmth_available,
    stove_task_started,
    stove_attempt_blocked
};

// Food, drink, medicine and sleep remain owned by npc::address_needs().
// This adds only the missing severe-cold stove initiative.
basic_survival_result consider_basic_survival( npc &who );
void clear_survival_state_for_test( const npc &who );
void reset_all_survival_state();
// Test seam for verifying that the one-minute gate precedes both snapshots.
std::uint64_t warmth_environment_evaluations_for_test();

} // namespace npc_ai

#endif // CATA_SRC_NPC_AI_SURVIVAL_H
