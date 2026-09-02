#include "cata_catch.h"

#include "calendar.h"
#include "faction.h"
#include "field_type.h"
#include "game.h"
#include "item.h"
#include "item_group.h"
#include "map.h"
#include "map_helpers.h"
#include "npc.h"
#include "npc_ai_fire.h"
#include "npc_ai_goal.h"
#include "npc_ai_profiler.h"
#include "npc_ai_survival.h"
#include "player_helpers.h"
#include "point.h"
#include "type_id.h"
#include "weather.h"

namespace
{

static const faction_id faction_your_followers( "your_followers" );
static const furn_str_id furn_woodstove( "f_woodstove" );
static const item_group_id group_bottle_water( "test_bottle_water" );
static const itype_id itype_2x4( "2x4" );
static const itype_id itype_backpack( "backpack" );
static const itype_id itype_match( "match" );
static const itype_id itype_matches( "matches" );
static const itype_id itype_sandwich( "sandwich_cheese_grilled" );

npc &prepare_survivor()
{
    g->faction_manager_ptr->create_if_needed();
    clear_map( -1, 1 );
    clear_avatar();
    set_time_to_day();
    g->place_player( tripoint_bub_ms{ 60, 60, 0 } );
    npc &who = spawn_npc( point_bub_ms{ 65, 60 }, "test_talker" );
    clear_character( who );
    who.worn.wear_item( who, item( itype_backpack, calendar::turn ), false, false );
    who.set_fac( faction_your_followers );
    who.set_attitude( NPCATT_FOLLOW );
    npc_ai::cancel_start_fire_task( who );
    npc_ai::clear_goals_for_test( who );
    npc_ai::clear_survival_state_for_test( who );
    map &here = get_map();
    here.invalidate_map_cache( who.posz() );
    here.build_map_cache( who.posz(), true );
    here.invalidate_visibility_cache();
    here.update_visibility_cache( who.posz() );
    who.recalc_sight_limits();
    who.regen_ai_cache();
    return who;
}

void add_loaded_matches( npc &who )
{
    item matches( itype_matches, calendar::turn );
    matches.ammo_set( itype_match, 10 );
    REQUIRE( who.i_add( matches ) );
}

} // namespace

TEST_CASE( "npc_ai_survival_reuses_vanilla_food_and_drink_consumption",
           "[npc_ai][npc_ai_survival]" )
{
    SECTION( "hungry NPC consumes genuinely edible owned food" ) {
        npc &who = prepare_survivor();
        who.set_hunger( 500 );
        REQUIRE( who.i_add( item( itype_sandwich, calendar::turn ) ) );
        REQUIRE( who.consume_food() );
        CHECK_FALSE( who.has_amount( itype_sandwich, 1 ) );
    }

    SECTION( "thirsty NPC consumes potable owned drink" ) {
        npc &who = prepare_survivor();
        who.set_thirst( 700 );
        const item_group::ItemList water = item_group::items_from( group_bottle_water );
        REQUIRE( water.size() == 1 );
        REQUIRE( who.i_add( water.front() ) );
        REQUIRE( who.consume_food() );
        CHECK_FALSE( who.has_charges( itype_id( "water_clean" ), 1 ) );
    }
}

TEST_CASE( "npc_ai_survival_severe_cold_starts_physical_stove_goal",
           "[npc_ai][npc_ai_survival]" )
{
    npc &who = prepare_survivor();
    map &here = get_map();
    const tripoint_bub_ms origin = who.pos_bub( here );
    const tripoint_bub_ms stove{ origin.x() + 1, origin.y(), origin.z() };
    here.furn_set( stove, furn_woodstove );
    here.add_item_or_charges( stove, item( itype_2x4, calendar::turn ) );
    add_loaded_matches( who );
    who.set_part_temp_cur( bodypart_id( "torso" ), BODYTEMP_VERY_COLD - 1_C_delta );

    const npc_ai::basic_survival_result result = npc_ai::consider_basic_survival( who );
    REQUIRE( result == npc_ai::basic_survival_result::stove_task_started );
    REQUIRE( npc_ai::has_start_fire_task( who ) );
    const std::optional<npc_ai::ai_goal> goal = npc_ai::active_goal( who );
    REQUIRE( goal );
    CHECK( goal->kind == npc_ai::ai_goal_kind::light_stove );

    for( int turn = 0; turn < 30 &&
         ( npc_ai::has_start_fire_task( who ) || who.has_player_activity() ); ++turn ) {
        who.set_moves( 1000 );
        if( who.has_player_activity() ) {
            who.do_player_activity();
        } else {
            npc_ai::process_start_fire_task( who );
        }
    }
    CHECK( here.get_field( stove, fd_fire ) != nullptr );
    CHECK_FALSE( npc_ai::has_start_fire_task( who ) );
    CHECK_FALSE( npc_ai::active_goal( who ) );
}

TEST_CASE( "npc_ai_survival_does_not_light_stove_without_severe_cold",
           "[npc_ai][npc_ai_survival]" )
{
    npc &who = prepare_survivor();
    map &here = get_map();
    const tripoint_bub_ms origin = who.pos_bub( here );
    const tripoint_bub_ms stove{ origin.x() + 1, origin.y(), origin.z() };
    here.furn_set( stove, furn_woodstove );
    here.add_item_or_charges( stove, item( itype_2x4, calendar::turn ) );
    add_loaded_matches( who );
    who.set_part_temp_cur( bodypart_id( "torso" ), BODYTEMP_NORM );

    CHECK( npc_ai::consider_basic_survival( who ) ==
           npc_ai::basic_survival_result::no_action );
    CHECK_FALSE( npc_ai::has_start_fire_task( who ) );
    CHECK( here.get_field( stove, fd_fire ) == nullptr );
}

namespace
{

std::uint64_t perception_calls()
{
    return npc_ai::profile_snapshot()[static_cast<std::size_t>(
                                          npc_ai::profile_subsystem::perception )].calls;
}

void make_severely_cold( npc &who )
{
    who.set_part_temp_cur( bodypart_id( "torso" ), BODYTEMP_VERY_COLD - 1_C_delta );
}

} // namespace

TEST_CASE( "npc_ai_survival_first_severe_cold_transition_scans_immediately",
           "[npc_ai][npc_ai_survival]" )
{
    npc &who = prepare_survivor();
    add_loaded_matches( who );
    npc_ai::reset_all_survival_state();

    npc_ai::set_profiling_enabled_for_test( true );
    npc_ai::reset_profile();
    REQUIRE( npc_ai::consider_basic_survival( who ) ==
             npc_ai::basic_survival_result::no_action );
    REQUIRE( perception_calls() == 0 );
    REQUIRE( npc_ai::warmth_environment_evaluations_for_test() == 0 );

    make_severely_cold( who );
    CHECK( npc_ai::consider_basic_survival( who ) ==
           npc_ai::basic_survival_result::no_action );
    CHECK( perception_calls() == 1 );
    CHECK( npc_ai::warmth_environment_evaluations_for_test() == 1 );
    npc_ai::set_profiling_enabled_for_test( false );
}

TEST_CASE( "npc_ai_survival_visible_fire_rate_limits_environment_scan",
           "[npc_ai][npc_ai_survival]" )
{
    npc &who = prepare_survivor();
    map &here = get_map();
    const tripoint_bub_ms fire_pos = who.pos_bub( here ) + point_rel_ms{ 1, 0 };
    REQUIRE( here.add_field( fire_pos, fd_fire, 1 ) );
    make_severely_cold( who );
    here.invalidate_visibility_cache();
    here.update_visibility_cache( who.posz() );
    who.recalc_sight_limits();

    npc_ai::set_profiling_enabled_for_test( true );
    npc_ai::reset_profile();
    CHECK( npc_ai::consider_basic_survival( who ) ==
           npc_ai::basic_survival_result::warmth_available );
    CHECK( perception_calls() == 1 );
    CHECK( npc_ai::consider_basic_survival( who ) ==
           npc_ai::basic_survival_result::no_action );
    CHECK( perception_calls() == 1 );
    npc_ai::set_profiling_enabled_for_test( false );
}

TEST_CASE( "npc_ai_survival_missing_stove_or_firestarter_rate_limits_scan",
           "[npc_ai][npc_ai_survival]" )
{
    SECTION( "no visible stove" ) {
        npc &who = prepare_survivor();
        add_loaded_matches( who );
        make_severely_cold( who );
        npc_ai::set_profiling_enabled_for_test( true );
        npc_ai::reset_profile();
        CHECK( npc_ai::consider_basic_survival( who ) ==
               npc_ai::basic_survival_result::no_action );
        CHECK( perception_calls() == 1 );
        CHECK( npc_ai::consider_basic_survival( who ) ==
               npc_ai::basic_survival_result::no_action );
        CHECK( perception_calls() == 1 );
        npc_ai::set_profiling_enabled_for_test( false );
    }

    SECTION( "stove without firestarter" ) {
        npc &who = prepare_survivor();
        map &here = get_map();
        const tripoint_bub_ms stove = who.pos_bub( here ) + point_rel_ms{ 1, 0 };
        here.furn_set( stove, furn_woodstove );
        here.add_item_or_charges( stove, item( itype_2x4, calendar::turn ) );
        make_severely_cold( who );
        here.invalidate_visibility_cache();
        here.update_visibility_cache( who.posz() );
        who.recalc_sight_limits();
        npc_ai::set_profiling_enabled_for_test( true );
        npc_ai::reset_profile();
        CHECK( npc_ai::consider_basic_survival( who ) ==
               npc_ai::basic_survival_result::no_action );
        CHECK( perception_calls() == 1 );
        CHECK( npc_ai::consider_basic_survival( who ) ==
               npc_ai::basic_survival_result::no_action );
        CHECK( perception_calls() == 1 );
        npc_ai::set_profiling_enabled_for_test( false );
    }
}

TEST_CASE( "npc_ai_survival_cold_ally_does_not_rescan_every_turn",
           "[npc_ai][npc_ai_survival]" )
{
    npc &who = prepare_survivor();
    add_loaded_matches( who );
    make_severely_cold( who );
    npc_ai::reset_all_survival_state();

    npc_ai::set_profiling_enabled_for_test( true );
    npc_ai::reset_profile();
    REQUIRE( npc_ai::consider_basic_survival( who ) ==
             npc_ai::basic_survival_result::no_action );
    REQUIRE( perception_calls() == 1 );

    for( int turn = 0; turn < 600; ++turn ) {
        calendar::turn += 1_seconds;
        make_severely_cold( who );
        CHECK( npc_ai::consider_basic_survival( who ) ==
               npc_ai::basic_survival_result::no_action );
    }

    // One inspect per game minute after the first immediate detection.
    CHECK( perception_calls() == 11 );
    CHECK( npc_ai::warmth_environment_evaluations_for_test() == 11 );
    npc_ai::set_profiling_enabled_for_test( false );
}

TEST_CASE( "npc_ai_survival_cold_warm_cold_scans_immediately_again",
           "[npc_ai][npc_ai_survival]" )
{
    npc &who = prepare_survivor();
    add_loaded_matches( who );
    make_severely_cold( who );
    npc_ai::reset_all_survival_state();

    npc_ai::set_profiling_enabled_for_test( true );
    npc_ai::reset_profile();
    REQUIRE( npc_ai::consider_basic_survival( who ) ==
             npc_ai::basic_survival_result::no_action );
    REQUIRE( perception_calls() == 1 );
    REQUIRE( npc_ai::warmth_environment_evaluations_for_test() == 1 );

    who.set_part_temp_cur( bodypart_id( "torso" ), BODYTEMP_NORM );
    CHECK( npc_ai::consider_basic_survival( who ) ==
           npc_ai::basic_survival_result::no_action );
    CHECK( perception_calls() == 1 );
    CHECK( npc_ai::warmth_environment_evaluations_for_test() == 1 );

    make_severely_cold( who );
    CHECK( npc_ai::consider_basic_survival( who ) ==
           npc_ai::basic_survival_result::no_action );
    CHECK( perception_calls() == 2 );
    CHECK( npc_ai::warmth_environment_evaluations_for_test() == 2 );
    npc_ai::set_profiling_enabled_for_test( false );
}

TEST_CASE( "npc_ai_survival_cooldown_skips_expensive_snapshots",
           "[npc_ai][npc_ai_survival]" )
{
    npc &who = prepare_survivor();
    add_loaded_matches( who );
    make_severely_cold( who );
    npc_ai::reset_all_survival_state();

    npc_ai::set_profiling_enabled_for_test( true );
    npc_ai::reset_profile();
    const std::uint64_t before = npc_ai::warmth_environment_evaluations_for_test();
    REQUIRE( npc_ai::consider_basic_survival( who ) ==
             npc_ai::basic_survival_result::no_action );
    REQUIRE( npc_ai::warmth_environment_evaluations_for_test() == before + 1 );
    REQUIRE( perception_calls() == 1 );

    for( int turn = 0; turn < 59; ++turn ) {
        calendar::turn += 1_seconds;
        make_severely_cold( who );
        CHECK( npc_ai::consider_basic_survival( who ) ==
               npc_ai::basic_survival_result::no_action );
        CHECK( npc_ai::warmth_environment_evaluations_for_test() == before + 1 );
        CHECK( perception_calls() == 1 );
    }

    calendar::turn += 1_seconds;
    make_severely_cold( who );
    CHECK( npc_ai::consider_basic_survival( who ) ==
           npc_ai::basic_survival_result::no_action );
    CHECK( npc_ai::warmth_environment_evaluations_for_test() == before + 2 );
    CHECK( perception_calls() == 2 );
    npc_ai::set_profiling_enabled_for_test( false );
}

TEST_CASE( "npc_ai_survival_reset_clears_warmth_check",
           "[npc_ai][npc_ai_survival]" )
{
    npc &who = prepare_survivor();
    add_loaded_matches( who );
    make_severely_cold( who );
    npc_ai::reset_all_survival_state();

    npc_ai::set_profiling_enabled_for_test( true );
    npc_ai::reset_profile();
    REQUIRE( npc_ai::consider_basic_survival( who ) ==
             npc_ai::basic_survival_result::no_action );
    REQUIRE( perception_calls() == 1 );
    REQUIRE( npc_ai::warmth_environment_evaluations_for_test() == 1 );

    npc_ai::reset_all_survival_state();
    npc_ai::reset_profile();
    CHECK( npc_ai::consider_basic_survival( who ) ==
           npc_ai::basic_survival_result::no_action );
    CHECK( perception_calls() == 1 );
    CHECK( npc_ai::warmth_environment_evaluations_for_test() == 1 );
    npc_ai::set_profiling_enabled_for_test( false );
}
