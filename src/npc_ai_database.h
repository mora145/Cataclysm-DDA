#pragma once
#ifndef CATA_SRC_NPC_AI_DATABASE_H
#define CATA_SRC_NPC_AI_DATABASE_H

#include <string>
#include <vector>

class npc;

namespace npc_ai
{

struct database_status {
    bool success = false;
    int schema_version = 0;
    std::string path;
    std::string error;
};

struct conversation_record {
    long long id = 0;
    int npc_id = 0;
    std::string player_line;
    std::string npc_line;
    std::string created_at;
};

database_status ensure_database_ready();

// Guarda una conversacion en SQLite.
// Durante esta etapa se usa EN PARALELO con npc_X.memory.
bool store_conversation(
    const npc &who,
    const std::string &player_line,
    const std::string &npc_line,
    std::string &error
);

// Lee las conversaciones mas recientes de este NPC.
// Devuelve las filas en orden cronologico.
std::vector<conversation_record> load_recent_conversations(
    const npc &who,
    int max_records,
    std::string &error
);


bool store_seen_item_memory(
    const npc &who,
    const std::string &item_id,
    const std::string &item_name,
    int abs_x,
    int abs_y,
    int abs_z,
    bool &inserted,
    std::string &error
);

struct world_memory_record {
    long long id = 0;
    int npc_id = 0;
    std::string memory_type;
    std::string item_name;
    int abs_x = 0;
    int abs_y = 0;
    int abs_z = 0;
    std::string created_at;
};

std::vector<world_memory_record> load_recent_world_memories(
    const npc &who,
    int max_records,
    std::string &error
);
} // namespace npc_ai

#endif // CATA_SRC_NPC_AI_DATABASE_H