#pragma once
#ifndef CATA_SRC_NPC_AI_EQUIPMENT_H
#define CATA_SRC_NPC_AI_EQUIPMENT_H

#include <cstddef>
#include <string>
#include <vector>

#include "item_location.h"

class npc;

namespace npc_ai
{

enum class equipment_action {
    none,
    drop,
    recover,
    wear,
    take_off,
    store
};

struct equipment_command_result {
    bool handled = false;
    bool success = false;
    bool action_started = false;
    equipment_action action = equipment_action::none;
    std::string equipment_uid;
    std::string message;
};

struct group_equipment_command_result {
    bool handled = false;
    std::vector<npc *> affected;
    std::size_t pending = 0;
    std::size_t failed = 0;
    std::string reply;
    npc *failure_speaker = nullptr;
    std::string failure_reply;
};

equipment_action detect_equipment_action( const std::string &player_line );

equipment_command_result execute_equipment_action( npc &who, equipment_action action,
        item_location target, const std::string &reason = "player_order",
        bool retrieval_expected = false );

equipment_command_result try_handle_equipment_command( npc &who,
        const std::string &player_line );

// Executes deterministic equipment orders independently for every target.
// No LLM requests are created here; each NPC resolves only their own items and
// equipment memory.
group_equipment_command_result execute_group_equipment_command(
    const std::vector<npc *> &targets, const std::string &player_line );

} // namespace npc_ai

#endif // CATA_SRC_NPC_AI_EQUIPMENT_H
