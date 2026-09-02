#include "cata_catch.h"

#include <algorithm>
#include <vector>

#include "avatar.h"
#include "calendar.h"
#include "faction.h"
#include "game.h"
#include "map.h"
#include "map_helpers.h"
#include "monster.h"
#include "npc.h"
#include "npc_ai_event_stream.h"
#include "player_helpers.h"

namespace
{
static const faction_id faction_your_followers( "your_followers" );

npc &event_test_follower( const point &offset, const std::string &name )
{
    npc &who = spawn_npc( get_avatar().pos_bub().xy() + offset, "test_talker" );
    who.name = name;
    who.set_fac( faction_your_followers );
    who.set_attitude( NPCATT_FOLLOW );
    return who;
}
} // namespace

TEST_CASE( "npc_ai_event_stream_is_ordered_bounded_and_perception_scoped",
           "[npc_ai][npc_ai_event_stream]" )
{
    clear_map();
    clear_avatar();
    clear_npcs();
    set_time_to_day();
    g->faction_manager_ptr->create_if_needed();
    g->place_player( tripoint_bub_ms{ 60, 60, 0 } );
    npc &liam = event_test_follower( point::east, "Liam" );
    npc &kim = event_test_follower( point::south, "Kim" );
    npc_ai::reset_world_event_stream();

    npc_ai::world_event private_event;
    private_event.type = npc_ai::world_event_type::failed_escape;
    private_event.detail = "Liam intento liberarse y fallo.";
    private_event.actor = npc_ai::snapshot_entity( &liam );
    private_event.importance = 99;
    private_event.confirmed_outcome = true;
    private_event.known_by_npc_ids.push_back( liam.getID().get_value() );
    const std::uint64_t first = npc_ai::record_world_event( private_event );
    CHECK( first > 0 );
    CHECK( npc_ai::recent_world_events_for( liam ).size() == 1 );
    CHECK( npc_ai::recent_world_events_for( kim ).empty() );
    CHECK( npc_ai::build_recent_world_event_context( liam ).find( "CURRENT STATE always wins" ) !=
           std::string::npos );

    for( std::size_t index = 0; index < npc_ai::world_event_ring_capacity + 5; ++index ) {
        npc_ai::world_event event = private_event;
        event.detail = std::to_string( index );
        npc_ai::record_world_event( std::move( event ) );
    }
    CHECK( npc_ai::world_event_stream_size() == npc_ai::world_event_ring_capacity );
    CHECK( npc_ai::latest_world_event_sequence() ==
           first + npc_ai::world_event_ring_capacity + 5 );
    const auto recent = npc_ai::recent_world_events_for( liam, 0,
                        npc_ai::world_event_ring_capacity, 120 );
    REQUIRE_FALSE( recent.empty() );
    CHECK( recent.front().sequence_id < recent.back().sequence_id );
}

TEST_CASE( "npc_ai_real_monster_melee_records_grounded_hit_metadata_end_to_end",
           "[npc_ai][npc_ai_event_stream][npc_ai_combat_e2e]" )
{
    clear_map();
    clear_avatar();
    clear_npcs();
    set_time_to_day();
    g->faction_manager_ptr->create_if_needed();
    g->place_player( tripoint_bub_ms{ 60, 60, 0 } );
    npc &liam = event_test_follower( point::east, "Liam" );
    const tripoint_bub_ms liam_pos = liam.pos_bub( get_map() );
    monster &zombie = spawn_test_monster(
                          "mon_zombie", tripoint_bub_ms{ liam_pos.x() + 1, liam_pos.y(),
                                  liam_pos.z() } );
    npc_ai::reset_world_event_stream();

    REQUIRE( zombie.melee_attack( liam, 10000.0f ) );
    const std::vector<npc_ai::world_event> events =
        npc_ai::recent_world_events_for( liam, 0, 20, 20 );
    const auto hit = std::find_if( events.begin(), events.end(),
    []( const npc_ai::world_event &event ) {
        return event.type == npc_ai::world_event_type::npc_attack &&
               event.attack_mode == "melee" && event.damage > 0;
    } );
    REQUIRE( hit != events.end() );
    CHECK( hit->actor.name.find( "zombie" ) != std::string::npos );
    CHECK( hit->target.character_id == liam.getID().get_value() );
    CHECK_FALSE( hit->body_part.empty() );
    CHECK( hit->confirmed_outcome );
    CHECK( static_cast<int>( hit->claim_level ) >=
           static_cast<int>( npc_ai::world_event_claim_level::hit_confirmed ) );
}
