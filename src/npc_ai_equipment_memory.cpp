#include "npc_ai_equipment_memory.h"

#include <algorithm>
#include <string>
#include <unordered_set>
#include <unordered_map>
#include <utility>
#include <vector>

#include "activity_actor_definitions.h"
#include "debug.h"
#include "game.h"
#include "item.h"
#include "item_location.h"
#include "map.h"
#include "map_selector.h"
#include "npc.h"
#include "output.h"
#include "npc_ai_goal.h"
#include "rng.h"
#include "translations.h"

namespace
{

static const std::string var_uid = "npc_ai_equipment_uid";
static const std::string var_owner = "npc_ai_equipment_owner";
static const std::string var_reason = "npc_ai_drop_reason";
static const std::string var_retrieval = "npc_ai_retrieval_expected";
static const std::string memory_index_key = "npc_ai_equipment_memory_index";
static const std::string memory_prefix = "npc_ai_equipment_memory_";

std::unordered_map<int, std::vector<npc_ai::dropped_equipment_memory>> memories;
std::unordered_set<int> loaded_npcs;

std::vector<std::string> split_uids( const std::string &index )
{
    std::vector<std::string> result;
    std::size_t begin = 0;
    while( begin < index.size() ) {
        const std::size_t end = index.find( ';', begin );
        const std::string uid = index.substr( begin, end == std::string::npos ?
                                             std::string::npos : end - begin );
        if( !uid.empty() ) {
            result.push_back( uid );
        }
        if( end == std::string::npos ) {
            break;
        }
        begin = end + 1;
    }
    return result;
}

int value_as_int( const npc &who, const std::string &key )
{
    return static_cast<int>( who.get_value( key ).dbl() );
}

std::string status_string( npc_ai::equipment_memory_status status )
{
    switch( status ) {
        case npc_ai::equipment_memory_status::dropped:
            return "dropped";
        case npc_ai::equipment_memory_status::recovering:
            return "recovering";
        case npc_ai::equipment_memory_status::recovered:
            return "recovered";
        case npc_ai::equipment_memory_status::missing:
            return "missing";
    }
    return "dropped";
}

npc_ai::equipment_memory_status parse_status( const std::string &status )
{
    if( status == "recovering" ) {
        return npc_ai::equipment_memory_status::recovering;
    }
    if( status == "recovered" ) {
        return npc_ai::equipment_memory_status::recovered;
    }
    if( status == "missing" ) {
        return npc_ai::equipment_memory_status::missing;
    }
    return npc_ai::equipment_memory_status::dropped;
}

void persist_memory( npc &who, const npc_ai::dropped_equipment_memory &record )
{
    const std::string base = memory_prefix + record.item_uid + "_";
    who.set_value( base + "type", record.item_type );
    who.set_value( base + "name", record.item_name );
    who.set_value( base + "x", record.location.x() );
    who.set_value( base + "y", record.location.y() );
    who.set_value( base + "z", record.location.z() );
    who.set_value( base + "reason", record.reason );
    who.set_value( base + "turn", to_turn<int>( record.dropped_at ) );
    who.set_value( base + "expected", record.retrieval_expected ? "true" : "false" );
    who.set_value( base + "wear", record.wear_after_recovery ? "true" : "false" );
    who.set_value( base + "status", status_string( record.status ) );

    std::vector<std::string> index = split_uids( who.get_value( memory_index_key ).str() );
    if( std::find( index.begin(), index.end(), record.item_uid ) == index.end() ) {
        index.push_back( record.item_uid );
        std::string serialized;
        for( const std::string &uid : index ) {
            if( !serialized.empty() ) {
                serialized += ';';
            }
            serialized += uid;
        }
        who.set_value( memory_index_key, serialized );
    }
}

void ensure_memories_loaded( npc &who )
{
    const int key = who.getID().get_value();
    if( loaded_npcs.insert( key ).second == false ) {
        return;
    }
    std::vector<npc_ai::dropped_equipment_memory> &records = memories[key];
    for( const std::string &uid : split_uids( who.get_value( memory_index_key ).str() ) ) {
        const std::string base = memory_prefix + uid + "_";
        npc_ai::dropped_equipment_memory record;
        record.item_uid = uid;
        record.item_type = who.get_value( base + "type" ).str();
        record.item_name = who.get_value( base + "name" ).str();
        record.owner_id = key;
        record.location = tripoint_abs_ms{ value_as_int( who, base + "x" ),
                                           value_as_int( who, base + "y" ),
                                           value_as_int( who, base + "z" ) };
        record.reason = who.get_value( base + "reason" ).str();
        record.dropped_at = time_point( value_as_int( who, base + "turn" ) );
        record.retrieval_expected = who.get_value( base + "expected" ).str() == "true";
        record.wear_after_recovery = who.get_value( base + "wear" ).str() == "true";
        record.status = parse_status( who.get_value( base + "status" ).str() );
        records.push_back( std::move( record ) );
    }
}

std::string ensure_equipment_uid( npc &who, item &it )
{
    std::string uid = it.get_var( var_uid );
    if( uid.empty() ) {
        uid = std::to_string( who.getID().get_value() ) + "-" +
              std::to_string( to_turn<int>( calendar::turn ) ) + "-" + random_string( 12 );
        it.set_var( var_uid, uid );
    }
    return uid;
}

npc_ai::dropped_equipment_memory *find_memory( npc &who, const std::string &uid )
{
    ensure_memories_loaded( who );
    std::vector<npc_ai::dropped_equipment_memory> &records =
        memories[who.getID().get_value()];
    for( npc_ai::dropped_equipment_memory &record : records ) {
        if( record.item_uid == uid ) {
            return &record;
        }
    }
    return nullptr;
}

bool npc_owns_uid( npc &who, const std::string &uid )
{
    for( const item_location &location : who.all_items_loc() ) {
        if( location && location->get_var( var_uid ) == uid ) {
            return true;
        }
    }
    return false;
}

item_location owned_item_with_uid( npc &who, const std::string &uid )
{
    for( const item_location &location : who.all_items_loc() ) {
        if( location && location->get_var( var_uid ) == uid ) {
            return location;
        }
    }
    return item_location::nowhere;
}

item_location visible_item_with_uid( npc &who, const npc_ai::dropped_equipment_memory &record,
                                     bool &location_visible )
{
    map &here = get_map();
    const tripoint_bub_ms position = here.get_bub( record.location );
    location_visible = here.inbounds( position ) && who.sees( here, position ) &&
                       here.could_see_items( position, who );
    if( !location_visible ) {
        return item_location::nowhere;
    }
    for( item &it : here.i_at( position ) ) {
        if( it.get_var( var_uid ) == record.item_uid ) {
            return item_location( map_cursor( &here, position ), &it );
        }
    }
    return item_location::nowhere;
}

} // namespace

namespace npc_ai
{

std::string remember_dropped_equipment( npc &who, item &it,
                                        const tripoint_abs_ms &location,
                                        const std::string &reason,
                                        const bool retrieval_expected )
{
    const std::string uid = ensure_equipment_uid( who, it );
    it.set_var( var_owner, who.getID().get_value() );
    it.set_var( var_reason, reason );
    it.set_var( var_retrieval, retrieval_expected ? "true" : "false" );
    it.set_var( "npc_ai_drop_x", location.x() );
    it.set_var( "npc_ai_drop_y", location.y() );
    it.set_var( "npc_ai_drop_z", location.z() );
    it.set_var( "npc_ai_drop_turn", to_turn<int>( calendar::turn ) );

    dropped_equipment_memory record;
    record.item_uid = uid;
    record.item_type = it.typeId().str();
    record.item_name = remove_color_tags( it.tname() );
    record.owner_id = who.getID().get_value();
    record.location = location;
    record.reason = reason;
    record.dropped_at = calendar::turn;
    record.retrieval_expected = retrieval_expected;
    record.wear_after_recovery = retrieval_expected && it.is_armor() && who.is_worn( it );
    record.status = equipment_memory_status::dropped;

    if( dropped_equipment_memory *existing = find_memory( who, uid ) ) {
        *existing = record;
        persist_memory( who, *existing );
    } else {
        memories[record.owner_id].push_back( std::move( record ) );
        persist_memory( who, memories[who.getID().get_value()].back() );
    }
    return uid;
}

std::string remember_involuntary_weapon_drop( Character &who, item &it,
        const tripoint_abs_ms &location, const std::string &reason )
{
    npc *owner = dynamic_cast<npc *>( &who );
    if( owner == nullptr ) {
        return {};
    }
    return remember_dropped_equipment( *owner, it, location, reason, true );
}

std::vector<dropped_equipment_memory> get_dropped_equipment_memories( const npc &who )
{
    ensure_memories_loaded( const_cast<npc &>( who ) );
    const auto found = memories.find( who.getID().get_value() );
    return found == memories.end() ? std::vector<dropped_equipment_memory>() : found->second;
}

bool request_equipment_recovery( npc &who, const std::string &item_uid,
                                 const bool wear_after_recovery, std::string &error )
{
    dropped_equipment_memory *record = find_memory( who, item_uid );
    if( record == nullptr || record->owner_id != who.getID().get_value() ) {
        error = _( "I don't remember leaving that equipment anywhere." );
        return false;
    }
    if( record->status == equipment_memory_status::missing ) {
        error = string_format( _( "My %s is no longer where I left it." ), record->item_name );
        return false;
    }
    if( record->status == equipment_memory_status::recovered ) {
        error = string_format( _( "I already recovered %s." ), record->item_name );
        return false;
    }

    bool location_visible = false;
    item_location target = visible_item_with_uid( who, *record, location_visible );
    if( location_visible && !target ) {
        record->status = equipment_memory_status::missing;
        record->retrieval_expected = false;
        persist_memory( who, *record );
        error = string_format( _( "My %s is no longer where I left it." ), record->item_name );
        return false;
    }
    if( target && wear_after_recovery ) {
        const ret_val<void> can_wear = who.can_wear( *target );
        if( !can_wear.success() ) {
            error = string_format( _( "I can't wear %s: %s" ), record->item_name, can_wear.str() );
            return false;
        }
    }

    record->retrieval_expected = true;
    record->wear_after_recovery = wear_after_recovery;
    record->status = equipment_memory_status::recovering;
    if( record->recovery_goal_id == 0 ) {
        record->recovery_goal_id = begin_goal( who, ai_goal_kind::recover_equipment,
                                               ai_goal_priority::high,
                                               "recover " + record->item_name );
    }
    persist_memory( who, *record );
    error.clear();
    return true;
}

void clear_dropped_equipment_memories_for_test( const npc &who )
{
    npc &mutable_who = const_cast<npc &>( who );
    for( const std::string &uid : split_uids( mutable_who.get_value( memory_index_key ).str() ) ) {
        const std::string base = memory_prefix + uid + "_";
        for( const std::string &field : { "type", "name", "x", "y", "z", "reason", "turn",
                                         "expected", "wear", "status" } ) {
            mutable_who.remove_value( base + field );
        }
    }
    mutable_who.remove_value( memory_index_key );
    clear_runtime_equipment_memory_cache_for_test( who );
}

void clear_runtime_equipment_memory_cache_for_test( const npc &who )
{
    const int key = who.getID().get_value();
    memories.erase( key );
    loaded_npcs.erase( key );
}

bool process_equipment_recovery( npc &who )
{
    ensure_memories_loaded( who );
    std::vector<dropped_equipment_memory> &records = memories[who.getID().get_value()];
    for( dropped_equipment_memory &record : records ) {
        if( record.status == equipment_memory_status::recovered ||
            record.status == equipment_memory_status::missing ) {
            continue;
        }
        if( !record.retrieval_expected || !who.is_safe() || who.has_player_activity() ) {
            continue;
        }

        if( npc_owns_uid( who, record.item_uid ) ) {
            item_location owned = owned_item_with_uid( who, record.item_uid );
            if( record.wear_after_recovery && owned && !who.is_worn( *owned ) ) {
                const ret_val<void> can_wear = who.can_wear( *owned );
                if( !can_wear.success() ) {
                    record.retrieval_expected = false;
                    if( record.recovery_goal_id != 0 ) {
                        fail_goal( who, record.recovery_goal_id, can_wear.str() );
                        record.recovery_goal_id = 0;
                    }
                    persist_memory( who, record );
                    who.say( string_format( _( "I can't wear %s: %s" ), record.item_name,
                                            can_wear.str() ) );
                    continue;
                }
                who.assign_activity( wear_activity_actor( { owned }, { owned->count() } ) );
                who.do_player_activity();
                return true;
            }

            if( owned && who.is_worn( *owned ) ) {
                add_msg_debug( debugmode::DF_NPC_ITEMAI,
                               "%s ENGINE_PICKUP_MODE=STASH FINAL_DESTINATION=WORN TARGET_ITEM=%s",
                               who.get_name(), record.item_name );
            }

            record.status = equipment_memory_status::recovered;
            record.retrieval_expected = false;
            record.wear_after_recovery = false;
            if( record.recovery_goal_id != 0 ) {
                complete_goal( who, record.recovery_goal_id );
                record.recovery_goal_id = 0;
            }
            persist_memory( who, record );
            continue;
        }

        bool location_visible = false;
        item_location target = visible_item_with_uid( who, record, location_visible );
        if( !target ) {
            if( location_visible ) {
                record.status = equipment_memory_status::missing;
                record.retrieval_expected = false;
                if( record.recovery_goal_id != 0 ) {
                    fail_goal( who, record.recovery_goal_id, "equipment missing" );
                    record.recovery_goal_id = 0;
                }
                persist_memory( who, record );
                who.say( string_format( _( "My %s is no longer where I left it." ),
                                        record.item_name ) );
            }
            if( !location_visible ) {
                const tripoint_bub_ms remembered_position = get_map().get_bub( record.location );
                if( get_map().inbounds( remembered_position ) &&
                    who.update_path( remembered_position ) && !who.path.empty() ) {
                    who.move_to_next();
                    return true;
                }
            }
            continue;
        }

        const tripoint_bub_ms position = target.pos_bub( get_map() );
        if( record.wear_after_recovery ) {
            const ret_val<void> can_wear = who.can_wear( *target );
            if( !can_wear.success() ) {
                record.retrieval_expected = false;
                if( record.recovery_goal_id != 0 ) {
                    fail_goal( who, record.recovery_goal_id, can_wear.str() );
                    record.recovery_goal_id = 0;
                }
                persist_memory( who, record );
                who.say( string_format( _( "I can't wear %s: %s" ), record.item_name,
                                        can_wear.str() ) );
                continue;
            }
            if( rl_dist( who.pos_bub(), position ) > 1 ) {
                if( who.update_path( position ) && !who.path.empty() ) {
                    who.move_to_next();
                    return true;
                }
                continue;
            }
            who.assign_activity( wear_activity_actor( { target }, { target->count() } ) );
            who.do_player_activity();
            return true;
        }

        std::string error;
        if( !who.ai_request_pickup( target, position, error ) ) {
            record.retrieval_expected = false;
            if( record.recovery_goal_id != 0 ) {
                fail_goal( who, record.recovery_goal_id, error );
                record.recovery_goal_id = 0;
            }
            persist_memory( who, record );
            who.say( string_format( _( "I can't recover %s: %s" ), record.item_name, error ) );
            continue;
        }
        if( record.recovery_goal_id == 0 ) {
            record.recovery_goal_id = begin_goal( who, ai_goal_kind::recover_equipment,
                                                  ai_goal_priority::high,
                                                  "recover " + record.item_name );
        }
        record.status = equipment_memory_status::recovering;
        persist_memory( who, record );
        who.pick_up_item();
        return true;
    }
    return false;
}

void reset_all_equipment_memory_cache()
{
    // Only the in-memory mirror is dropped; the durable records live in the
    // NPC's own variables and are reloaded on demand for the new session.
    memories.clear();
    loaded_npcs.clear();
}

} // namespace npc_ai
