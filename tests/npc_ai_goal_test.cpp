#include "cata_catch.h"

#include "map_helpers.h"
#include "npc.h"
#include "npc_ai_goal.h"
#include "player_helpers.h"
#include "point.h"

namespace
{

npc &prepare_goal_npc( const point_bub_ms &position )
{
    npc &who = spawn_npc( position, "test_talker" );
    clear_character( who );
    npc_ai::clear_goals_for_test( who );
    return who;
}

} // namespace

TEST_CASE( "npc_ai_goal_stack_interrupts_and_resumes_by_priority",
           "[npc_ai][npc_ai_goal]" )
{
    clear_map();
    npc &who = prepare_goal_npc( point_bub_ms{ 65, 60 } );

    const npc_ai::ai_goal_id main_goal = npc_ai::begin_goal(
            who, npc_ai::ai_goal_kind::light_stove, npc_ai::ai_goal_priority::normal,
            "light stove" );
    REQUIRE( npc_ai::active_goal( who ) );
    CHECK( npc_ai::active_goal( who )->id == main_goal );

    const npc_ai::ai_goal_id low_goal = npc_ai::begin_goal(
            who, npc_ai::ai_goal_kind::generic, npc_ai::ai_goal_priority::low,
            "tidy later" );
    CHECK( npc_ai::active_goal( who )->id == main_goal );

    const npc_ai::ai_goal_id emergency = npc_ai::begin_goal(
            who, npc_ai::ai_goal_kind::drop_equipment, npc_ai::ai_goal_priority::emergency,
            "drop backpack" );
    REQUIRE( npc_ai::active_goal( who ) );
    CHECK( npc_ai::active_goal( who )->id == emergency );

    REQUIRE( npc_ai::complete_goal( who, emergency ) );
    REQUIRE( npc_ai::active_goal( who ) );
    CHECK( npc_ai::active_goal( who )->id == main_goal );

    REQUIRE( npc_ai::complete_goal( who, main_goal ) );
    REQUIRE( npc_ai::active_goal( who ) );
    CHECK( npc_ai::active_goal( who )->id == low_goal );
    REQUIRE( npc_ai::fail_goal( who, low_goal, "not needed" ) );
    CHECK_FALSE( npc_ai::active_goal( who ) );
}

TEST_CASE( "npc_ai_goal_state_is_independent_per_npc",
           "[npc_ai][npc_ai_goal][npc_ai_multi_npc]" )
{
    clear_map();
    npc &first = prepare_goal_npc( point_bub_ms{ 64, 60 } );
    npc &second = prepare_goal_npc( point_bub_ms{ 66, 60 } );

    const npc_ai::ai_goal_id first_goal = npc_ai::begin_goal(
            first, npc_ai::ai_goal_kind::light_stove, npc_ai::ai_goal_priority::normal,
            "first goal" );
    const npc_ai::ai_goal_id second_goal = npc_ai::begin_goal(
            second, npc_ai::ai_goal_kind::batch_pickup, npc_ai::ai_goal_priority::normal,
            "second goal" );

    REQUIRE( npc_ai::active_goal( first ) );
    REQUIRE( npc_ai::active_goal( second ) );
    CHECK( npc_ai::active_goal( first )->id == first_goal );
    CHECK( npc_ai::active_goal( second )->id == second_goal );

    REQUIRE( npc_ai::complete_goal( first, first_goal ) );
    CHECK_FALSE( npc_ai::active_goal( first ) );
    REQUIRE( npc_ai::active_goal( second ) );
    CHECK( npc_ai::active_goal( second )->id == second_goal );
}
