#include "npc_ai_fire.h"

#include <algorithm>
#include <cstddef>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "activity_actor_definitions.h"
#include "activity_type.h"
#include "cata_utility.h"
#include "clzones.h"
#include "field_type.h"
#include "flag.h"
#include "game.h"
#include "item.h"
#include "item_location.h"
#include "iuse_actor.h"
#include "map.h"
#include "map_selector.h"
#include "npc.h"
#include "output.h"
#include "npc_ai_goal.h"
#include "npc_ai_perception.h"
#include "point.h"
#include "translations.h"

namespace
{

static const activity_id ACT_START_FIRE( "ACT_START_FIRE" );
static const zone_type_id zone_type_NO_NPC_PICKUP( "NO_NPC_PICKUP" );

constexpr int fuel_search_radius = 8;
constexpr int pickup_timeout_turns = 240;

enum class start_fire_phase {
    retrieving_fuel,
    approaching_stove,
    dropping_fuel,
    igniting
};

struct start_fire_task {
    tripoint_abs_ms target;
    npc_ai::ai_goal_id goal_id = 0;
    start_fire_phase phase = start_fire_phase::approaching_stove;
    item_location fuel_target;
    item_location carried_fuel;
    itype_id fuel_id;
    std::string fuel_name;
    int pickup_wait_turns = 0;
};

struct stove_candidate {
    tripoint_bub_ms position;
    std::string furniture_id;
    std::string furniture_name;
    int distance = 0;
};

struct firestarter_choice {
    item *usable_item = nullptr;
    const firestarter_actor *actor = nullptr;
    std::string failure;
};

struct fuel_candidate {
    item_location location;
    tripoint_bub_ms position;
    int price = 0;
    int distance = 0;
};

std::unordered_map<int, start_fire_task> start_fire_tasks;

int npc_key( const npc &who )
{
    return who.getID().get_value();
}

bool looks_like_start_fire_command( const std::string &text )
{
    if( lcmatch( text, "no enciend" ) || lcmatch( text, "no prend" ) ||
        lcmatch( text, "sin encender" ) ) {
        return false;
    }

    const bool ignition_verb = lcmatch( text, "enciend" ) || lcmatch( text, "prend" ) ||
                               lcmatch( text, "haz fuego" ) || lcmatch( text, "hacer fuego" ) ||
                               lcmatch( text, "pon a funcionar" ) || lcmatch( text, "light the" ) ||
                               lcmatch( text, "start a fire" );
    const bool fire_target = lcmatch( text, "cocina" ) || lcmatch( text, "estufa" ) ||
                             lcmatch( text, "fuego" ) || lcmatch( text, "chimenea" ) ||
                             lcmatch( text, "brasero" ) || lcmatch( text, "stove" ) ||
                             lcmatch( text, "fireplace" ) || lcmatch( text, "fire" );
    return ignition_verb && fire_target;
}

std::vector<stove_candidate> visible_fire_containers( const npc &who )
{
    map &here = get_map();
    const tripoint_bub_ms origin = who.pos_bub( here );
    const npc_ai::npc_sensory_snapshot snapshot = npc_ai::build_sensory_snapshot( who );
    std::vector<stove_candidate> result;

    for( const npc_ai::sensory_tile_observation &tile : snapshot.tiles ) {
        if( tile.knowledge != npc_ai::sensory_knowledge::currently_perceived ||
            !tile.fire_container || tile.furniture_id.empty() ) {
            continue;
        }
        stove_candidate candidate;
        candidate.position = tripoint_bub_ms{ origin.x() + tile.dx, origin.y() + tile.dy,
                                              origin.z() + tile.dz };
        candidate.furniture_id = tile.furniture_id;
        candidate.furniture_name = tile.furniture_name;
        candidate.distance = tile.distance;
        result.push_back( std::move( candidate ) );
    }

    std::sort( result.begin(), result.end(), []( const stove_candidate &lhs,
    const stove_candidate &rhs ) {
        return lhs.distance < rhs.distance;
    } );
    return result;
}

const stove_candidate *select_stove( const std::vector<stove_candidate> &candidates,
                                     const std::string &command )
{
    if( candidates.size() == 1 ) {
        return &candidates.front();
    }

    const stove_candidate *matched = nullptr;
    for( const stove_candidate &candidate : candidates ) {
        bool matches = !candidate.furniture_name.empty() &&
                       lcmatch( command, candidate.furniture_name );
        if( !matches && ( lcmatch( command, "cocina" ) || lcmatch( command, "estufa" ) ||
                          lcmatch( command, "stove" ) ) ) {
            matches = candidate.furniture_id.find( "stove" ) != std::string::npos;
        }
        if( !matches && ( lcmatch( command, "chimenea" ) || lcmatch( command, "fireplace" ) ) ) {
            matches = candidate.furniture_id.find( "fireplace" ) != std::string::npos;
        }
        if( !matches ) {
            continue;
        }
        if( matched != nullptr ) {
            return nullptr;
        }
        matched = &candidate;
    }
    return matched;
}

firestarter_choice find_usable_firestarter( npc &who, const tripoint_bub_ms &target )
{
    firestarter_choice best;
    int best_moves = 0;

    for( item_location &location : who.all_items_loc() ) {
        if( !location || location.where_recursive() != item_location::type::character ) {
            continue;
        }
        item *usable = location->get_usable_item( "firestarter" );
        if( usable == nullptr ) {
            continue;
        }
        const use_function *use = usable->type->get_use( "firestarter" );
        if( use == nullptr || use->get_actor_ptr() == nullptr ) {
            continue;
        }
        const firestarter_actor *actor = dynamic_cast<const firestarter_actor *>(
                                             use->get_actor_ptr() );
        if( actor == nullptr ) {
            continue;
        }
        const ret_val<void> usable_now = actor->can_use( who, *usable, &get_map(), target );
        if( !usable_now.success() ) {
            if( best.failure.empty() ) {
                best.failure = usable_now.str();
            }
            continue;
        }
        if( usable->has_flag( flag_REQUIRES_TINDER ) && !get_map().tinder_at( target ) ) {
            if( best.failure.empty() ) {
                best.failure = _( "this firestarter requires tinder" );
            }
            continue;
        }
        if( best.usable_item == nullptr || actor->moves_cost_fast < best_moves ) {
            best.usable_item = usable;
            best.actor = actor;
            best_moves = actor->moves_cost_fast;
        }
    }
    return best;
}

std::optional<tripoint_bub_ms> route_adjacent_to( npc &who, const tripoint_bub_ms &target )
{
    map &here = get_map();
    const tripoint_bub_ms origin = who.pos_bub( here );
    std::vector<tripoint_bub_ms> candidates = closest_points_first( target, 1 );
    std::sort( candidates.begin(), candidates.end(), [&]( const tripoint_bub_ms &lhs,
    const tripoint_bub_ms &rhs ) {
        return rl_dist( origin, lhs ) < rl_dist( origin, rhs );
    } );

    for( const tripoint_bub_ms &candidate : candidates ) {
        if( candidate == target || candidate.z() != target.z() ||
            !here.passable_through( candidate ) || g->is_dangerous_tile( candidate ) ) {
            continue;
        }
        if( candidate == origin ) {
            who.path.clear();
            return candidate;
        }
        if( who.update_path( candidate, true ) && !who.path.empty() ) {
            return candidate;
        }
    }
    who.path.clear();
    return std::nullopt;
}

std::vector<fuel_candidate> visible_firewood( npc &who )
{
    map &here = get_map();
    const tripoint_bub_ms origin = who.pos_bub( here );
    std::vector<fuel_candidate> result;

    for( const tripoint_bub_ms &position : here.points_in_radius( origin, fuel_search_radius ) ) {
        if( position.z() != origin.z() || !who.sees( here, position ) ||
            !here.could_see_items( position, who ) ) {
            continue;
        }
        if( who.is_player_ally() && g->check_zone( zone_type_NO_NPC_PICKUP, position ) ) {
            continue;
        }
        for( item &candidate_item : here.i_at( position ) ) {
            if( !candidate_item.has_flag( flag_FIREWOOD ) || candidate_item.is_favorite ) {
                continue;
            }
            item_location location( map_cursor( &here, position ), &candidate_item );
            if( !location || !who.can_take_that( candidate_item ) ||
                !who.would_take_that( candidate_item, position ) ) {
                continue;
            }
            result.push_back( { location, position, candidate_item.price( true ),
                                rl_dist( origin, position ) } );
        }
    }

    std::sort( result.begin(), result.end(), []( const fuel_candidate &lhs,
    const fuel_candidate &rhs ) {
        if( lhs.price != rhs.price ) {
            return lhs.price < rhs.price;
        }
        return lhs.distance < rhs.distance;
    } );
    return result;
}

item_location find_carried_fuel( npc &who, const itype_id &wanted )
{
    for( item_location &location : who.all_items_loc() ) {
        if( location && location.where_recursive() == item_location::type::character &&
            location->typeId() == wanted && location->has_flag( flag_FIREWOOD ) ) {
            return location;
        }
    }
    return item_location();
}

void fail_task( npc &who, const std::string &message )
{
    const auto found = start_fire_tasks.find( npc_key( who ) );
    if( found != start_fire_tasks.end() ) {
        npc_ai::fail_goal( who, found->second.goal_id, message );
    }
    start_fire_tasks.erase( npc_key( who ) );
    who.path.clear();
    who.say( message );
    who.move_pause();
}

bool ignite_stove( npc &who, start_fire_task &task, const tripoint_bub_ms &target )
{
    map &here = get_map();
    if( here.get_field( target, fd_fire ) ) {
        npc_ai::complete_goal( who, task.goal_id );
        start_fire_tasks.erase( npc_key( who ) );
        who.say( _( "The stove is already burning." ) );
        return true;
    }
    if( !here.is_flammable( target ) ) {
        fail_task( who, _( "There is no usable fuel in the stove." ) );
        return true;
    }

    firestarter_choice choice = find_usable_firestarter( who, target );
    if( choice.usable_item == nullptr || choice.actor == nullptr ) {
        const std::string reason = choice.failure.empty() ?
                                   _( "I do not have a usable firestarter." ) : choice.failure;
        fail_task( who, string_format( _( "I cannot light the stove: %s." ), reason ) );
        return true;
    }

    item &firestarter = *choice.usable_item;
    const itype_id firestarter_id = firestarter.typeId();
    const std::optional<int> charges = choice.actor->use( &who, firestarter, &here, target );
    if( !charges ) {
        fail_task( who, _( "I could not start the fire." ) );
        return true;
    }
    if( *charges > 0 ) {
        who.use_charges( firestarter_id, *charges );
    }

    if( here.get_field( target, fd_fire ) ) {
        npc_ai::complete_goal( who, task.goal_id );
        start_fire_tasks.erase( npc_key( who ) );
        who.say( _( "The stove is lit." ) );
        return true;
    }
    if( who.activity.id() == ACT_START_FIRE ) {
        task.phase = start_fire_phase::igniting;
        return true;
    }

    fail_task( who, _( "I tried to light the stove, but it did not catch." ) );
    return true;
}

} // namespace

namespace npc_ai
{

start_fire_command_result try_handle_start_fire_command( npc &who,
        const std::string &player_line )
{
    start_fire_command_result result;
    if( !looks_like_start_fire_command( player_line ) ) {
        return result;
    }
    result.handled = true;

    if( who.has_player_activity() ) {
        result.message = _( "I am busy with another task right now." );
        return result;
    }

    const std::vector<stove_candidate> candidates = visible_fire_containers( who );
    if( candidates.empty() ) {
        result.message = _( "I cannot see a stove or fireplace that I can light." );
        return result;
    }
    const stove_candidate *selected = select_stove( candidates, player_line );
    if( selected == nullptr ) {
        result.message = _( "I can see more than one place for a fire.  Which one do you mean?" );
        return result;
    }

    map &here = get_map();
    if( here.get_field( selected->position, fd_fire ) ) {
        result.success = true;
        result.message = _( "The stove is already burning." );
        return result;
    }

    const firestarter_choice firestarter = find_usable_firestarter( who, selected->position );
    if( firestarter.usable_item == nullptr ) {
        result.message = firestarter.failure.empty() ?
                         _( "I do not have a usable firestarter." ) :
                         string_format( _( "I cannot use my firestarter: %s." ), firestarter.failure );
        return result;
    }

    start_fire_task task;
    task.target = here.get_abs( selected->position );

    if( !here.is_flammable( selected->position ) ) {
        std::vector<fuel_candidate> fuel = visible_firewood( who );
        std::string pickup_error;
        bool pickup_started = false;
        for( fuel_candidate &candidate : fuel ) {
            if( who.ai_request_pickup( candidate.location, candidate.position, pickup_error ) ) {
                task.phase = start_fire_phase::retrieving_fuel;
                task.fuel_target = candidate.location;
                task.fuel_id = candidate.location->typeId();
                task.fuel_name = remove_color_tags( candidate.location->tname() );
                pickup_started = true;
                break;
            }
        }
        if( !pickup_started ) {
            result.message = fuel.empty() ? _( "There is no fuel in the stove, and I cannot see suitable firewood." ) :
                             string_format( _( "I can see firewood, but I cannot reach or carry it: %s." ),
                                            pickup_error );
            return result;
        }
    } else {
        task.phase = start_fire_phase::approaching_stove;
        if( rl_dist( who.pos_bub( here ), selected->position ) > 1 &&
            !route_adjacent_to( who, selected->position ) ) {
            result.message = _( "I cannot reach a safe position next to the stove." );
            return result;
        }
    }

    task.goal_id = begin_goal( who, ai_goal_kind::light_stove, ai_goal_priority::normal,
                              "light stove" );
    start_fire_tasks[npc_key( who )] = std::move( task );
    result.started = true;
    result.message = here.is_flammable( selected->position ) ?
                     _( "I will light the stove." ) :
                     _( "I will get suitable firewood and then light the stove." );
    return result;
}

bool process_start_fire_task( npc &who )
{
    const auto found = start_fire_tasks.find( npc_key( who ) );
    if( found == start_fire_tasks.end() ) {
        return false;
    }
    start_fire_task &task = found->second;
    map &here = get_map();
    const tripoint_bub_ms target = here.get_bub( task.target );

    if( task.phase == start_fire_phase::igniting ) {
        if( who.activity.id() == ACT_START_FIRE ) {
            return false;
        }
        if( here.get_field( target, fd_fire ) ) {
            complete_goal( who, task.goal_id );
            start_fire_tasks.erase( found );
            who.say( _( "The stove is lit." ) );
        } else {
            fail_task( who, _( "The fuel did not ignite." ) );
            return true;
        }
        return false;
    }

    if( task.phase == start_fire_phase::dropping_fuel ) {
        if( who.has_player_activity() ) {
            return false;
        }
        if( !here.is_flammable( target ) ) {
            fail_task( who, _( "The fuel is no longer in the stove." ) );
            return true;
        }
        task.phase = start_fire_phase::approaching_stove;
    }

    if( task.phase == start_fire_phase::retrieving_fuel ) {
        if( task.fuel_target &&
            task.fuel_target.where_recursive() == item_location::type::map ) {
            if( ++task.pickup_wait_turns > pickup_timeout_turns ) {
                fail_task( who, _( "I could not finish retrieving the firewood." ) );
                return true;
            }
            return false;
        }
        task.carried_fuel = task.fuel_target &&
                            task.fuel_target.where_recursive() == item_location::type::character ?
                            task.fuel_target : find_carried_fuel( who, task.fuel_id );
        if( !task.carried_fuel ) {
            fail_task( who, string_format( _( "I no longer have the %s I picked up for fuel." ),
                                           task.fuel_name ) );
            return true;
        }
        task.phase = start_fire_phase::approaching_stove;
    }

    if( here.get_field( target, fd_fire ) ) {
        complete_goal( who, task.goal_id );
        start_fire_tasks.erase( found );
        who.say( _( "The stove is already burning." ) );
        return false;
    }
    if( !here.has_flag_furn( ter_furn_flag::TFLAG_FIRE_CONTAINER, target ) ) {
        fail_task( who, _( "The stove is no longer there." ) );
        return true;
    }

    if( rl_dist( who.pos_bub( here ), target ) > 1 ) {
        if( who.current_target() != nullptr ) {
            return false;
        }
        if( who.path.empty() && !route_adjacent_to( who, target ) ) {
            fail_task( who, _( "I cannot reach the stove." ) );
            return true;
        }
        who.move_to_next();
        return true;
    }

    if( task.carried_fuel ) {
        const tripoint_rel_ms placement = target - who.pos_bub( here );
        const int count = task.carried_fuel->count_by_charges() ?
                          std::min( task.carried_fuel->count(), 1 ) : 1;
        const std::vector<drop_or_stash_item_info> to_drop = {
            drop_or_stash_item_info( task.carried_fuel, count )
        };
        who.assign_activity( drop_activity_actor( to_drop, placement, true ) );
        task.carried_fuel = item_location();
        task.phase = start_fire_phase::dropping_fuel;
        return true;
    }

    return ignite_stove( who, task, target );
}

bool has_start_fire_task( const npc &who )
{
    return start_fire_tasks.find( npc_key( who ) ) != start_fire_tasks.end();
}

void cancel_start_fire_task( const npc &who )
{
    const auto found = start_fire_tasks.find( npc_key( who ) );
    if( found != start_fire_tasks.end() ) {
        fail_goal( who, found->second.goal_id, "cancelled" );
    }
    start_fire_tasks.erase( npc_key( who ) );
}

void reset_all_start_fire_tasks()
{
    start_fire_tasks.clear();
}

} // namespace npc_ai
