#pragma once
#ifndef CATA_SRC_NPC_AI_PROFILER_H
#define CATA_SRC_NPC_AI_PROFILER_H

#include <array>
#include <chrono>
#include <cstdint>
#include <string>

namespace npc_ai
{

// Lightweight, opt-in profiling for the custom NPC AI.  Normal games pay only
// a predictable branch and clock read at instrumented boundaries.  Enable with
// CDDA_NPC_AI_PROFILE=1; summaries are written to the user directory rather
// than stdout so per-turn execution never floods the UI.
enum class profile_subsystem : int {
    total,
    context,
    memory,
    perception,
    spontaneous,
    combat_social,
    combat_snapshot,
    async_preparation,
    async_main_thread,
    npc_to_npc,
    event_publish,
    watchlist,
    survival,
    climbing_pathfinding,
    count
};

struct profile_sample {
    std::uint64_t calls = 0;
    std::uint64_t total_us = 0;
    std::uint64_t max_us = 0;
};

using profile_report = std::array<profile_sample,
      static_cast<std::size_t>( profile_subsystem::count )>;

class scoped_profile
{
    public:
        explicit scoped_profile( profile_subsystem subsystem );
        ~scoped_profile();
        scoped_profile( const scoped_profile & ) = delete;
        scoped_profile &operator=( const scoped_profile & ) = delete;

    private:
        profile_subsystem subsystem_;
        bool active_ = false;
        std::chrono::steady_clock::time_point started_;
};

bool profiling_enabled();
void set_profiling_enabled_for_test( bool enabled );
void reset_profile();
profile_report profile_snapshot();
std::string format_profile_report( const profile_report &report );
void maybe_write_profile_report( int game_turn );

} // namespace npc_ai

#endif // CATA_SRC_NPC_AI_PROFILER_H
