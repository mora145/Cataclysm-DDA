#pragma once

#include <string>

class npc;

namespace npc_ai
{

struct batch_pickup_command_result {
    bool handled = false;
    bool success = false;
    std::string message;
};


// Interpreta ordenes como:
// "recoge toda la comida"
batch_pickup_command_result try_handle_batch_pickup_command(
    npc &who,
    const std::string &player_line
);


// Continua una orden por lote ya iniciada.
// Se llama desde el turno normal del NPC.
void process_batch_pickup(
    npc &who
);


void reset_all_food_batches();


} // namespace npc_ai