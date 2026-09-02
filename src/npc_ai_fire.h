#pragma once
#ifndef CATA_SRC_NPC_AI_FIRE_H
#define CATA_SRC_NPC_AI_FIRE_H

#include <string>

class npc;

namespace npc_ai
{

struct start_fire_command_result {
    bool handled = false;
    bool started = false;
    bool success = false;
    std::string message;
};

start_fire_command_result try_handle_start_fire_command( npc &who,
        const std::string &player_line );

// Continues deterministic movement, fuel placement, and ignition without an LLM call.
// Returns true when this function spent or claimed the NPC's current action.
bool process_start_fire_task( npc &who );

bool has_start_fire_task( const npc &who );
void cancel_start_fire_task( const npc &who );
void reset_all_start_fire_tasks();

} // namespace npc_ai

#endif // CATA_SRC_NPC_AI_FIRE_H
