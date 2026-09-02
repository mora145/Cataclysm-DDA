#pragma once
#ifndef CATA_SRC_NPC_AI_MEMORY_H
#define CATA_SRC_NPC_AI_MEMORY_H

#include <cstddef>
#include <string>

class npc;

namespace npc_ai
{

std::string build_memory_context(
    const npc &who,
    std::size_t max_exchanges = 10
);

void remember_exchange(
    const npc &who,
    const std::string &player_line,
    const std::string &npc_line
);

// Short-lived, in-memory speech history used to prevent chatter repetition.
// This complements (rather than replaces) persistent conversational memory.
void remember_recent_speech( const npc &who, const std::string &npc_line,
                             const std::string &intent = "" );
bool recent_speech_is_duplicate( const npc &who, const std::string &candidate );
bool recent_speech_mentions( const npc &who, const std::string &name );
std::string build_recent_speech_context( const npc &who, std::size_t max_entries = 4 );
void reset_recent_speech_for_test( const npc &who );
void reset_all_recent_speech();
// Opt-in benchmarks use transient speech history but must not write the
// world's .memory files or SQLite database.
void set_persistent_memory_writes_for_test( bool enabled );

// Stores a significant observed combat episode in the existing SQLite
// conversation store.  It is deliberately sparse: ordinary attacks are not
// persisted.
void remember_combat_event( const npc &who, const std::string &event_kind,
                            const std::string &detail, int importance );

} // namespace npc_ai

#endif // CATA_SRC_NPC_AI_MEMORY_H
