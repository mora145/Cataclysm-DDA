#pragma once
#ifndef CATA_SRC_NPC_AI_INTERIOR_H
#define CATA_SRC_NPC_AI_INTERIOR_H

#include <string>
#include <utility>
#include <vector>

#include "coordinates.h"

class npc;

namespace npc_ai
{

enum class structured_voice_order : int {
    none,
    enter_nearest_reachable_safe_interior
};

struct interior_order_result {
    bool handled = false;
    bool success = false;
    std::vector<std::pair<int, tripoint_abs_ms>> assignments;
    std::string message;
};

structured_voice_order parse_structured_voice_order( const std::string &spoken );
interior_order_result execute_enter_nearest_reachable_safe_interior(
    const std::vector<npc *> &targets );

} // namespace npc_ai

#endif // CATA_SRC_NPC_AI_INTERIOR_H
