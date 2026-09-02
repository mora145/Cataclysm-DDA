#pragma once
#ifndef CATA_SRC_NPC_AI_CLIMBING_H
#define CATA_SRC_NPC_AI_CLIMBING_H

#include <string>

#include "coordinates.h"

class npc;

namespace npc_ai
{

enum class climb_refusal {
    none,
    invalid_transition,
    blocked_landing,
    damaged_arms,
    exhausted,
    overloaded,
    excessive_risk,
    cannot_stow_weapon
};

struct climb_assessment {
    bool possible = false;
    bool vertical_climb = false;
    int move_cost = 0;
    float slip_chance = 100.0f;
    climb_refusal refusal = climb_refusal::invalid_transition;
};

enum class climb_attempt_result {
    not_a_climb,
    succeeded,
    slipped,
    refused
};

climb_assessment assess_climb( const npc &who, const tripoint_bub_ms &from,
                               const tripoint_bub_ms &to );
climb_attempt_result attempt_climb( npc &who, const tripoint_bub_ms &from,
                                    const tripoint_bub_ms &to );
std::string climb_refusal_message( climb_refusal reason );

} // namespace npc_ai

#endif // CATA_SRC_NPC_AI_CLIMBING_H
