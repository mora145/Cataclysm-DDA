#include "cata_catch.h"

#include <string>

#include "avatar.h"
#include "calendar.h"
#include "field_type.h"
#include "faction.h"
#include "flag.h"
#include "game.h"
#include "item.h"
#include "map.h"
#include "map_helpers.h"
#include "npc.h"
#include "npc_ai_fire.h"
#include "player_helpers.h"
#include "point.h"
#include "type_id.h"

namespace
{

static const activity_id ACT_START_FIRE( "ACT_START_FIRE" );
static const faction_id faction_your_followers( "your_followers" );
static const furn_str_id furn_woodstove( "f_woodstove" );
static const itype_id itype_2x4( "2x4" );
static const itype_id itype_backpack( "backpack" );
static const itype_id itype_magnifying_glass( "magnifying_glass" );
static const itype_id itype_match( "match" );
static const itype_id itype_matches( "matches" );
static const itype_id itype_splinter( "splinter" );

void refresh_visibility( npc &observer )
{
    map &here = get_map();
    here.invalidate_map_cache( observer.posz() );
    here.build_map_cache( observer.posz(), true );
    here.invalidate_visibility_cache();
    here.update_visibility_cache( observer.posz() );
    observer.recalc_sight_limits();
}

npc &prepare_fire_tender()
{
    g->faction_manager_ptr->create_if_needed();
    clear_map( -1, 1 );
    clear_avatar();
    set_time_to_day();
    g->place_player( tripoint_bub_ms{ 60, 60, 0 } );

    npc &observer = spawn_npc( point_bub_ms{ 65, 60 }, "test_talker" );
    observer.name = "Fire tender";
    clear_character( observer );
    observer.worn.wear_item( observer, item( itype_backpack, calendar::turn ), false, false );
    observer.set_fac( faction_your_followers );
    observer.set_attitude( NPCATT_FOLLOW );
    npc_ai::cancel_start_fire_task( observer );
    refresh_visibility( observer );
    return observer;
}

tripoint_bub_ms stove_tile( const npc &observer )
{
    const tripoint_bub_ms origin = observer.pos_bub( get_map() );
    return tripoint_bub_ms{ origin.x() + 1, origin.y(), origin.z() };
}

void add_loaded_matches( npc &who )
{
    item matches( itype_matches, calendar::turn );
    matches.ammo_set( itype_match, 10 );
    REQUIRE( who.i_add( matches ) );
}

void add_firewood( const tripoint_bub_ms &where, const itype_id &fuel = itype_2x4 )
{
    get_map().add_item_or_charges( where, item( fuel, calendar::turn ) );
}

void run_fire_task( npc &who, const int limit = 100 )
{
    for( int turn = 0; turn < limit &&
         ( npc_ai::has_start_fire_task( who ) || who.has_player_activity() ); ++turn ) {
        who.set_moves( 1000 );
        if( who.has_player_activity() ) {
            who.do_player_activity();
        } else {
            npc_ai::process_start_fire_task( who );
        }
    }
}

} // namespace

TEST_CASE( "npc_ai_start_fire_command_requires_an_ignition_intent",
           "[npc_ai][npc_ai_fire]" )
{
    npc &who = prepare_fire_tender();
    map &here = get_map();
    here.furn_set( stove_tile( who ), furn_woodstove );
    refresh_visibility( who );

    const npc_ai::start_fire_command_result result =
        npc_ai::try_handle_start_fire_command( who, "Is the stove lit?" );

    CHECK_FALSE( result.handled );
    CHECK_FALSE( npc_ai::has_start_fire_task( who ) );
}

TEST_CASE( "npc_ai_start_fire_reports_an_already_burning_stove",
           "[npc_ai][npc_ai_fire]" )
{
    npc &who = prepare_fire_tender();
    map &here = get_map();
    const tripoint_bub_ms stove = stove_tile( who );
    here.furn_set( stove, furn_woodstove );
    REQUIRE( here.add_field( stove, fd_fire, 2 ) );
    refresh_visibility( who );

    const npc_ai::start_fire_command_result result =
        npc_ai::try_handle_start_fire_command( who, "Enciende la cocina." );

    CHECK( result.handled );
    CHECK( result.success );
    CHECK_FALSE( result.started );
    CHECK_FALSE( npc_ai::has_start_fire_task( who ) );
    CHECK( here.get_field_intensity( stove, fd_fire ) == 2 );
}

TEST_CASE( "npc_ai_start_fire_requires_real_fuel_and_a_usable_firestarter",
           "[npc_ai][npc_ai_fire]" )
{
    npc &who = prepare_fire_tender();
    map &here = get_map();
    const tripoint_bub_ms stove = stove_tile( who );
    here.furn_set( stove, furn_woodstove );
    refresh_visibility( who );

    SECTION( "fuel is present but no firestarter is available" ) {
        add_firewood( stove );
        const npc_ai::start_fire_command_result result =
            npc_ai::try_handle_start_fire_command( who, "Prende la cocina de lena." );

        CHECK( result.handled );
        CHECK_FALSE( result.started );
        CHECK_FALSE( here.get_field( stove, fd_fire ) );
    }

    SECTION( "firestarter is present but no fuel can be found" ) {
        add_loaded_matches( who );
        const npc_ai::start_fire_command_result result =
            npc_ai::try_handle_start_fire_command( who, "Haz fuego." );

        CHECK( result.handled );
        CHECK_FALSE( result.started );
        CHECK_FALSE( here.get_field( stove, fd_fire ) );
    }

    SECTION( "an empty firestarter cannot start the task" ) {
        add_firewood( stove );
        item empty_matches( itype_matches, calendar::turn );
        empty_matches.ammo_unset();
        REQUIRE( who.i_add( empty_matches ) );

        const npc_ai::start_fire_command_result result =
            npc_ai::try_handle_start_fire_command( who, "Enciende la cocina." );

        CHECK( result.handled );
        CHECK_FALSE( result.started );
        CHECK_FALSE( here.get_field( stove, fd_fire ) );
    }
}

TEST_CASE( "npc_ai_start_fire_physically_ignites_existing_stove_fuel",
           "[npc_ai][npc_ai_fire]" )
{
    npc &who = prepare_fire_tender();
    map &here = get_map();
    const tripoint_bub_ms stove = stove_tile( who );
    here.furn_set( stove, furn_woodstove );
    add_firewood( stove );
    add_loaded_matches( who );
    refresh_visibility( who );

    const npc_ai::start_fire_command_result result =
        npc_ai::try_handle_start_fire_command( who, "Enciende la cocina de lena." );

    REQUIRE( result.handled );
    REQUIRE( result.started );
    CHECK( npc_ai::has_start_fire_task( who ) );

    run_fire_task( who );

    CHECK_FALSE( npc_ai::has_start_fire_task( who ) );
    CHECK_FALSE( who.activity.id() == ACT_START_FIRE );
    CHECK( here.get_field_intensity( stove, fd_fire ) > 0 );
}

TEST_CASE( "npc_ai_start_fire_retrieves_visible_firewood_before_ignition",
           "[npc_ai][npc_ai_fire]" )
{
    npc &who = prepare_fire_tender();
    map &here = get_map();
    const tripoint_bub_ms stove = stove_tile( who );
    const tripoint_bub_ms origin = who.pos_bub( here );
    const tripoint_bub_ms fuel_tile{ origin.x(), origin.y() + 1, origin.z() };
    here.furn_set( stove, furn_woodstove );
    add_firewood( fuel_tile, itype_splinter );
    add_loaded_matches( who );
    refresh_visibility( who );

    REQUIRE_FALSE( here.i_at( fuel_tile ).empty() );
    item &fuel = *here.i_at( fuel_tile ).begin();
    REQUIRE( fuel.has_flag( flag_FIREWOOD ) );
    REQUIRE( who.sees( here, fuel_tile ) );
    REQUIRE( here.could_see_items( fuel_tile, who ) );
    CAPTURE( who.weight_carried(), who.weight_capacity() );
    REQUIRE( who.can_take_that( fuel ) );
    REQUIRE( who.would_take_that( fuel, fuel_tile ) );

    const npc_ai::start_fire_command_result result =
        npc_ai::try_handle_start_fire_command( who, "Pon a funcionar la cocina de lena." );

    CAPTURE( result.message );
    REQUIRE( result.handled );
    REQUIRE( result.started );
    REQUIRE( npc_ai::has_start_fire_task( who ) );
    REQUIRE( who.fetching_item );

    who.set_moves( 1000 );
    who.pick_up_item();
    run_fire_task( who );

    CHECK_FALSE( npc_ai::has_start_fire_task( who ) );
    CHECK( here.get_field_intensity( stove, fd_fire ) > 0 );
}

TEST_CASE( "npc_ai_start_fire_uses_the_vanilla_long_fire_activity",
           "[npc_ai][npc_ai_fire]" )
{
    npc &who = prepare_fire_tender();
    map &here = get_map();
    const tripoint_bub_ms stove = stove_tile( who );
    here.furn_set( stove, furn_woodstove );
    add_firewood( stove );
    REQUIRE( who.i_add( item( itype_magnifying_glass, calendar::turn ) ) );
    refresh_visibility( who );

    const npc_ai::start_fire_command_result result =
        npc_ai::try_handle_start_fire_command( who, "Enciende la cocina." );

    REQUIRE( result.started );
    REQUIRE( npc_ai::process_start_fire_task( who ) );
    REQUIRE( who.activity.id() == ACT_START_FIRE );

    run_fire_task( who, 400 );

    CHECK_FALSE( npc_ai::has_start_fire_task( who ) );
    CHECK( here.get_field_intensity( stove, fd_fire ) > 0 );
}
