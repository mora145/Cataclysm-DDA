#include "npc_ai_survival.h"

#include <algorithm>
#include <unordered_map>
#include <vector>

#include "calendar.h"
#include "field_type.h"
#include "map.h"
#include "npc.h"
#include "npc_ai_fire.h"
#include "npc_ai_perception.h"
#include "npc_ai_profiler.h"
#include "npc_ai_self.h"

namespace
{

std::unordered_map<int, time_point> next_warmth_attempt;
std::unordered_map<int, time_point> next_warmth_check;
std::uint64_t warmth_environment_evaluations = 0;
bool severely_cold( const npc_ai::npc_self_snapshot &snapshot )
{
    for( const npc_ai::self_bodypart_observation &part : snapshot.bodyparts ) {
        if( part.temperature_c <= 31 ) {
            return true;
        }
    }
    return false;
}

bool severely_cold_now( const npc &who )
{
    const std::vector<bodypart_id> parts = who.get_all_body_parts(
            get_body_part_flags::only_main );
    return std::any_of( parts.begin(), parts.end(),
    [&]( const bodypart_id &part ) {
        return units::to_celsius( who.get_part_temp_cur( part ) ) <= 31;
    } );
}

} // namespace

namespace npc_ai
{

basic_survival_result consider_basic_survival( npc &who )
{
    scoped_profile profile( profile_subsystem::survival );
    if( !who.is_player_ally() ) {
        return basic_survival_result::no_action;
    }
    const int key = who.getID().get_value();
    // Avoid the inventory/resource snapshot for the overwhelmingly common
    // warm NPC.  This reads the same bodypart temperatures used below and does
    // not change temperature or stamina mechanics.
    if( !severely_cold_now( who ) ) {
        // A later return to severe cold must detect immediately, not wait out
        // a leftover inspect interval from the previous cold episode.
        next_warmth_check.erase( key );
        return basic_survival_result::no_action;
    }
    if( !who.is_safe() || who.has_player_activity() || has_start_fire_task( who ) ) {
        return basic_survival_result::no_action;
    }
    const auto check = next_warmth_check.find( key );
    if( check != next_warmth_check.end() && calendar::turn < check->second ) {
        return basic_survival_result::no_action;
    }

    // Record the inspect interval before any inventory or scene work so every
    // later exit (visible fire, no stove, no firestarter) is rate-limited.
    next_warmth_check[key] = calendar::turn + 1_minutes;
    ++warmth_environment_evaluations;

    const npc_self_snapshot self = build_self_snapshot( who );
    if( !severely_cold( self ) ) {
        return basic_survival_result::no_action;
    }

    // Preserve the existing 30-minute action-attempt cooldown independently
    // from the one-minute environment-inspection throttle.
    const auto attempt = next_warmth_attempt.find( key );
    if( attempt != next_warmth_attempt.end() && calendar::turn < attempt->second ) {
        return basic_survival_result::no_action;
    }

    bool visible_stove = false;
    const npc_sensory_snapshot senses = build_sensory_snapshot( who, 20 );
    for( const sensory_tile_observation &tile : senses.tiles ) {
        if( tile.knowledge != sensory_knowledge::currently_perceived ) {
            continue;
        }
        if( tile.fire.knowledge == sensory_knowledge::currently_perceived && tile.fire.value ) {
            return basic_survival_result::warmth_available;
        }
        visible_stove = visible_stove || tile.fire_container;
    }
    if( !visible_stove || !self.firestarters.available ) {
        return basic_survival_result::no_action;
    }

    next_warmth_attempt[key] = calendar::turn + 30_minutes;

    const start_fire_command_result result = try_handle_start_fire_command(
                who, "Enciende la cocina de leña." );
    if( result.started ) {
        who.say( result.message );
        return basic_survival_result::stove_task_started;
    }
    return basic_survival_result::stove_attempt_blocked;
}

void clear_survival_state_for_test( const npc &who )
{
    const int key = who.getID().get_value();
    next_warmth_attempt.erase( key );
    next_warmth_check.erase( key );
}

void reset_all_survival_state()
{
    next_warmth_attempt.clear();
    next_warmth_check.clear();
    warmth_environment_evaluations = 0;
}

std::uint64_t warmth_environment_evaluations_for_test()
{
    return warmth_environment_evaluations;
}

} // namespace npc_ai
