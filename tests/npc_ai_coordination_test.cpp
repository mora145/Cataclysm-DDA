#include "cata_catch.h"

#include "avatar.h"
#include "faction.h"
#include "game.h"
#include "map.h"
#include "map_helpers.h"
#include "npc.h"
#include "npc_ai_coordination.h"
#include "npc_ai_goal.h"
#include "player_helpers.h"
#include "point.h"
#include "type_id.h"

namespace
{

static const faction_id faction_your_followers( "your_followers" );
static const skill_id skill_firstaid( "firstaid" );
static const skill_id skill_mechanics( "mechanics" );

npc &spawn_coordination_follower( const point_bub_ms &position, const std::string &name )
{
    npc &who = spawn_npc( position, "test_talker" );
    who.name = name;
    who.set_fac( faction_your_followers );
    who.set_attitude( NPCATT_FOLLOW );
    who.clear_effects();
    who.set_stamina( who.get_stamina_max() );
    return who;
}

void prepare_coordination_map()
{
    g->faction_manager_ptr->create_if_needed();
    clear_map();
    clear_avatar();
    set_time_to_day();
    g->place_player( tripoint_bub_ms{ 60, 60, 0 } );
    npc_ai::clear_assignments_for_test();

    map &here = get_map();
    here.invalidate_map_cache( 0 );
    here.build_map_cache( 0, true );
    here.invalidate_visibility_cache();
    here.update_visibility_cache( 0 );
}

} // namespace

TEST_CASE( "npc_coordination_selects_the_best_real_specialist",
           "[npc_ai][npc_ai_coordination]" )
{
    prepare_coordination_map();
    npc &requester = spawn_coordination_follower( point_bub_ms{ 62, 60 }, "Requester" );
    npc &mechanic = spawn_coordination_follower( point_bub_ms{ 63, 60 }, "Mechanic" );
    npc &medic = spawn_coordination_follower( point_bub_ms{ 64, 60 }, "Medic" );
    mechanic.set_skill_level( skill_mechanics, 8 );
    medic.set_skill_level( skill_firstaid, 10 );

    map &here = get_map();
    here.invalidate_visibility_cache();
    here.update_visibility_cache( 0 );

    CHECK( npc_ai::select_best_helper( requester,
                                       npc_ai::cooperative_task_kind::mechanics ) == &mechanic );
    CHECK( npc_ai::select_best_helper( requester,
                                       npc_ai::cooperative_task_kind::medical ) == &medic );
}

TEST_CASE( "npc_coordination_respects_existing_high_priority_work",
           "[npc_ai][npc_ai_coordination]" )
{
    prepare_coordination_map();
    npc &requester = spawn_coordination_follower( point_bub_ms{ 62, 60 }, "Requester" );
    npc &busy_mechanic = spawn_coordination_follower( point_bub_ms{ 63, 60 }, "Busy mechanic" );
    npc &available = spawn_coordination_follower( point_bub_ms{ 64, 60 }, "Available" );
    busy_mechanic.set_skill_level( skill_mechanics, 10 );
    available.set_skill_level( skill_mechanics, 3 );
    npc_ai::begin_goal( busy_mechanic, npc_ai::ai_goal_kind::generic,
                        npc_ai::ai_goal_priority::high, "urgent existing work" );

    CHECK( npc_ai::select_best_helper( requester,
                                       npc_ai::cooperative_task_kind::mechanics ) == &available );
    npc_ai::clear_goals_for_test( busy_mechanic );
}

TEST_CASE( "npc_coordination_creates_independent_structured_assignments",
           "[npc_ai][npc_ai_coordination]" )
{
    prepare_coordination_map();
    npc &requester = spawn_coordination_follower( point_bub_ms{ 62, 60 }, "Requester" );
    npc &helper = spawn_coordination_follower( point_bub_ms{ 63, 60 }, "Helper" );
    const npc_ai::ai_goal_id parent = npc_ai::begin_goal(
            requester, npc_ai::ai_goal_kind::unload_vehicle,
            npc_ai::ai_goal_priority::normal, "unload vehicle" );

    const std::optional<npc_ai::delegated_assignment> assignment = npc_ai::delegate_to_helper(
                requester, helper, npc_ai::cooperative_task_kind::heavy_transport,
                parent, "help carry vehicle cargo" );

    REQUIRE( assignment );
    CHECK( assignment->requester_id == requester.getID().get_value() );
    CHECK( assignment->helper_id == helper.getID().get_value() );
    CHECK( assignment->requester_goal == parent );
    REQUIRE( npc_ai::assignment_for( helper ) );
    REQUIRE( npc_ai::active_goal( helper ) );
    CHECK( npc_ai::active_goal( helper )->kind == npc_ai::ai_goal_kind::assist_ally );

    CHECK( npc_ai::complete_assignment( helper ) );
    CHECK_FALSE( npc_ai::assignment_for( helper ) );
    CHECK( npc_ai::active_goal( requester )->id == parent );
}
