#pragma once
#ifndef CATA_SRC_NPC_AI_VEHICLE_UNLOAD_H
#define CATA_SRC_NPC_AI_VEHICLE_UNLOAD_H

#include <string>

class npc;

namespace npc_ai
{

struct vehicle_unload_command_result {
    bool handled = false;
    bool started = false;
    bool success = false;
    std::string message;
};

vehicle_unload_command_result try_handle_vehicle_unload_command( npc &who,
        const std::string &player_line );

// Continues physical cargo handling without requiring inventory pocket capacity.
bool process_vehicle_unload_task( npc &who );

bool has_vehicle_unload_task( const npc &who );
void cancel_vehicle_unload_task( const npc &who );
void reset_all_vehicle_unload_tasks();

} // namespace npc_ai

#endif // CATA_SRC_NPC_AI_VEHICLE_UNLOAD_H
