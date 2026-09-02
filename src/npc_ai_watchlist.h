#pragma once
#ifndef CATA_SRC_NPC_AI_WATCHLIST_H
#define CATA_SRC_NPC_AI_WATCHLIST_H

#include <string>

class npc;

namespace npc_ai
{

// Procesa una instruccion interna generada por el LLM:
// [[WATCH_ITEM:axe]]
// La elimina de lo que Larion dira al jugador.
bool apply_watch_control(
    const npc &who,
    std::string &speech
);

// Informa al LLM de las busquedas activas del NPC.
std::string build_watchlist_context( const npc &who );

// Revision ligera durante los turnos normales del NPC.
// IMPORTANTE: esta funcion NO llama a Ollama ni a Qwen.
void check_item_watchlist( npc &who );
void reset_watch_cache();

} // namespace npc_ai

#endif // CATA_SRC_NPC_AI_WATCHLIST_H
