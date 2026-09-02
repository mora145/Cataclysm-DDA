#include "cata_catch.h"

#include <algorithm>

#include "avatar.h"
#include "climbing.h"
#include "faction.h"
#include "game.h"
#include "item.h"
#include "map.h"
#include "map_helpers.h"
#include "npc.h"
#include "npc_ai_climbing.h"
#include "player_helpers.h"
#include "point.h"
#include "type_id.h"

namespace
{

static const faction_id faction_your_followers( "your_followers" );
static const ter_str_id ter_downspout( "t_gutter_downspout" );
static const ter_str_id ter_dirt( "t_dirt" );
static const ter_str_id ter_flat_roof( "t_flat_roof" );
static const ter_str_id ter_gutter_drop( "t_gutter_drop" );
static const ter_str_id ter_open_air( "t_open_air" );
static const ter_str_id ter_wall( "t_wall" );

struct climbing_scene {
    npc *follower;
    tripoint_bub_ms base;
    tripoint_bub_ms aid;
    tripoint_bub_ms landing;
    tripoint_bub_ms player_position;
};

struct gutter_descent_scene {
    npc *follower;
    tripoint_bub_ms upper_roof;
    tripoint_bub_ms upper_gutter;
    tripoint_bub_ms upper_ledge;
    tripoint_bub_ms lower_downspout;
    tripoint_bub_ms lower_player;
};

void rebuild_vertical_caches()
{
    map &here = get_map();
    for( int z = -1; z <= 1; ++z ) {
        here.invalidate_map_cache( z );
        here.build_map_cache( z, true );
    }
    here.invalidate_visibility_cache();
    here.update_visibility_cache( 0 );
}

climbing_scene prepare_climbing_scene()
{
    g->faction_manager_ptr->create_if_needed();
    clear_map( -1, 1 );
    clear_avatar();
    set_time_to_day();

    map &here = get_map();
    const tripoint_bub_ms base{ 65, 65, 0 };
    const tripoint_bub_ms aid{ 66, 65, 0 };
    const tripoint_bub_ms landing{ 66, 65, 1 };
    const tripoint_bub_ms player_position{ 69, 65, 1 };
    here.ter_set( aid, ter_downspout );
    here.ter_set( landing, ter_gutter_drop );
    for( int x = landing.x() + 1; x <= player_position.x(); ++x ) {
        here.ter_set( tripoint_bub_ms{ x, landing.y(), landing.z() }, ter_flat_roof );
    }
    g->place_player( player_position );

    npc &follower = spawn_npc( base.xy(), "test_talker" );
    follower.name = "Climbing follower";
    follower.setpos( here, base );
    follower.remove_weapon();
    follower.clear_effects();
    follower.str_max = 20;
    follower.dex_max = 20;
    follower.reset_stats();
    follower.set_fac( faction_your_followers );
    follower.set_attitude( NPCATT_FOLLOW );
    follower.set_stamina( follower.get_stamina_max() );
    rebuild_vertical_caches();

    REQUIRE( follower.is_player_ally() );
    REQUIRE( follower.is_following() );
    REQUIRE_FALSE( follower.get_wielded_item() );
    REQUIRE( follower.move_effects( false ) );
    return { &follower, base, aid, landing, player_position };
}

gutter_descent_scene prepare_real_gutter_descent_scene()
{
    g->faction_manager_ptr->create_if_needed();
    clear_map( -1, 1 );
    clear_avatar();
    set_time_to_day();

    map &here = get_map();
    // This is the vanilla roof_palette geometry used by house_45: the roof-side
    // gutter drop is beside open air, with the downspout below that open-air tile.
    const tripoint_bub_ms upper_roof{ 65, 65, 1 };
    const tripoint_bub_ms upper_gutter{ 66, 65, 1 };
    const tripoint_bub_ms upper_ledge{ 67, 65, 1 };
    const tripoint_bub_ms lower_downspout{ 67, 65, 0 };
    const tripoint_bub_ms lower_player{ 70, 65, 0 };

    here.ter_set( upper_roof, ter_flat_roof );
    here.ter_set( upper_gutter, ter_gutter_drop );
    here.ter_set( upper_ledge, ter_open_air );
    here.ter_set( upper_gutter + tripoint::below, ter_wall );
    here.ter_set( lower_downspout, ter_downspout );
    for( int x = lower_downspout.x() + 1; x <= lower_player.x(); ++x ) {
        here.ter_set( tripoint_bub_ms{ x, lower_player.y(), lower_player.z() }, ter_dirt );
    }

    g->place_player( upper_gutter );
    npc &follower = spawn_npc( upper_roof.xy(), "test_talker" );
    follower.name = "Descending follower";
    follower.setpos( here, upper_roof );
    follower.remove_weapon();
    follower.clear_effects();
    follower.str_max = 20;
    follower.dex_max = 20;
    follower.reset_stats();
    follower.set_fac( faction_your_followers );
    follower.set_attitude( NPCATT_FOLLOW );
    follower.set_stamina( follower.get_stamina_max() );
    rebuild_vertical_caches();

    REQUIRE( follower.is_player_ally() );
    REQUIRE( follower.is_following() );
    return { &follower, upper_roof, upper_gutter, upper_ledge, lower_downspout, lower_player };
}

void complete_player_gutter_descent( const gutter_descent_scene &scene )
{
    map &here = get_map();
    avatar &player = get_avatar();
    REQUIRE( player.pos_bub() == scene.upper_gutter );
    REQUIRE( here.is_open_air( scene.upper_ledge ) );
    REQUIRE( here.valid_move( scene.upper_gutter, scene.upper_ledge, false, true ) );

    const climbing_aid::fall_scan fall( scene.upper_ledge );
    REQUIRE( fall.height == 1 );
    REQUIRE( fall.pos_bottom() == scene.lower_downspout );
    REQUIRE( here.has_flag( ter_furn_flag::TFLAG_CLIMBABLE, fall.pos_furniture_or_floor() ) );

    // climb_down_using ultimately performs this vertical placement after validating
    // the ledge and detected CLIMBABLE aid.  Keep UI confirmation out of this test.
    player.setpos( here, scene.lower_downspout );
    REQUIRE( player.pos_bub() == scene.lower_downspout );
    player.setpos( here, scene.lower_player );
    rebuild_vertical_caches();
}

} // namespace

TEST_CASE( "npc_follower_routes_and_physically_climbs_a_downspout",
           "[npc_ai][npc_ai_climbing]" )
{
    climbing_scene scene = prepare_climbing_scene();
    npc &follower = *scene.follower;
    map &here = get_map();

    REQUIRE( follower.update_path( scene.player_position ) );
    REQUIRE_FALSE( follower.path.empty() );
    CAPTURE( follower.pos_bub(), follower.path );

    tripoint_bub_ms previous = follower.pos_bub();
    bool found_climb_edge = false;
    for( const tripoint_bub_ms &step : follower.path ) {
        if( step.z() != previous.z() ) {
            found_climb_edge = true;
            CHECK( previous == scene.base );
            CHECK( step == scene.landing );
            CHECK( step.z() - previous.z() == 1 );
            CHECK( std::abs( step.x() - previous.x() ) <= 1 );
            CHECK( std::abs( step.y() - previous.y() ) <= 1 );
        }
        previous = step;
    }
    REQUIRE( found_climb_edge );

    const npc_ai::climb_assessment assessment =
        npc_ai::assess_climb( follower, scene.base, scene.landing );
    REQUIRE( assessment.vertical_climb );
    REQUIRE( assessment.possible );
    CHECK( assessment.move_cost > follower.get_speed() );

    follower.set_moves( assessment.move_cost + 100 );
    const int moves_before = follower.get_moves();
    const int stamina_before = follower.get_stamina();
    follower.move_to_next();
    CAPTURE( follower.pos_bub(), follower.path, follower.get_moves(), assessment.move_cost,
             assessment.slip_chance );

    CHECK( follower.pos_bub() == scene.landing );
    CHECK( follower.get_moves() == moves_before - assessment.move_cost );
    CHECK( follower.get_stamina() < stamina_before );
    CHECK( here.has_floor_or_water( follower.pos_bub() ) );
}

TEST_CASE( "npc_follower_climbs_down_a_gutter_without_teleporting",
           "[npc_ai][npc_ai_climbing]" )
{
    gutter_descent_scene scene = prepare_real_gutter_descent_scene();
    npc &follower = *scene.follower;
    map &here = get_map();
    complete_player_gutter_descent( scene );

    REQUIRE( follower.update_path( scene.lower_player ) );
    REQUIRE_FALSE( follower.path.empty() );

    tripoint_bub_ms previous = follower.pos_bub();
    bool found_descent = false;
    for( const tripoint_bub_ms &step : follower.path ) {
        if( step.z() != previous.z() ) {
            found_descent = true;
            CHECK( previous == scene.upper_gutter );
            CHECK( step == scene.lower_downspout );
            CHECK( step.z() == previous.z() - 1 );
        }
        previous = step;
    }
    REQUIRE( found_descent );

    while( follower.pos_bub() != scene.upper_gutter ) {
        REQUIRE_FALSE( follower.path.empty() );
        follower.set_moves( 1000 );
        follower.move_to_next();
    }

    const npc_ai::climb_assessment assessment =
        npc_ai::assess_climb( follower, scene.upper_gutter, scene.lower_downspout );
    REQUIRE( assessment.vertical_climb );
    REQUIRE( assessment.possible );

    follower.set_moves( assessment.move_cost + 100 );
    const int moves_before = follower.get_moves();
    const int stamina_before = follower.get_stamina();
    follower.move_to_next();

    CHECK( follower.pos_bub() == scene.lower_downspout );
    CHECK( follower.posz() == scene.upper_gutter.z() - 1 );
    CHECK( follower.get_moves() == moves_before - assessment.move_cost );
    CHECK( follower.get_stamina() < stamina_before );
    CHECK( here.has_floor_or_water( follower.pos_bub() ) );

    const int distance_before = rl_dist( follower.pos_bub(), scene.lower_player );
    REQUIRE( follower.update_path( scene.lower_player ) );
    follower.set_moves( 1000 );
    follower.move_to_next();
    CHECK( follower.posz() == scene.lower_player.z() );
    CHECK( rl_dist( follower.pos_bub(), scene.lower_player ) < distance_before );
}

TEST_CASE( "npc_follower_gutter_descent_rejects_unsafe_or_impossible_conditions",
           "[npc_ai][npc_ai_climbing]" )
{
    gutter_descent_scene scene = prepare_real_gutter_descent_scene();
    npc &follower = *scene.follower;
    map &here = get_map();
    follower.setpos( here, scene.upper_gutter );
    rebuild_vertical_caches();

    SECTION( "dangerous landing" ) {
        here.add_field( scene.lower_downspout, field_type_id( "fd_fire" ), 2 );
        const npc_ai::climb_assessment assessment =
            npc_ai::assess_climb( follower, scene.upper_gutter, scene.lower_downspout );
        REQUIRE( assessment.vertical_climb );
        CHECK_FALSE( assessment.possible );
        CHECK( assessment.refusal == npc_ai::climb_refusal::blocked_landing );
    }

    SECTION( "exhausted" ) {
        follower.set_stamina( follower.get_stamina_max() / 10 );
        const npc_ai::climb_assessment assessment =
            npc_ai::assess_climb( follower, scene.upper_gutter, scene.lower_downspout );
        REQUIRE( assessment.vertical_climb );
        CHECK_FALSE( assessment.possible );
        CHECK( assessment.refusal == npc_ai::climb_refusal::exhausted );
    }

    SECTION( "overloaded" ) {
        follower.str_max = 1;
        follower.reset_stats();
        REQUIRE( follower.i_add( item( itype_id( "anvil" ), calendar::turn ) ) );
        CAPTURE( follower.weight_carried(), follower.weight_capacity() );
        REQUIRE( follower.weight_carried() > follower.weight_capacity() );
        const npc_ai::climb_assessment assessment =
            npc_ai::assess_climb( follower, scene.upper_gutter, scene.lower_downspout );
        REQUIRE( assessment.vertical_climb );
        CHECK_FALSE( assessment.possible );
        CHECK( assessment.refusal == npc_ai::climb_refusal::overloaded );
    }
}

TEST_CASE( "npc_follower_refuses_an_exhausted_climb_without_moving",
           "[npc_ai][npc_ai_climbing]" )
{
    climbing_scene scene = prepare_climbing_scene();
    npc &follower = *scene.follower;
    follower.mod_stamina( -( follower.get_stamina() - follower.get_stamina_max() / 10 ) );
    CAPTURE( follower.get_stamina(), follower.get_stamina_max() );
    REQUIRE( follower.get_stamina() < follower.get_stamina_max() / 4 );

    const npc_ai::climb_assessment assessment =
        npc_ai::assess_climb( follower, scene.base, scene.landing );
    REQUIRE( assessment.vertical_climb );
    CHECK_FALSE( assessment.possible );
    CHECK( assessment.refusal == npc_ai::climb_refusal::exhausted );

    follower.set_moves( 1000 );
    const tripoint_bub_ms before = follower.pos_bub();
    const npc_ai::climb_attempt_result result =
        npc_ai::attempt_climb( follower, scene.base, scene.landing );

    CHECK( result == npc_ai::climb_attempt_result::refused );
    CHECK( follower.pos_bub() == before );
    CHECK( follower.get_moves() == 1000 );
}
