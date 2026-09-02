#include "npc_ai_vehicle_unload.h"

#include <algorithm>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "activity_handlers.h"
#include "cata_utility.h"
#include "clzones.h"
#include "creature_tracker.h"
#include "debug.h"
#include "faction.h"
#include "game.h"
#include "item.h"
#include "item_location.h"
#include "map.h"
#include "npc.h"
#include "npc_ai_goal.h"
#include "npc_ai_perception.h"
#include "pathfinding.h"
#include "point.h"
#include "translations.h"
#include "vehicle.h"
#include "veh_type.h"
#include "vpart_position.h"

namespace
{

static const zone_type_id zone_type_LOOT_UNSORTED( "LOOT_UNSORTED" );

enum class unload_phase {
    approaching_source,
    verifying_source
};

struct vehicle_cargo_source {
    int part_index = -1;
    std::string part_type;
};

struct vehicle_candidate {
    vehicle *veh = nullptr;
    std::string name;
    std::string type;
    int distance = 0;
    bool moving = false;
    std::vector<vehicle_cargo_source> cargo_sources;
};

struct vehicle_unload_task {
    vehicle *veh = nullptr;
    std::string vehicle_name;
    std::string vehicle_type;
    std::vector<vehicle_cargo_source> sources;
    std::size_t source_index = 0;
    unload_phase phase = unload_phase::approaching_source;
    npc_ai::ai_goal_id goal_id = 0;
    bool unmoved_cargo = false;
};

std::unordered_map<int, vehicle_unload_task> unload_tasks;

int npc_key( const npc &who )
{
    return who.getID().get_value();
}

bool looks_like_vehicle_unload_command( const std::string &text )
{
    if( lcmatch( text, "no descarg" ) || lcmatch( text, "sin descargar" ) ||
        lcmatch( text, "do not unload" ) ) {
        return false;
    }
    const bool unload_verb = lcmatch( text, "descarg" ) || lcmatch( text, "unload" );
    const bool vehicle_target = lcmatch( text, "camion" ) || lcmatch( text, "camión" ) ||
                                lcmatch( text, "vehiculo" ) || lcmatch( text, "vehículo" ) ||
                                lcmatch( text, "coche" ) || lcmatch( text, "auto" ) ||
                                lcmatch( text, "carro" ) || lcmatch( text, "truck" ) ||
                                lcmatch( text, "vehicle" ) || lcmatch( text, "cargo" ) ||
                                lcmatch( text, "carga" );
    return unload_verb && vehicle_target;
}

std::vector<vehicle_candidate> visible_vehicle_candidates( const npc &who )
{
    map &here = get_map();
    const tripoint_bub_ms origin = who.pos_bub( here );
    const npc_ai::npc_sensory_snapshot snapshot = npc_ai::build_sensory_snapshot( who );
    std::unordered_map<vehicle *, vehicle_candidate> by_vehicle;

    for( const npc_ai::sensory_tile_observation &tile : snapshot.tiles ) {
        if( tile.knowledge != npc_ai::sensory_knowledge::currently_perceived ||
            tile.vehicle.knowledge != npc_ai::sensory_knowledge::currently_perceived ) {
            continue;
        }
        const tripoint_bub_ms position{ origin.x() + tile.dx, origin.y() + tile.dy,
                                        origin.z() + tile.dz };
        const optional_vpart_position vp = here.veh_at( position );
        if( !vp ) {
            continue;
        }
        vehicle &veh = vp->vehicle();
        vehicle_candidate &candidate = by_vehicle[&veh];
        if( candidate.veh == nullptr ) {
            candidate.veh = &veh;
            candidate.name = veh.name;
            candidate.type = veh.type.str();
            candidate.distance = tile.distance;
            candidate.moving = veh.is_moving();
        } else {
            candidate.distance = std::min( candidate.distance, tile.distance );
        }
    }

    std::vector<vehicle_candidate> result;
    result.reserve( by_vehicle.size() );
    for( auto &entry : by_vehicle ) {
        vehicle_candidate &candidate = entry.second;
        for( const vpart_reference &cargo_part : candidate.veh->get_avail_parts( VPFLAG_CARGO ) ) {
            if( cargo_part.items().empty() ) {
                continue;
            }
            candidate.cargo_sources.push_back( {
                static_cast<int>( cargo_part.part_index() ), cargo_part.info().id.str()
            } );
            add_msg_debug( debugmode::DF_NPC_ITEMAI,
                           "UNLOAD_SCAN vehicle=%s PART=%d feature=%s item_count=%d",
                           candidate.name, cargo_part.part_index(), cargo_part.info().id.str(),
                           cargo_part.items().size() );
        }
        std::sort( candidate.cargo_sources.begin(), candidate.cargo_sources.end(),
        [&]( const vehicle_cargo_source & lhs, const vehicle_cargo_source & rhs ) {
            return rl_dist( who.pos_abs(), candidate.veh->abs_part_pos( lhs.part_index ) ) <
                   rl_dist( who.pos_abs(), candidate.veh->abs_part_pos( rhs.part_index ) );
        } );
        result.push_back( std::move( candidate ) );
    }
    std::sort( result.begin(), result.end(), []( const vehicle_candidate &lhs,
    const vehicle_candidate &rhs ) {
        return lhs.distance < rhs.distance;
    } );
    return result;
}

bool vehicle_is_still_loaded( map &here, const vehicle *expected )
{
    const std::vector<wrapped_vehicle> &vehicles = here.get_vehicles();
    return std::any_of( vehicles.begin(), vehicles.end(), [expected]( const wrapped_vehicle &entry ) {
        return entry.v == expected;
    } );
}

bool valid_ground_destination( npc &who, const tripoint_bub_ms &position )
{
    map &here = get_map();
    return here.inbounds( position ) && here.passable_through( position ) &&
           here.can_put_items_ter_furn( position ) && !here.impassable_field_at( position ) &&
           !g->is_dangerous_tile( position ) && !here.veh_at( position ) &&
           get_creature_tracker().creature_at( position ) == nullptr &&
           static_cast<int>( here.i_at( position ).size() ) < MAX_ITEM_IN_SQUARE;
}

std::optional<tripoint_bub_ms> unload_destination( npc &who, const tripoint_bub_ms &source )
{
    map &here = get_map();
    zone_manager &zones = zone_manager::get_manager();
    const faction_id faction = who.get_faction()->id;
    const std::unordered_set<tripoint_abs_ms> unsorted = zones.get_near(
                zone_type_LOOT_UNSORTED, here.get_abs( source ), 3, nullptr, faction );

    std::vector<tripoint_bub_ms> candidates;
    candidates.reserve( unsorted.size() + 48 );
    for( const tripoint_abs_ms &position : unsorted ) {
        candidates.push_back( here.get_bub( position ) );
    }
    // Interior seats can be several tiles from the vehicle perimeter.  The NPC
    // temporarily handles the item in their hands and deposits it on the nearest
    // reachable ground outside the vehicle; no inventory pocket is involved.
    for( const tripoint_bub_ms &position : closest_points_first( source, 3 ) ) {
        if( position != source ) {
            candidates.push_back( position );
        }
    }
    std::stable_sort( candidates.begin(), candidates.end(), [&]( const tripoint_bub_ms &lhs,
    const tripoint_bub_ms &rhs ) {
        const bool lhs_zone = zones.has( zone_type_LOOT_UNSORTED, here.get_abs( lhs ), faction );
        const bool rhs_zone = zones.has( zone_type_LOOT_UNSORTED, here.get_abs( rhs ), faction );
        return std::make_tuple( !lhs_zone, rl_dist( who.pos_bub( here ), lhs ) ) <
               std::make_tuple( !rhs_zone, rl_dist( who.pos_bub( here ), rhs ) );
    } );
    candidates.erase( std::unique( candidates.begin(), candidates.end() ), candidates.end() );
    for( const tripoint_bub_ms &candidate : candidates ) {
        if( !valid_ground_destination( who, candidate ) ) {
            continue;
        }
        if( candidate == who.pos_bub( here ) ||
            !here.route( who, pathfinding_target::point( candidate ) ).empty() ) {
            return candidate;
        }
    }
    return std::nullopt;
}

bool cargo_item_is_eligible( npc &who, const item &it )
{
    return !it.has_var( "activity_var" ) && it.is_owned_by( who, true ) &&
           it.made_of_from_type( phase_id::SOLID );
}

bool move_cargo_item_to_ground( npc &who, vehicle &veh, vehicle_part &part, item &it,
                                const tripoint_bub_ms &source,
                                const tripoint_bub_ms &destination )
{
    const bool pocket_fit = who.can_pickVolume( it );
    const bool physically_movable = who.can_lift( it );
    add_msg_debug( debugmode::DF_NPC_ITEMAI,
                   "VEHICLE_PART=%d ITEM=%s ITEM_WEIGHT_GRAM=%d SOURCE_LOCATION=%s DESTINATION=%s "
                   "POCKET_FIT=%s PHYSICALLY_MOVABLE=%s MOVE_VALIDATION=%s",
                   veh.index_of_part( &part ), it.typeId().str(),
                   static_cast<int>( units::to_gram( it.weight() ) ), source.to_string(),
                   destination.to_string(), pocket_fit ? "true" : "false",
                   physically_movable ? "true" : "false",
                   physically_movable ? "success" : "too_heavy" );
    if( !physically_movable ) {
        add_msg_debug( debugmode::DF_NPC_ITEMAI,
                       "VEHICLE_PART=%d ITEM=%s MOVE_RESULT=failure FAIL_REASON=physically_too_heavy",
                       veh.index_of_part( &part ), it.typeId().str() );
        return false;
    }

    std::vector<item_location> placed = put_into_vehicle_or_drop_ret_locs(
                who, item_drop_reason::deliberate, { it }, &get_map(), destination, true );
    if( placed.empty() || !placed.front() ) {
        add_msg_debug( debugmode::DF_NPC_ITEMAI,
                       "VEHICLE_PART=%d ITEM=%s MOVE_RESULT=failure FAIL_REASON=ground_rejected_item",
                       veh.index_of_part( &part ), it.typeId().str() );
        return false;
    }
    who.mod_moves( -activity_handlers::move_cost( it, source, destination ) );
    const std::string item_id = it.typeId().str();
    if( !veh.remove_item( part, &it ) ) {
        placed.front().remove_item();
        add_msg_debug( debugmode::DF_NPC_ITEMAI,
                       "VEHICLE_PART=%d ITEM=%s MOVE_RESULT=failure FAIL_REASON=source_remove_failed",
                       veh.index_of_part( &part ), item_id );
        return false;
    }
    add_msg_debug( debugmode::DF_NPC_ITEMAI,
                   "VEHICLE_PART=%d ITEM=%s MOVE_RESULT=success GROUND_LOCATION=%s",
                   veh.index_of_part( &part ), item_id, destination.to_string() );
    return true;
}

const vehicle_candidate *select_vehicle( const std::vector<vehicle_candidate> &candidates,
        const std::string &command )
{
    if( candidates.size() == 1 ) {
        return &candidates.front();
    }
    const vehicle_candidate *matched = nullptr;
    for( const vehicle_candidate &candidate : candidates ) {
        if( ( candidate.name.empty() || !lcmatch( command, candidate.name ) ) &&
            ( candidate.type.empty() || !lcmatch( command, candidate.type ) ) ) {
            continue;
        }
        if( matched != nullptr ) {
            return nullptr;
        }
        matched = &candidate;
    }
    return matched;
}

bool route_adjacent_to( npc &who, const tripoint_bub_ms &target )
{
    map &here = get_map();
    const tripoint_bub_ms origin = who.pos_bub( here );
    std::vector<tripoint_bub_ms> candidates = closest_points_first( target, 1 );
    // Seats and other interior cargo can only be manipulated after boarding their
    // own passable tile.  Exterior cargo still rejects the impassable source tile.
    candidates.push_back( target );
    std::sort( candidates.begin(), candidates.end(), [&]( const tripoint_bub_ms &lhs,
    const tripoint_bub_ms &rhs ) {
        return rl_dist( origin, lhs ) < rl_dist( origin, rhs );
    } );
    for( const tripoint_bub_ms &candidate : candidates ) {
        if( candidate.z() != target.z() ||
            !here.passable_through( candidate ) || g->is_dangerous_tile( candidate ) ) {
            continue;
        }
        if( candidate == origin ) {
            who.path.clear();
            return true;
        }
        if( who.update_path( candidate, true ) && !who.path.empty() ) {
            return true;
        }
    }
    who.path.clear();
    return false;
}

void fail_task( npc &who, const std::string &message )
{
    const auto found = unload_tasks.find( npc_key( who ) );
    if( found != unload_tasks.end() ) {
        npc_ai::fail_goal( who, found->second.goal_id, message );
        unload_tasks.erase( found );
    }
    who.path.clear();
    who.say( message );
    who.move_pause();
}

void finish_task( npc &who, vehicle_unload_task &task )
{
    if( !task.unmoved_cargo ) {
        npc_ai::complete_goal( who, task.goal_id );
        unload_tasks.erase( npc_key( who ) );
        who.say( _( "I unloaded all cargo I could physically move." ) );
    } else {
        const std::string reason = _( "Some cargo could not be physically moved." );
        npc_ai::fail_goal( who, task.goal_id, reason );
        unload_tasks.erase( npc_key( who ) );
        who.say( _( "I unloaded what I could, but some cargo could not be moved." ) );
    }
}

} // namespace

namespace npc_ai
{

vehicle_unload_command_result try_handle_vehicle_unload_command( npc &who,
        const std::string &player_line )
{
    vehicle_unload_command_result result;
    if( !looks_like_vehicle_unload_command( player_line ) ) {
        return result;
    }
    result.handled = true;

    if( who.has_player_activity() || has_vehicle_unload_task( who ) ) {
        result.message = _( "I am busy with another task right now." );
        return result;
    }

    const std::vector<vehicle_candidate> candidates = visible_vehicle_candidates( who );
    if( candidates.empty() ) {
        result.message = _( "I cannot see a vehicle to unload." );
        return result;
    }
    const vehicle_candidate *selected = select_vehicle( candidates, player_line );
    if( selected == nullptr ) {
        result.message = _( "I can see more than one vehicle.  Which one do you mean?" );
        return result;
    }
    if( selected->moving ) {
        result.message = _( "I cannot unload a moving vehicle." );
        return result;
    }
    if( selected->cargo_sources.empty() ) {
        result.message = _( "I cannot see an accessible cargo area on that vehicle." );
        return result;
    }

    vehicle_unload_task task;
    task.veh = selected->veh;
    task.vehicle_name = selected->name;
    task.vehicle_type = selected->type;
    task.sources = selected->cargo_sources;
    task.goal_id = begin_goal( who, ai_goal_kind::unload_vehicle, ai_goal_priority::normal,
                              "unload visible vehicle cargo" );
    unload_tasks[npc_key( who )] = std::move( task );
    result.started = true;
    result.message = _( "I will unload all cargo I can physically move onto nearby ground." );
    return result;
}

bool process_vehicle_unload_task( npc &who )
{
    const auto found = unload_tasks.find( npc_key( who ) );
    if( found == unload_tasks.end() ) {
        return false;
    }
    vehicle_unload_task &task = found->second;

    if( task.source_index >= task.sources.size() ) {
        finish_task( who, task );
        return true;
    }

    map &here = get_map();
    const vehicle_cargo_source &source_record = task.sources[task.source_index];
    if( !vehicle_is_still_loaded( here, task.veh ) || source_record.part_index < 0 ||
        source_record.part_index >= task.veh->part_count() ) {
        fail_task( who, _( "The vehicle cargo moved out of reach." ) );
        return true;
    }
    vehicle_part &cargo_part = task.veh->part( source_record.part_index );
    const tripoint_abs_ms source_abs = task.veh->abs_part_pos( cargo_part );
    const tripoint_bub_ms source = here.get_bub( source_abs );
    if( !here.inbounds( source ) || cargo_part.info().id.str() != source_record.part_type ||
        !cargo_part.info().has_flag( VPFLAG_CARGO ) || !cargo_part.is_available() ||
        task.veh->name != task.vehicle_name || task.veh->type.str() != task.vehicle_type ) {
        fail_task( who, _( "The vehicle is no longer where I saw it." ) );
        return true;
    }
    if( task.veh->is_moving() ) {
        fail_task( who, _( "The vehicle started moving, so I stopped unloading it." ) );
        return true;
    }

    if( who.current_target() != nullptr ) {
        return false;
    }

    if( rl_dist( who.pos_bub( here ), source ) > 1 ) {
        if( who.current_target() != nullptr ) {
            return false;
        }
        if( who.path.empty() && !route_adjacent_to( who, source ) ) {
            fail_task( who, _( "I cannot reach the vehicle cargo." ) );
            return true;
        }
        who.move_to_next();
        return true;
    }


    if( task.phase == unload_phase::verifying_source ) {
        task.unmoved_cargo = task.unmoved_cargo || !task.veh->get_items( cargo_part ).empty();
        ++task.source_index;
        task.phase = unload_phase::approaching_source;
        if( task.source_index >= task.sources.size() ) {
            finish_task( who, task );
        }
        return true;
    }

    const std::optional<tripoint_bub_ms> destination = unload_destination( who, source );
    if( !destination ) {
        task.unmoved_cargo = true;
        add_msg_debug( debugmode::DF_NPC_ITEMAI,
                       "VEHICLE_PART=%d MOVE_RESULT=failure FAIL_REASON=no_reachable_ground_destination",
                       source_record.part_index );
        task.phase = unload_phase::verifying_source;
        return true;
    }

    vehicle_stack cargo = task.veh->get_items( cargo_part );
    for( item &it : cargo ) {
        if( !cargo_item_is_eligible( who, it ) ) {
            add_msg_debug( debugmode::DF_NPC_ITEMAI,
                           "VEHICLE_PART=%d ITEM=%s MOVE_RESULT=failure FAIL_REASON=ineligible_item",
                           source_record.part_index, it.typeId().str() );
            continue;
        }
        if( move_cargo_item_to_ground( who, *task.veh, cargo_part, it, source, *destination ) ) {
            return true;
        }
    }
    task.phase = unload_phase::verifying_source;
    return true;
}

bool has_vehicle_unload_task( const npc &who )
{
    return unload_tasks.find( npc_key( who ) ) != unload_tasks.end();
}

void cancel_vehicle_unload_task( const npc &who )
{
    const auto found = unload_tasks.find( npc_key( who ) );
    if( found != unload_tasks.end() ) {
        fail_goal( who, found->second.goal_id, "cancelled" );
        unload_tasks.erase( found );
    }
}

void reset_all_vehicle_unload_tasks()
{
    unload_tasks.clear();
}

} // namespace npc_ai
