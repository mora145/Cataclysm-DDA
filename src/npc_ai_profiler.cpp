#include "npc_ai_profiler.h"

#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <fstream>
#include <sstream>

#include "npc_ai_debug.h"

namespace npc_ai
{
namespace
{

struct atomic_profile_sample {
    std::atomic<std::uint64_t> calls{ 0 };
    std::atomic<std::uint64_t> total_us{ 0 };
    std::atomic<std::uint64_t> max_us{ 0 };
};

constexpr std::size_t subsystem_count = static_cast<std::size_t>( profile_subsystem::count );
std::array<atomic_profile_sample, subsystem_count> samples;
std::atomic<int> enabled_override{ -1 };
std::atomic<int> last_report_turn{ -1000000000 };
constexpr int report_interval_turns = 600;

const char *subsystem_name( const profile_subsystem subsystem )
{
    static constexpr std::array<const char *, subsystem_count> names = {{
            "npc_ai_total", "npc_ai_context", "npc_ai_memory", "npc_ai_perception",
            "npc_ai_spontaneous", "npc_ai_combat_social", "npc_ai_combat_snapshot",
            "npc_ai_async_preparation", "npc_ai_async_main_thread", "npc_to_npc_evaluation",
            "npc_ai_event_publish", "npc_ai_watchlist", "npc_ai_survival",
            "npc_ai_climbing_pathfinding"
        }};
    return names[static_cast<std::size_t>( subsystem )];
}

void record_sample( const profile_subsystem subsystem, const std::uint64_t elapsed_us )
{
    atomic_profile_sample &sample = samples[static_cast<std::size_t>( subsystem )];
    sample.calls.fetch_add( 1, std::memory_order_relaxed );
    sample.total_us.fetch_add( elapsed_us, std::memory_order_relaxed );
    std::uint64_t previous = sample.max_us.load( std::memory_order_relaxed );
    while( previous < elapsed_us &&
           !sample.max_us.compare_exchange_weak( previous, elapsed_us,
                   std::memory_order_relaxed ) ) {
    }
}

} // namespace

scoped_profile::scoped_profile( const profile_subsystem subsystem ) : subsystem_( subsystem )
{
    active_ = profiling_enabled();
    if( active_ ) {
        started_ = std::chrono::steady_clock::now();
    }
}

scoped_profile::~scoped_profile()
{
    if( !active_ ) {
        return;
    }
    const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
                             std::chrono::steady_clock::now() - started_ ).count();
    record_sample( subsystem_, static_cast<std::uint64_t>( std::max<std::int64_t>( 0, elapsed ) ) );
}

bool profiling_enabled()
{
    const int override_value = enabled_override.load( std::memory_order_relaxed );
    if( override_value >= 0 ) {
        return override_value != 0;
    }
    static const bool environment_enabled = []() {
        const char *value = std::getenv( "CDDA_NPC_AI_PROFILE" );
        return value != nullptr && std::string( value ) == "1";
    }();
    return environment_enabled;
}

void set_profiling_enabled_for_test( const bool enabled )
{
    enabled_override.store( enabled ? 1 : 0, std::memory_order_relaxed );
}

void reset_profile()
{
    for( atomic_profile_sample &sample : samples ) {
        sample.calls.store( 0, std::memory_order_relaxed );
        sample.total_us.store( 0, std::memory_order_relaxed );
        sample.max_us.store( 0, std::memory_order_relaxed );
    }
    last_report_turn.store( -1000000000, std::memory_order_relaxed );
}

profile_report profile_snapshot()
{
    profile_report result;
    for( std::size_t index = 0; index < samples.size(); ++index ) {
        result[index].calls = samples[index].calls.load( std::memory_order_relaxed );
        result[index].total_us = samples[index].total_us.load( std::memory_order_relaxed );
        result[index].max_us = samples[index].max_us.load( std::memory_order_relaxed );
    }
    return result;
}

std::string format_profile_report( const profile_report &report )
{
    std::ostringstream output;
    for( std::size_t index = 0; index < report.size(); ++index ) {
        const profile_sample &sample = report[index];
        const double average = sample.calls == 0 ? 0.0 :
                               static_cast<double>( sample.total_us ) / sample.calls;
        output << subsystem_name( static_cast<profile_subsystem>( index ) )
               << " calls=" << sample.calls << " total_us=" << sample.total_us
               << " avg_us=" << average << " max_us=" << sample.max_us << '\n';
    }
    return output.str();
}

void maybe_write_profile_report( const int game_turn )
{
    if( !profiling_enabled() ) {
        return;
    }
    int previous = last_report_turn.load( std::memory_order_relaxed );
    if( game_turn - previous < report_interval_turns ||
        !last_report_turn.compare_exchange_strong( previous, game_turn,
                std::memory_order_relaxed ) ) {
        return;
    }
    std::ofstream output( debug_file_path( "npc_ai_profile.log" ),
                          std::ios::binary | std::ios::app );
    if( output ) {
        output << "turn=" << game_turn << '\n' << format_profile_report( profile_snapshot() ) << '\n';
    }
}

} // namespace npc_ai
