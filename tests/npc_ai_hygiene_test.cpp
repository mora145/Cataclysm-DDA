#include "cata_catch.h"

#include <filesystem>
#include <optional>
#include <string>

#include "avatar.h"
#include "faction.h"
#include "game.h"
#include "map_helpers.h"
#include "npc.h"
#include "npc_ai_async.h"
#include "npc_ai_coordination.h"
#include "npc_ai_debug.h"
#include "npc_ai_goal.h"
#include "player_helpers.h"
#include "point.h"

namespace
{

static const faction_id faction_your_followers( "your_followers" );

struct restore_debug_gate {
    ~restore_debug_gate() {
        npc_ai::set_runtime_debug_enabled_for_test( false );
    }
};

npc &prepare_follower( const point_bub_ms &where, const std::string &name )
{
    npc &who = spawn_npc( where, "test_talker" );
    who.name = name;
    who.set_fac( faction_your_followers );
    who.set_attitude( NPCATT_FOLLOW );
    return who;
}

} // namespace

TEST_CASE( "npc_ai_diagnostic_logging_is_silent_unless_explicitly_enabled",
           "[npc_ai][npc_ai_hygiene]" )
{
    restore_debug_gate restore;
    const std::string filename = "npc_ai_hygiene_probe.txt";
    const std::filesystem::path path = npc_ai::debug_file_path( filename );
    std::error_code error;
    std::filesystem::remove( path, error );

    SECTION( "disabled gate never touches the filesystem" ) {
        npc_ai::set_runtime_debug_enabled_for_test( false );
        REQUIRE_FALSE( npc_ai::runtime_debug_enabled() );

        npc_ai::append_debug_line( filename, "must not be written" );
        {
            npc_ai::debug_stream stream( filename );
            CHECK_FALSE( static_cast<bool>( stream ) );
            stream << "must not be written either" << '\n';
        }

        CHECK_FALSE( std::filesystem::exists( path, error ) );
    }

    SECTION( "enabled gate writes through the portable user directory" ) {
        npc_ai::set_runtime_debug_enabled_for_test( true );
        REQUIRE( npc_ai::runtime_debug_enabled() );

        npc_ai::append_debug_line( filename, "recorded" );
        REQUIRE( std::filesystem::exists( path, error ) );
        CHECK( std::filesystem::file_size( path, error ) > 0 );

        // No diagnostic path may point outside the user directory.
        CHECK( path.string().find( "OneDrive" ) == std::string::npos );

        std::filesystem::remove( path, error );
    }
}

TEST_CASE( "npc_ai_per_npc_state_does_not_survive_a_session_change",
           "[npc_ai][npc_ai_hygiene]" )
{
    npc_ai::end_ai_session();
    clear_map();
    clear_avatar();
    clear_npcs();
    g->faction_manager_ptr->create_if_needed();
    g->place_player( tripoint_bub_ms{ 60, 60, 0 } );
    npc_ai::begin_ai_session();

    npc &requester = prepare_follower( point_bub_ms{ 61, 60 }, "Liam" );
    npc &helper = prepare_follower( point_bub_ms{ 62, 60 }, "Kim" );

    const npc_ai::ai_goal_id goal = npc_ai::begin_goal(
                                        requester, npc_ai::ai_goal_kind::unload_vehicle,
                                        npc_ai::ai_goal_priority::normal, "unload the truck" );
    REQUIRE( goal != 0 );
    REQUIRE( npc_ai::active_goal( requester ).has_value() );
    REQUIRE_FALSE( npc_ai::goal_history( requester ).empty() );

    const std::optional<npc_ai::delegated_assignment> delegated =
        npc_ai::delegate_to_helper( requester, helper,
                                    npc_ai::cooperative_task_kind::logistics, goal,
                                    "help unload" );
    REQUIRE( delegated.has_value() );
    REQUIRE( npc_ai::assignment_for( helper ).has_value() );

    // Loading another world must not inherit the previous world's AI state,
    // because character ids are reused between saves.
    npc_ai::begin_ai_session();

    CHECK_FALSE( npc_ai::active_goal( requester ).has_value() );
    CHECK( npc_ai::goal_history( requester ).empty() );
    CHECK_FALSE( npc_ai::assignment_for( helper ).has_value() );
}
