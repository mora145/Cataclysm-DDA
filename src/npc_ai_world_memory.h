#pragma once
#ifndef CATA_SRC_NPC_AI_WORLD_MEMORY_H
#define CATA_SRC_NPC_AI_WORLD_MEMORY_H

#include <cstddef>
#include <string>

class npc;

namespace npc_ai
{

// Guarda un hecho observado directamente por el NPC.
void remember_seen_item(
    const npc &who,
    const std::string &item_id,
    const std::string &item_name,
    int abs_x,
    int abs_y,
    int abs_z
);

// Construye contexto con recuerdos reales del mundo.
std::string build_world_memory_context(
    const npc &who,
    std::size_t max_memories = 12
);

} // namespace npc_ai

#endif // CATA_SRC_NPC_AI_WORLD_MEMORY_H
