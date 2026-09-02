#include "npc_ai_event_stream.h"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <system_error>

#include "calendar.h"
#include "character.h"
#include "game.h"
#include "map.h"
#include "map_scale_constants.h"
#include "monster.h"
#include "mtype.h"
#include "npc.h"
#include "npc_ai_profiler.h"
#include "path_info.h"
#include "worldfactory.h"

namespace npc_ai
{
namespace
{

std::deque<world_event> events;
std::uint64_t next_sequence = 1;
std::uint64_t stream_session = 1;
bool debug_override_set = false;
bool debug_override = false;
constexpr std::uintmax_t max_jsonl_bytes = 5U * 1024U * 1024U;

int current_turn_number()
{
    return to_turn<int>( calendar::turn );
}

std::string json_escape( const std::string &input )
{
    std::string result;
    result.reserve( input.size() + 8 );
    for( const unsigned char c : input ) {
        switch( c ) {
            case '\\': result += "\\\\"; break;
            case '"': result += "\\\""; break;
            case '\n': result += "\\n"; break;
            case '\r': result += "\\r"; break;
            case '\t': result += "\\t"; break;
            default:
                if( c >= 0x20 ) {
                    result.push_back( static_cast<char>( c ) );
                }
                break;
        }
    }
    return result;
}

std::filesystem::path debug_path()
{
    if( world_generator != nullptr && world_generator->active_world != nullptr ) {
        return world_generator->active_world->folder_path().get_unrelative_path() /
               "npc_ai_events.jsonl";
    }
    return std::filesystem::u8path( PATH_INFO::user_dir() ) / "npc_ai_events.jsonl";
}

void write_json_entity( std::ostream &output, const world_entity_snapshot &entity )
{
    output << "{\"kind\":\"" << json_escape( entity.kind )
           << "\",\"character_id\":" << entity.character_id
           << ",\"type_id\":\"" << json_escape( entity.type_id )
           << "\",\"name\":\"" << json_escape( entity.name )
           << "\",\"position\":[" << entity.x << ',' << entity.y << ',' << entity.z << "]}";
}

void append_debug_event( const world_event &event )
{
    if( !world_event_jsonl_debug_enabled() ) {
        return;
    }
    const std::filesystem::path path = debug_path();
    std::error_code error;
    std::filesystem::create_directories( path.parent_path(), error );
    if( std::filesystem::exists( path, error ) &&
        std::filesystem::file_size( path, error ) >= max_jsonl_bytes ) {
        const std::filesystem::path rotated = path.string() + ".1";
        std::filesystem::remove( rotated, error );
        error.clear();
        std::filesystem::rename( path, rotated, error );
    }
    std::ofstream output( path, std::ios::app | std::ios::binary );
    if( !output ) {
        return;
    }
    output << "{\"sequence_id\":" << event.sequence_id
           << ",\"turn\":" << event.game_turn
           << ",\"event_type\":\"" << world_event_type_name( event.type ) << "\""
           << ",\"actor\":";
    write_json_entity( output, event.actor );
    output << ",\"target\":";
    write_json_entity( output, event.target );
    output << ",\"previous_position\":[" << event.previous_x << ',' << event.previous_y << ','
           << event.previous_z << ']'
           << ",\"importance\":" << event.importance
           << ",\"audible_volume\":" << event.audible_volume
           << ",\"lazy_perception\":" << ( event.resolve_perception_lazily ? "true" : "false" )
           << ",\"encounter\":" << event.encounter_generation
           << ",\"confirmed_outcome\":" << ( event.confirmed_outcome ? "true" : "false" )
           << ",\"source\":\"" << json_escape( event.source ) << "\""
           << ",\"body_part\":\"" << json_escape( event.body_part ) << "\""
           << ",\"attack_mode\":\"" << json_escape( event.attack_mode ) << "\""
           << ",\"damage\":" << event.damage
           << ",\"claim_level\":\""
           << world_event_claim_level_name( event.claim_level ) << "\""
           << ",\"recipients\":[";
    for( std::size_t i = 0; i < event.known_by_npc_ids.size(); ++i ) {
        if( i > 0 ) {
            output << ',';
        }
        output << event.known_by_npc_ids[i];
    }
    output << "],\"coalesced_with\":[";
    for( std::size_t i = 0; i < event.coalesced_with.size(); ++i ) {
        if( i > 0 ) {
            output << ',';
        }
        output << event.coalesced_with[i];
    }
    output << "],\"candidate_state\":\"" << json_escape( event.candidate_state )
           << "\",\"selected\":"
           << ( event.selected ? "true" : "false" )
           << ",\"request_id\":" << event.request_id
           << ",\"discard_reason\":\"" << json_escape( event.discard_reason ) << "\"}"
           << '\n';
}

} // namespace

world_entity_snapshot snapshot_entity( const Creature *creature )
{
    world_entity_snapshot result;
    if( creature == nullptr ) {
        return result;
    }
    result.name = creature->disp_name();
    result.x = creature->pos_abs().x();
    result.y = creature->pos_abs().y();
    result.z = creature->pos_abs().z();
    if( const Character *character = creature->as_character() ) {
        result.kind = character->is_npc() ? "npc" : "player";
        result.character_id = character->getID().get_value();
    } else if( const monster *mon = creature->as_monster() ) {
        result.kind = "monster";
        result.type_id = mon->type->id.str();
    } else {
        result.kind = "creature";
    }
    return result;
}

std::string world_event_type_name( const world_event_type type )
{
    static const char *names[] = {
        "COMBAT_START", "COMBAT_END", "ENEMY_SPOTTED", "DANGEROUS_ENEMY_SPOTTED",
        "ENEMY_GROUP_DETECTED", "NPC_ATTACK", "NPC_HIT", "NPC_BADLY_HURT", "NPC_BLEEDING",
        "NPC_GRABBED", "PLAYER_HIT", "PLAYER_BADLY_HURT", "PLAYER_BLEEDING", "PLAYER_GRABBED",
        "PLAYER_SURROUNDED", "ALLY_HIT", "ALLY_BADLY_HURT", "ALLY_BLEEDING", "ALLY_GRABBED",
        "ALLY_SURROUNDED", "ENEMY_KILLED", "LOW_STAMINA", "LOW_AMMO", "OUT_OF_AMMO",
        "WEAPON_JAMMED", "RETREAT_STARTED", "ALLY_RETREATING", "FAILED_ESCAPE", "GRAB_BROKEN",
        "DRAGGED", "ALLY_DRAGGED", "SIGNIFICANT_CRITICAL", "ALLY_CRITICAL_HIT",
        "PLAYER_CRITICAL_HIT", "ATTACK_MISSED", "DODGE", "ALLY_SAVED", "HEAL_STARTED",
        "HEAL_COMPLETED"
    };
    const int index = static_cast<int>( type );
    return index >= 0 && index < static_cast<int>( sizeof( names ) / sizeof( names[0] ) ) ?
           names[index] : "UNKNOWN";
}

std::string world_event_claim_level_name( const world_event_claim_level level )
{
    switch( level ) {
        case world_event_claim_level::fact_only:
            return "FACT_ONLY";
        case world_event_claim_level::hit_confirmed:
            return "HIT_ONLY";
        case world_event_claim_level::limb_disabled:
            return "LIMB_DISABLED_CONFIRMED";
        case world_event_claim_level::death_confirmed:
            return "DEATH_CONFIRMED";
    }
    return "FACT_ONLY";
}

std::uint64_t record_world_event( world_event event )
{
    event.sequence_id = next_sequence++;
    event.session_generation = stream_session;
    if( event.game_turn == 0 ) {
        event.game_turn = current_turn_number();
    }
    events.push_back( std::move( event ) );
    if( events.size() > world_event_ring_capacity ) {
        events.pop_front();
    }
    append_debug_event( events.back() );
    return events.back().sequence_id;
}

std::uint64_t record_creature_world_event( const world_event_type type, const Creature *actor,
        const Creature *target, const int importance, const std::string &source,
        const std::string &detail, const bool confirmed_outcome,
        const std::uint64_t encounter_generation, const int audible_volume,
        const int previous_x, const int previous_y, const int previous_z,
        const std::string &body_part, const int damage,
        const std::string &attack_mode, const world_event_claim_level claim_level )
{
    scoped_profile profile( profile_subsystem::event_publish );
    world_event event;
    event.type = type;
    event.actor = snapshot_entity( actor );
    event.target = snapshot_entity( target );
    event.importance = importance;
    event.audible_volume = audible_volume;
    event.resolve_perception_lazily = true;
    event.source = source;
    event.detail = detail;
    event.confirmed_outcome = confirmed_outcome;
    event.encounter_generation = encounter_generation;
    event.previous_x = previous_x;
    event.previous_y = previous_y;
    event.previous_z = previous_z;
    event.body_part = body_part;
    event.damage = damage;
    event.attack_mode = attack_mode;
    event.claim_level = claim_level;

    // Hot combat hooks only capture immutable facts and participants.  Visual
    // and audible relevance is resolved lazily when an NPC consumes the new
    // suffix, avoiding NPC x LOS work inside every melee miss, dodge or drag.
    for( const world_entity_snapshot *participant : { &event.actor, &event.target } ) {
        if( participant->character_id >= 0 &&
            std::find( event.known_by_npc_ids.begin(), event.known_by_npc_ids.end(),
                       participant->character_id ) == event.known_by_npc_ids.end() ) {
            event.known_by_npc_ids.push_back( participant->character_id );
        }
    }
    return record_world_event( std::move( event ) );
}

std::vector<world_event> recent_world_events_for( const npc &observer,
        const std::uint64_t after_sequence, const std::size_t maximum,
        const int maximum_age_turns )
{
    std::vector<world_event> result;
    const int now = current_turn_number();
    const int observer_id = observer.getID().get_value();
    map &here = get_map();
    const auto observer_knows = [&]( world_event &event ) {
        if( std::find( event.known_by_npc_ids.begin(), event.known_by_npc_ids.end(), observer_id ) !=
            event.known_by_npc_ids.end() ) {
            return true;
        }
        if( !event.resolve_perception_lazily ) {
            return false;
        }
        const auto perceives_snapshot = [&]( const world_entity_snapshot &entity ) {
            const tripoint_bub_ms position = here.get_bub(
                                                 tripoint_abs_ms{ entity.x, entity.y, entity.z } );
            return !entity.kind.empty() && here.inbounds( position ) && observer.posz() == position.z() &&
                   rl_dist( observer.pos_bub( here ), position ) <= MAX_VIEW_DISTANCE &&
                   observer.sees( here, position );
        };
        bool known = perceives_snapshot( event.actor ) || perceives_snapshot( event.target );
        if( !known && event.audible_volume > 0 ) {
            const world_entity_snapshot &origin = !event.actor.kind.empty() ? event.actor : event.target;
            const tripoint_bub_ms position = here.get_bub(
                                                 tripoint_abs_ms{ origin.x, origin.y, origin.z } );
            known = !origin.kind.empty() && here.inbounds( position ) &&
                    observer.can_hear( position, event.audible_volume );
        }
        if( known ) {
            event.known_by_npc_ids.push_back( observer_id );
        }
        return known;
    };
    for( auto iter = events.rbegin(); iter != events.rend() && result.size() < maximum; ++iter ) {
        // Sequence and turn are monotonic in this bounded deque.  Once a
        // reverse scan reaches already-consumed or expired history, every
        // remaining entry is older too.  The old `continue` scanned all 256
        // entries for every NPC move even when there was no new event.
        if( iter->sequence_id <= after_sequence || now - iter->game_turn > maximum_age_turns ) {
            break;
        }
        if( !observer_knows( *iter ) ) {
            continue;
        }
        result.push_back( *iter );
    }
    std::reverse( result.begin(), result.end() );
    return result;
}

std::string build_recent_world_event_context( const npc &observer, const std::size_t maximum,
        const int maximum_age_turns )
{
    const std::vector<world_event> recent = recent_world_events_for(
                observer, 0, maximum, maximum_age_turns );
    if( recent.empty() ) {
        return {};
    }
    std::ostringstream output;
    output << "RECENT HISTORY (past facts only; CURRENT STATE always wins):\n";
    for( const world_event &event : recent ) {
        output << "- " << world_event_type_name( event.type ) << ": " << event.detail << '\n';
    }
    return output.str();
}

std::size_t world_event_stream_size()
{
    return events.size();
}

std::uint64_t latest_world_event_sequence()
{
    return events.empty() ? 0 : events.back().sequence_id;
}

std::optional<world_event> world_event_by_sequence( const std::uint64_t sequence_id )
{
    const auto found = std::find_if( events.begin(), events.end(),
    [sequence_id]( const world_event &event ) {
        return event.sequence_id == sequence_id;
    } );
    return found == events.end() ? std::nullopt : std::optional<world_event>( *found );
}

bool add_world_event_known_observer( const std::uint64_t sequence_id, const int observer_id )
{
    const auto found = std::find_if( events.begin(), events.end(), [&]( const world_event &event ) {
        return event.sequence_id == sequence_id;
    } );
    if( found == events.end() || observer_id < 0 ) {
        return false;
    }
    if( std::find( found->known_by_npc_ids.begin(), found->known_by_npc_ids.end(), observer_id ) !=
        found->known_by_npc_ids.end() ) {
        return false;
    }
    found->known_by_npc_ids.push_back( observer_id );
    append_debug_event( *found );
    return true;
}

void reset_world_event_stream()
{
    events.clear();
    // Sequence IDs remain monotonic for the lifetime of the process.  Session
    // generation distinguishes worlds while avoiding ID reuse in debug logs.
    ++stream_session;
}

void annotate_world_event( const std::uint64_t sequence_id, const std::string &candidate_state,
                           const bool selected, const std::uint64_t request_id,
                           const std::string &discard_reason,
                           const std::vector<std::uint64_t> &coalesced_with,
                           const std::uint64_t encounter_generation )
{
    const auto found = std::find_if( events.begin(), events.end(), [&]( const world_event &event ) {
        return event.sequence_id == sequence_id;
    } );
    if( found == events.end() ) {
        return;
    }
    found->candidate_state = candidate_state;
    found->selected = selected;
    found->request_id = request_id;
    found->discard_reason = discard_reason;
    if( !coalesced_with.empty() ) {
        found->coalesced_with = coalesced_with;
    }
    if( encounter_generation != 0 ) {
        found->encounter_generation = encounter_generation;
    }
    // JSONL is append-only: diagnostic transitions are new records sharing the
    // same sequence_id, so a human can reconstruct the candidate lifecycle.
    append_debug_event( *found );
}

void set_world_event_jsonl_debug( const bool enabled )
{
    debug_override_set = true;
    debug_override = enabled;
}

bool world_event_jsonl_debug_enabled()
{
    if( debug_override_set ) {
        return debug_override;
    }
    const char *value = std::getenv( "CDDA_NPC_AI_EVENT_DEBUG" );
    return value != nullptr && std::string( value ) == "1";
}

std::string world_event_jsonl_path()
{
    return debug_path().u8string();
}

} // namespace npc_ai
