#pragma once
#ifndef CATA_SRC_NPC_AI_WIELD_H
#define CATA_SRC_NPC_AI_WIELD_H

#include <string>

class item_location;
class npc;

namespace npc_ai
{

struct ai_request_completion;

struct wield_command_result {
    bool handled = false;
    bool success = false;
    bool pending = false;
    std::string message;
};

// Result of validating or executing a wield action against a concrete item
// already chosen by deterministic C++/LLM candidate resolution.
struct wield_target_result {
    bool success = false;
    bool drops_previous = false;
    bool drop_previous_before_wield = false;
    std::string previous_name;
    std::string message;
};

wield_target_result validate_wield_target( npc &who, const item_location &target,
        bool allow_drop_previous = true );

wield_target_result wield_target( npc &who, item_location target,
                                  bool allow_drop_previous = true );

wield_command_result try_handle_wield_command(
    npc &who,
    const std::string &player_line
);

void apply_wield_ai_completion( npc &who, const ai_request_completion &completion );

} // namespace npc_ai

#endif // CATA_SRC_NPC_AI_WIELD_H
