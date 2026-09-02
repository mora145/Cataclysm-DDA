#include "cata_catch.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <iostream>
#include <mutex>
#include <string>
#include <vector>

#include "avatar.h"
#include "bodypart.h"
#include "calendar.h"
#include "faction.h"
#include "game.h"
#include "map.h"
#include "map_helpers.h"
#include "monster.h"
#include "npc.h"
#include "npc_ai_async.h"
#include "npc_ai_combat_social.h"
#include "npc_ai_context.h"
#include "npc_ai_event_stream.h"
#include "npc_ai_memory.h"
#include "npc_ai_profiler.h"
#include "npc_ai_spontaneous.h"
#include "player_helpers.h"
#include "point.h"
#include "rng.h"
#include "sounds.h"

using namespace std::chrono_literals;

namespace
{

static const faction_id faction_your_followers( "your_followers" );
static const efftype_id effect_grabbed( "grabbed" );
static const ter_str_id ter_floor( "t_floor" );
static const ter_str_id ter_wall_metal( "t_wall_metal" );

struct reset_combat_executor {
    ~reset_combat_executor() {
        npc_ai::reset_ai_request_system_for_test();
    }
};

void refresh_visibility( npc &observer )
{
    map &here = get_map();
    here.invalidate_map_cache( observer.posz() );
    here.build_map_cache( observer.posz(), true );
    here.invalidate_visibility_cache();
    here.update_visibility_cache( observer.posz() );
    observer.recalc_sight_limits();
}

npc &prepare_combat_follower()
{
    npc_ai::end_ai_session();
    clear_map();
    clear_avatar();
    clear_npcs();
    set_time_to_day();
    g->faction_manager_ptr->create_if_needed();
    g->place_player( tripoint_bub_ms{ 60, 60, 0 } );
    npc &who = spawn_npc( point_bub_ms{ 61, 60 }, "test_talker" );
    who.name = "Liam";
    who.set_fac( faction_your_followers );
    who.set_attitude( NPCATT_FOLLOW );
    refresh_visibility( who );
    npc_ai::begin_ai_session();
    npc_ai::reset_combat_social_state_for_test( who );
    return who;
}

monster &place_visible_zombie( npc &who, const int distance = 3 )
{
    const tripoint_bub_ms origin = who.pos_bub( get_map() );
    monster &zombie = spawn_test_monster(
                          "mon_zombie", tripoint_bub_ms{ origin.x() + distance, origin.y(), origin.z() } );
    refresh_visibility( who );
    who.regen_ai_cache();
    return zombie;
}

bool prompt_contains( const npc_ai::ai_request_completion &completion, const std::string &needle )
{
    return completion.request.prompt.find( needle ) != std::string::npos;
}

npc_ai::combat_visible_creature hostile_snapshot_creature(
    const std::string &name, const std::uint64_t runtime_identity, const int distance,
    const bool observer_target = false, const int character_id = -1 )
{
    npc_ai::combat_visible_creature creature;
    creature.name = name;
    creature.hostile = true;
    creature.monster = character_id < 0;
    creature.npc = character_id >= 0;
    creature.runtime_identity = runtime_identity;
    creature.character_id = character_id;
    creature.distance = distance;
    creature.dx = distance;
    creature.observer_target = observer_target;
    return creature;
}

npc_ai::combat_perception_snapshot combat_snapshot(
    std::vector<npc_ai::combat_visible_creature> creatures, const float tactical_danger = 5.0f )
{
    npc_ai::combat_perception_snapshot snapshot;
    snapshot.visible_creatures = std::move( creatures );
    snapshot.visible_enemy_count = static_cast<int>( std::count_if(
                snapshot.visible_creatures.begin(), snapshot.visible_creatures.end(),
    []( const npc_ai::combat_visible_creature &creature ) {
        return creature.hostile;
    } ) );
    snapshot.in_combat = snapshot.visible_enemy_count > 0;
    snapshot.tactical_danger = tactical_danger;
    return snapshot;
}

std::vector<npc_ai::combat_social_event> first_sight_events(
    const npc_ai::combat_perception_snapshot &before,
    const npc_ai::combat_perception_snapshot &now )
{
    std::vector<npc_ai::combat_social_event> result =
        npc_ai::detect_combat_social_events_for_test( before, now );
    result.erase( std::remove_if( result.begin(), result.end(),
    []( const npc_ai::combat_social_event &event ) {
        return !event.observer_first_sight;
    } ), result.end() );
    return result;
}

} // namespace

TEST_CASE( "combat_social_first_sight_uses_typed_now_minus_before_identity",
           "[npc_ai][npc_ai_combat_social][npc_ai_first_sight]" )
{
    SECTION( "twenty_three old hostiles cannot steal the new target" ) {
        std::vector<npc_ai::combat_visible_creature> old_hostiles;
        for( std::uint64_t id = 1; id <= 23; ++id ) {
            old_hostiles.push_back( hostile_snapshot_creature(
                                        "old zombie " + std::to_string( id ), id,
                                        static_cast<int>( id ) ) );
        }
        npc_ai::combat_perception_snapshot before = combat_snapshot( old_hostiles );
        std::vector<npc_ai::combat_visible_creature> current = old_hostiles;
        current.front().dx += 1;
        current.front().distance += 1;
        current.push_back( hostile_snapshot_creature( "hulk", 990, 4 ) );
        const std::vector<npc_ai::combat_social_event> spotted =
            first_sight_events( before, combat_snapshot( current ) );

        REQUIRE( spotted.size() == 1 );
        CHECK( spotted.front().target_name == "hulk" );
        CHECK( spotted.front().target_identity == "runtime:990" );
    }

    SECTION( "continuous visibility and movement preserve identity" ) {
        const npc_ai::combat_perception_snapshot before = combat_snapshot( {
            hostile_snapshot_creature( "zombie", 11, 3 )
        } );
        npc_ai::combat_perception_snapshot stationary = before;
        CHECK( first_sight_events( before, stationary ).empty() );

        npc_ai::combat_perception_snapshot moved = before;
        moved.visible_creatures.front().dx = 4;
        moved.visible_creatures.front().distance = 4;
        CHECK( first_sight_events( before, moved ).empty() );
    }

    SECTION( "equal count exit and entry names the entrant" ) {
        const npc_ai::combat_perception_snapshot before = combat_snapshot( {
            hostile_snapshot_creature( "zombie A", 21, 2 ),
            hostile_snapshot_creature( "zombie B", 22, 3 )
        } );
        const npc_ai::combat_perception_snapshot now = combat_snapshot( {
            hostile_snapshot_creature( "zombie A", 21, 2 ),
            hostile_snapshot_creature( "hulk", 99, 5 )
        } );
        const std::vector<npc_ai::combat_social_event> spotted = first_sight_events( before, now );

        REQUIRE( spotted.size() == 1 );
        CHECK( spotted.front().target_name == "hulk" );
        CHECK( spotted.front().target_identity == "runtime:99" );
    }

    SECTION( "aggregate danger and observer target do not upgrade a weak newcomer" ) {
        const npc_ai::combat_perception_snapshot before = combat_snapshot( {
            hostile_snapshot_creature( "old hulk", 31, 2 )
        }, 30.0f );
        const npc_ai::combat_perception_snapshot now = combat_snapshot( {
            hostile_snapshot_creature( "old hulk", 31, 2 ),
            hostile_snapshot_creature( "new zombie", 32, 3, true )
        }, 30.0f );
        const std::vector<npc_ai::combat_social_event> events =
            npc_ai::detect_combat_social_events_for_test( before, now );
        const std::vector<npc_ai::combat_social_event> spotted = first_sight_events( before, now );

        REQUIRE( spotted.size() == 1 );
        CHECK( spotted.front().type == npc_ai::combat_social_event_type::enemy_spotted );
        CHECK( spotted.front().target_name == "new zombie" );
        CHECK( spotted.front().target_identity == "runtime:32" );
        CHECK_FALSE( std::any_of( events.begin(), events.end(),
        []( const npc_ai::combat_social_event &event ) {
            return event.type == npc_ai::combat_social_event_type::dangerous_enemy_spotted;
        } ) );
    }

    SECTION( "character and runtime numeric domains do not collide" ) {
        const npc_ai::combat_perception_snapshot before = combat_snapshot( {
            hostile_snapshot_creature( "hostile character", 900, 2, false, 57 )
        } );
        const npc_ai::combat_perception_snapshot now = combat_snapshot( {
            hostile_snapshot_creature( "hostile character", 900, 2, false, 57 ),
            hostile_snapshot_creature( "monster with same numeric id", 57, 3 )
        } );
        const std::vector<npc_ai::combat_social_event> spotted = first_sight_events( before, now );

        REQUIRE( spotted.size() == 1 );
        CHECK( spotted.front().target_identity == "runtime:57" );
        CHECK( spotted.front().target_identity != "character:57" );
    }
}

TEST_CASE( "combat_social_surrounded_is_a_two_enemy_or_grabbed_transition",
           "[npc_ai][npc_ai_combat_social][npc_ai_surrounded]" )
{
    npc_ai::combat_visible_creature player;
    player.name = "player";
    player.player = true;
    player.character_id = 1;
    player.runtime_identity = 101;
    player.adjacent_hostiles = 1;
    npc_ai::combat_perception_snapshot before = combat_snapshot( {
        hostile_snapshot_creature( "zombie", 1, 2 )
    } );
    before.visible_creatures.push_back( player );

    const std::vector<npc_ai::combat_social_event> unchanged =
        npc_ai::detect_combat_social_events_for_test( before, before );
    CHECK_FALSE( std::any_of( unchanged.begin(), unchanged.end(),
    []( const npc_ai::combat_social_event &event ) {
        return event.type == npc_ai::combat_social_event_type::player_surrounded;
    } ) );

    npc_ai::combat_perception_snapshot two_adjacent = before;
    two_adjacent.visible_creatures.back().adjacent_hostiles = 2;
    const std::vector<npc_ai::combat_social_event> transition =
        npc_ai::detect_combat_social_events_for_test( before, two_adjacent );
    REQUIRE( std::count_if( transition.begin(), transition.end(),
    []( const npc_ai::combat_social_event &event ) {
        return event.type == npc_ai::combat_social_event_type::player_surrounded;
    } ) == 1 );

    npc_ai::combat_perception_snapshot grabbed = before;
    grabbed.visible_creatures.back().grabbed = true;
    const std::vector<npc_ai::combat_social_event> grabbed_transition =
        npc_ai::detect_combat_social_events_for_test( before, grabbed );
    CHECK( std::count_if( grabbed_transition.begin(), grabbed_transition.end(),
    []( const npc_ai::combat_social_event &event ) {
        return event.type == npc_ai::combat_social_event_type::player_surrounded;
    } ) == 1 );

    npc_ai::combat_visible_creature ally = player;
    ally.player = false;
    ally.npc = true;
    ally.name = "ally";
    ally.character_id = 2;
    npc_ai::combat_perception_snapshot ally_before = combat_snapshot( {
        hostile_snapshot_creature( "zombie", 1, 2 )
    } );
    ally_before.visible_creatures.push_back( ally );
    npc_ai::combat_perception_snapshot ally_now = ally_before;
    ally_now.visible_creatures.back().adjacent_hostiles = 2;
    const std::vector<npc_ai::combat_social_event> ally_transition =
        npc_ai::detect_combat_social_events_for_test( ally_before, ally_now );
    CHECK( std::count_if( ally_transition.begin(), ally_transition.end(),
    []( const npc_ai::combat_social_event &event ) {
        return event.type == npc_ai::combat_social_event_type::ally_surrounded;
    } ) == 1 );
}

TEST_CASE( "combat_social_initial_combat_baseline_emits_only_combat_start",
           "[npc_ai][npc_ai_combat_social][npc_ai_first_sight]" )
{
    npc_ai::combat_perception_snapshot current = combat_snapshot( {
        hostile_snapshot_creature( "zombie A", 71, 2 ),
        hostile_snapshot_creature( "zombie B", 72, 3 )
    } );
    npc_ai::combat_perception_snapshot baseline = current;
    baseline.in_combat = false;
    const std::vector<npc_ai::combat_social_event> events =
        npc_ai::detect_combat_social_events_for_test( baseline, current );

    CHECK( first_sight_events( baseline, current ).empty() );
    CHECK( std::count_if( events.begin(), events.end(),
    []( const npc_ai::combat_social_event &event ) {
        return event.type == npc_ai::combat_social_event_type::combat_start;
    } ) == 1 );
}

TEST_CASE( "combat_social_first_sight_bypasses_ordinary_gap_for_distinct_identities",
           "[npc_ai][npc_ai_combat_social][npc_ai_first_sight][npc_ai_async]" )
{
    reset_combat_executor reset;
    npc &who = prepare_combat_follower();
    place_visible_zombie( who, 3 );
    std::atomic<int> first_sight_requests{ 0 };
    npc_ai::set_ai_request_executor_for_test( [&]( const std::string &prompt ) {
        if( prompt.find( "type=ENEMY_SPOTTED" ) != std::string::npos ) {
            first_sight_requests.fetch_add( 1 );
        }
        return npc_ai::ai_response{ true, "DECISION=SILENT", "" };
    } );
    npc_ai::begin_ai_session();
    refresh_visibility( who );
    who.regen_ai_cache();

    const npc_ai::combat_social_process_result started = npc_ai::process_combat_social( who );
    REQUIRE( started.request_queued );
    CHECK( started.event.type == npc_ai::combat_social_event_type::combat_start );
    REQUIRE( npc_ai::get_ai_request_queue().wait_until_idle_for_test( 1s ) );
    npc_ai::process_ai_completions();

    calendar::turn += 3_turns;
    const tripoint_bub_ms origin = who.pos_bub( get_map() );
    spawn_test_monster( "mon_zombie_brute",
                        tripoint_bub_ms{ origin.x() + 4, origin.y(), origin.z() } );
    refresh_visibility( who );
    who.regen_ai_cache();
    const npc_ai::combat_social_process_result first = npc_ai::process_combat_social( who );
    REQUIRE( first.request_queued );
    CHECK( first.event.type == npc_ai::combat_social_event_type::enemy_spotted );
    CHECK( first.event.target_name.find( "brute" ) != std::string::npos );
    CHECK( first.event.may_bypass_cooldown );
    CHECK_FALSE( first.event.coalesced_sequences.empty() );
    REQUIRE( npc_ai::get_ai_request_queue().wait_until_idle_for_test( 1s ) );
    npc_ai::process_ai_completions();

    calendar::turn += 3_turns;
    spawn_test_monster( "mon_zombie_dog",
                        tripoint_bub_ms{ origin.x() + 5, origin.y() + 1, origin.z() } );
    refresh_visibility( who );
    who.regen_ai_cache();
    const npc_ai::combat_social_process_result second = npc_ai::process_combat_social( who );
    INFO( "detected=" << second.event_detected << " type=" <<
          npc_ai::combat_social_event_name( second.event.type ) << " target=" <<
          second.event.target_name << " bypass=" << second.event.may_bypass_cooldown );
    REQUIRE( second.request_queued );
    CHECK( second.event.type == npc_ai::combat_social_event_type::enemy_spotted );
    CHECK( second.event.target_identity != first.event.target_identity );
    REQUIRE( npc_ai::get_ai_request_queue().wait_until_idle_for_test( 1s ) );
    CHECK( first_sight_requests.load() == 2 );

    npc_ai::process_ai_completions();
    calendar::turn += 1_turns;
    refresh_visibility( who );
    who.regen_ai_cache();
    CHECK_FALSE( npc_ai::process_combat_social( who ).request_queued );
    CHECK( first_sight_requests.load() == 2 );
}

TEST_CASE( "combat_social_late_group_los_updates_known_by_without_another_request",
           "[npc_ai][npc_ai_combat_social][npc_ai_first_sight][npc_ai_knowledge]" )
{
    reset_combat_executor reset;
    npc &liam = prepare_combat_follower();
    npc &kim = spawn_npc( point_bub_ms{ 61, 64 }, "test_talker" );
    kim.name = "Kim";
    kim.set_fac( faction_your_followers );
    kim.set_attitude( NPCATT_FOLLOW );
    map &here = get_map();
    for( int x = 60; x <= 66; ++x ) {
        if( x != 61 ) {
            here.ter_set( tripoint_bub_ms{ x, 62, 0 }, ter_wall_metal );
        }
    }
    spawn_test_monster( "mon_zombie", tripoint_bub_ms{ 61, 61, 0 } );
    for( npc *ally : { &liam, &kim } ) {
        refresh_visibility( *ally );
        ally->regen_ai_cache();
    }

    std::atomic<int> first_sight_requests{ 0 };
    npc_ai::set_ai_request_executor_for_test( [&]( const std::string &prompt ) {
        if( prompt.find( "type=ENEMY_SPOTTED" ) != std::string::npos ) {
            first_sight_requests.fetch_add( 1 );
        }
        return npc_ai::ai_response{ true, "DECISION=SILENT", "" };
    } );
    npc_ai::begin_ai_session();
    for( npc *ally : { &liam, &kim } ) {
        ally->regen_ai_cache();
        npc_ai::process_combat_social( *ally );
    }
    REQUIRE( npc_ai::get_ai_request_queue().wait_until_idle_for_test( 1s ) );
    npc_ai::process_ai_completions();

    calendar::turn += 3_turns;
    monster &hulk = spawn_test_monster( "mon_zombie_hulk", tripoint_bub_ms{ 65, 60, 0 } );
    refresh_visibility( liam );
    refresh_visibility( kim );
    liam.regen_ai_cache();
    kim.regen_ai_cache();
    REQUIRE( liam.sees( here, hulk ) );
    REQUIRE_FALSE( kim.sees( here, hulk ) );

    const npc_ai::combat_social_process_result announced =
        npc_ai::process_combat_social( liam );
    REQUIRE( announced.request_queued );
    REQUIRE( announced.event.observer_first_sight );
    const std::uint64_t shared_fact = announced.event.sequence_id;
    npc_ai::process_combat_social( kim );
    REQUIRE( npc_ai::get_ai_request_queue().wait_until_idle_for_test( 1s ) );
    npc_ai::process_ai_completions();
    CHECK( first_sight_requests.load() == 1 );
    const std::optional<npc_ai::world_event> before_kim =
        npc_ai::world_event_by_sequence( shared_fact );
    REQUIRE( before_kim );
    CHECK( std::find( before_kim->known_by_npc_ids.begin(), before_kim->known_by_npc_ids.end(),
                      kim.getID().get_value() ) == before_kim->known_by_npc_ids.end() );

    calendar::turn += 15_turns;
    for( int x = 60; x <= 66; ++x ) {
        here.ter_set( tripoint_bub_ms{ x, 62, 0 }, ter_floor );
    }
    refresh_visibility( liam );
    refresh_visibility( kim );
    liam.regen_ai_cache();
    kim.regen_ai_cache();
    REQUIRE( kim.sees( here, hulk ) );
    const npc_ai::combat_social_process_result learned = npc_ai::process_combat_social( kim );

    CHECK_FALSE( learned.request_queued );
    CHECK( first_sight_requests.load() == 1 );
    const std::optional<npc_ai::world_event> after_kim =
        npc_ai::world_event_by_sequence( shared_fact );
    REQUIRE( after_kim );
    CHECK( std::find( after_kim->known_by_npc_ids.begin(), after_kim->known_by_npc_ids.end(),
                      kim.getID().get_value() ) != after_kim->known_by_npc_ids.end() );
}

TEST_CASE( "combat_social_group_loss_allows_same_identity_to_be_announced_after_reappearance",
           "[npc_ai][npc_ai_combat_social][npc_ai_first_sight][npc_ai_async]" )
{
    reset_combat_executor reset;
    npc &who = prepare_combat_follower();
    place_visible_zombie( who, 3 );
    std::atomic<int> first_sight_requests{ 0 };
    npc_ai::set_ai_request_executor_for_test( [&]( const std::string &prompt ) {
        if( prompt.find( "type=ENEMY_SPOTTED" ) != std::string::npos ) {
            first_sight_requests.fetch_add( 1 );
        }
        return npc_ai::ai_response{ true, "DECISION=SILENT", "" };
    } );
    npc_ai::begin_ai_session();
    who.regen_ai_cache();
    npc_ai::process_combat_social( who );
    REQUIRE( npc_ai::get_ai_request_queue().wait_until_idle_for_test( 1s ) );
    npc_ai::process_ai_completions();

    calendar::turn += 3_turns;
    map &here = get_map();
    monster &hulk = spawn_test_monster( "mon_zombie_hulk", tripoint_bub_ms{ 66, 60, 0 } );
    refresh_visibility( who );
    who.regen_ai_cache();
    const npc_ai::combat_social_process_result first = npc_ai::process_combat_social( who );
    REQUIRE( first.request_queued );
    const std::string first_identity = first.event.target_identity;
    REQUIRE( npc_ai::get_ai_request_queue().wait_until_idle_for_test( 1s ) );
    npc_ai::process_ai_completions();

    calendar::turn += 1_turns;
    hulk.setpos( here, tripoint_bub_ms{ 1, 1, 0 } );
    refresh_visibility( who );
    who.regen_ai_cache();
    npc_ai::process_combat_social( who );

    calendar::turn += 121_turns;
    hulk.setpos( here, tripoint_bub_ms{ 66, 60, 0 } );
    refresh_visibility( who );
    who.regen_ai_cache();
    const npc_ai::combat_social_process_result reappeared =
        npc_ai::process_combat_social( who );

    REQUIRE( reappeared.request_queued );
    CHECK( reappeared.event.target_identity == first_identity );
    REQUIRE( npc_ai::get_ai_request_queue().wait_until_idle_for_test( 1s ) );
    CHECK( first_sight_requests.load() == 2 );
}

TEST_CASE( "combat_social_simultaneous_player_grab_and_surrounded_is_one_candidate",
           "[npc_ai][npc_ai_combat_social][npc_ai_surrounded][npc_ai_async]" )
{
    reset_combat_executor reset;
    npc &who = prepare_combat_follower();
    spawn_test_monster( "mon_zombie", tripoint_bub_ms{ 60, 61, 0 } );
    refresh_visibility( who );
    who.regen_ai_cache();
    npc_ai::set_ai_request_executor_for_test(
    []( const std::string & ) {
        return npc_ai::ai_response{ true, "DECISION=SILENT", "" };
    } );
    npc_ai::begin_ai_session();
    who.regen_ai_cache();
    npc_ai::process_combat_social( who );
    REQUIRE( npc_ai::get_ai_request_queue().wait_until_idle_for_test( 1s ) );
    npc_ai::process_ai_completions();

    calendar::turn += 3_turns;
    spawn_test_monster( "mon_zombie", tripoint_bub_ms{ 59, 60, 0 } );
    get_avatar().add_effect( effect_grabbed, 10_turns, body_part_arm_l, false, 1, true );
    refresh_visibility( who );
    who.regen_ai_cache();
    const npc_ai::combat_social_process_result danger = npc_ai::process_combat_social( who );

    REQUIRE( danger.request_queued );
    CHECK( danger.event.type == npc_ai::combat_social_event_type::player_grabbed );
    CHECK_FALSE( danger.event.coalesced_sequences.empty() );
    REQUIRE( npc_ai::get_ai_request_queue().wait_until_idle_for_test( 1s ) );
    npc_ai::process_ai_completions();
    calendar::turn += 1_turns;
    refresh_visibility( who );
    who.regen_ai_cache();
    const npc_ai::combat_social_process_result next = npc_ai::process_combat_social( who );
    CHECK( ( !next.event_detected ||
             next.event.type != npc_ai::combat_social_event_type::player_surrounded ) );
}

TEST_CASE( "combat_social_process_only_enqueues_and_never_executes_http_inline",
           "[npc_ai][npc_ai_combat_social][npc_ai_async]" )
{
    reset_combat_executor reset;
    npc &who = prepare_combat_follower();
    place_visible_zombie( who );
    spawn_test_monster( "mon_zombie", tripoint_bub_ms{ 65, 61, 0 } );
    refresh_visibility( who );
    who.regen_ai_cache();
    std::atomic<int> executor_calls{ 0 };
    npc_ai::set_ai_request_executor_for_test( [&]( const std::string & ) {
        executor_calls.fetch_add( 1 );
        return npc_ai::ai_response{ true, "DECISION=SILENT", "" };
    }, false );
    npc_ai::begin_ai_session();
    who.regen_ai_cache();

    const npc_ai::combat_social_process_result baseline = npc_ai::process_combat_social( who );
    REQUIRE( baseline.request_queued );
    CHECK( baseline.event.type == npc_ai::combat_social_event_type::combat_start );
    CHECK( executor_calls.load() == 0 );
}

TEST_CASE( "combat_social_twenty_allies_share_one_first_sight_request",
           "[npc_ai][npc_ai_combat_social][npc_ai_first_sight][npc_ai_scaling]" )
{
    reset_combat_executor reset;
    npc &first = prepare_combat_follower();
    std::vector<npc *> followers{ &first };
    for( int index = 0; index < 19; ++index ) {
        npc &ally = spawn_npc( point_bub_ms{ 52 + index, 62 }, "test_talker" );
        ally.name = "First sight ally " + std::to_string( index );
        ally.set_fac( faction_your_followers );
        ally.set_attitude( NPCATT_FOLLOW );
        followers.push_back( &ally );
    }
    spawn_test_monster( "mon_zombie", tripoint_bub_ms{ 64, 60, 0 } );
    npc_ai::set_ai_request_executor_for_test(
    []( const std::string & ) {
        return npc_ai::ai_response{ true, "DECISION=SILENT", "" };
    } );
    npc_ai::begin_ai_session();
    for( npc *ally : followers ) {
        refresh_visibility( *ally );
        ally->regen_ai_cache();
        npc_ai::process_combat_social( *ally );
    }
    REQUIRE( npc_ai::get_ai_request_queue().wait_until_idle_for_test( 1s ) );
    npc_ai::process_ai_completions();

    calendar::turn += 3_turns;
    monster &hulk = spawn_test_monster( "mon_zombie_hulk", tripoint_bub_ms{ 65, 61, 0 } );
    int requests_queued = 0;
    int observers_with_los = 0;
    for( npc *ally : followers ) {
        refresh_visibility( *ally );
        ally->regen_ai_cache();
        if( ally->sees( get_map(), hulk ) ) {
            ++observers_with_los;
        }
        requests_queued += npc_ai::process_combat_social( *ally ).request_queued ? 1 : 0;
    }

    CHECK( observers_with_los == 20 );
    CHECK( requests_queued == 1 );
}

TEST_CASE( "combat_social_snapshot_contains_only_visible_creatures",
           "[npc_ai][npc_ai_combat_social]" )
{
    npc &who = prepare_combat_follower();
    map &here = get_map();

    SECTION( "visible hostile is grounded" ) {
        place_visible_zombie( who );
        const npc_ai::combat_perception_snapshot snapshot =
            npc_ai::build_combat_perception_snapshot( who );
        CHECK( snapshot.visible_enemy_count == 1 );
        REQUIRE( !snapshot.visible_creatures.empty() );
        CHECK( std::any_of( snapshot.visible_creatures.begin(),
                            snapshot.visible_creatures.end(),
        []( const npc_ai::combat_visible_creature & creature ) {
            return creature.hostile &&
                   creature.name.find( "zombie" ) !=
                   std::string::npos;
        } ) );
        npc_ai::combat_social_event event;
        event.type = npc_ai::combat_social_event_type::combat_start;
        event.detail = "Comienza un encuentro real.";
        const std::string prompt =
            npc_ai::build_combat_social_prompt( who, snapshot, event );
        CHECK( prompt.find( "entity_type=monster" ) != std::string::npos );
        CHECK( prompt.find( "alive=yes" ) != std::string::npos );
        CHECK( prompt.find( "exact_name=" ) != std::string::npos );
        const std::string system = npc_ai::build_npc_system_prompt(
                                       who, npc_ai::npc_prompt_purpose::combat_social );
        CHECK( system.find( "Never reinterpret a monster" ) != std::string::npos );
    }

    SECTION( "hostile behind opaque wall is absent" ) {
        const tripoint_bub_ms origin = who.pos_bub( here );
        here.ter_set( tripoint_bub_ms{origin.x() + 1, origin.y(), origin.z()},
                      ter_wall_metal );
        spawn_test_monster( "mon_zombie", tripoint_bub_ms{origin.x() + 3,
                            origin.y(), origin.z()} );
        refresh_visibility( who );
        who.regen_ai_cache();
        const npc_ai::combat_perception_snapshot snapshot =
            npc_ai::build_combat_perception_snapshot( who );
        CHECK( snapshot.visible_enemy_count == 0 );
        CHECK_FALSE( std::any_of(
                         snapshot.visible_creatures.begin(), snapshot.visible_creatures.end(),
        []( const npc_ai::combat_visible_creature & creature ) {
            return creature.hostile;
        } ) );
    }
}

TEST_CASE( "combat_social_snapshot_work_is_bounded_per_observer_at_20_npcs",
           "[npc_ai][npc_ai_combat_social][npc_ai_scaling]" )
{
    npc &first = prepare_combat_follower();
    std::vector<npc *> followers{ &first };
    for( int index = 0; index < 19; ++index ) {
        npc &ally = spawn_npc( point_bub_ms{ 52 + index, 62 }, "test_talker" );
        ally.name = "Scaling ally " + std::to_string( index );
        ally.set_fac( faction_your_followers );
        ally.set_attitude( NPCATT_FOLLOW );
        followers.push_back( &ally );
    }

    for( npc *ally : followers ) {
        refresh_visibility( *ally );
        ally->regen_ai_cache();
    }

    std::size_t total_visibility_checks = 0;
    for( npc *ally : followers ) {
        const npc_ai::combat_perception_snapshot snapshot =
            npc_ai::build_combat_perception_snapshot( *ally );
        INFO( "observer=" << ally->get_name() );
        CHECK( snapshot.creature_visibility_checks <=
               npc_ai::combat_snapshot_visibility_check_limit() );
        CHECK( snapshot.visible_creatures.size() <= 12 );
        total_visibility_checks += snapshot.creature_visibility_checks;
    }

    // The old implementation performed a complete global creature scan for
    // every observer.  The new work bound is population-independent, so total
    // observer-specific LOS work is O(N), even when the prompt is saturated.
    CHECK( total_visibility_checks <=
           followers.size() * npc_ai::combat_snapshot_visibility_check_limit() );
}

TEST_CASE( "npc_ai_performance_probe", "[.npc_ai_performance]" )
{
    reset_combat_executor reset;
    npc_ai::set_ai_request_executor_for_test(
    []( const std::string & ) {
        return npc_ai::ai_response{ true, "DECISION=SILENT", "" };
    },
    false );
    npc_ai::set_profiling_enabled_for_test( true );

    const auto measured = []( const std::string & label, const auto & operation ) {
        npc_ai::reset_profile();
        const auto started = std::chrono::steady_clock::now();
        operation();
        const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
                                 std::chrono::steady_clock::now() - started ).count();
        std::cout << "NPC_AI_PERF " << label << " wall_us=" << elapsed << '\n'
                  << npc_ai::format_profile_report( npc_ai::profile_snapshot() );
    };

    npc_ai::end_ai_session();
    clear_map();
    clear_avatar();
    clear_npcs();
    npc_ai::begin_ai_session();
    measured( "A_no_nearby_npc_10000_completion_polls", []() {
        for( int i = 0; i < 10000; ++i ) {
            npc_ai::process_ai_completions();
        }
    } );

    npc &one = prepare_combat_follower();
    npc_ai::process_combat_social( one );
    measured( "B_one_idle_npc_500_evaluations", [&]() {
        for( int i = 0; i < 500; ++i ) {
            npc_ai::process_combat_social( one );
        }
    } );

    std::vector<npc *> followers{ &one };
    for( int index = 1; index < 4; ++index ) {
        npc &ally = spawn_npc( point_bub_ms{ 61, 60 + index }, "test_talker" );
        ally.name = "Follower " + std::to_string( index );
        ally.set_fac( faction_your_followers );
        ally.set_attitude( NPCATT_FOLLOW );
        refresh_visibility( ally );
        npc_ai::reset_combat_social_state_for_test( ally );
        npc_ai::process_combat_social( ally );
        followers.push_back( &ally );
    }
    measured( "C_four_idle_npcs_500_evaluations_each", [&]() {
        for( int i = 0; i < 500; ++i ) {
            for( npc *ally : followers ) {
                npc_ai::process_combat_social( *ally );
            }
        }
    } );

    measured( "D_four_moving_followers_100_turns", [&]() {
        map &here = get_map();
        for( int turn = 0; turn < 100; ++turn ) {
            calendar::turn += 1_turns;
            for( std::size_t index = 0; index < followers.size(); ++index ) {
                npc *ally = followers[index];
                const tripoint_bub_ms old = ally->pos_bub( here );
                ally->setpos( here, tripoint_bub_ms{ old.x() + ( turn % 2 == 0 ? 1 : -1 ),
                                                     old.y(), old.z() } );
                ally->regen_ai_cache();
                npc_ai::process_combat_social( *ally );
            }
        }
    } );

    std::vector<monster *> enemies;
    for( int index = 0; index < 12; ++index ) {
        enemies.push_back( &spawn_test_monster(
                               "mon_zombie", tripoint_bub_ms{ 54 + index, 55 + index % 4, 0 } ) );
    }
    for( npc *ally : followers ) {
        refresh_visibility( *ally );
        ally->regen_ai_cache();
        npc_ai::reset_combat_social_state_for_test( *ally );
        npc_ai::process_combat_social( *ally );
    }
    measured( "E_four_npcs_twelve_zombies_200_evaluations_each", [&]() {
        for( int i = 0; i < 200; ++i ) {
            for( npc *ally : followers ) {
                npc_ai::process_combat_social( *ally );
            }
        }
    } );

    measured( "F_physical_event_publish_500_events", [&]() {
        for( int i = 0; i < 500; ++i ) {
            npc_ai::record_creature_world_event( npc_ai::world_event_type::attack_missed,
                                                 followers.front(), enemies.front(), 48,
                                                 "performance_probe", "confirmed miss", true );
        }
    } );

    measured( "G_ordinary_context_20_prompts", [&]() {
        for( int i = 0; i < 20; ++i ) {
            const std::string prompt = npc_ai::build_npc_prompt(
                                           *followers.front(), "Como estas?" );
            CHECK_FALSE( prompt.empty() );
        }
    } );

    measured( "H_detailed_context_5_prompts", [&]() {
        for( int i = 0; i < 5; ++i ) {
            const std::string prompt = npc_ai::build_npc_prompt(
                                           *followers.front(), "Describe todo lo que ves" );
            CHECK_FALSE( prompt.empty() );
        }
    } );

    npc_ai::set_profiling_enabled_for_test( false );
    CHECK( true );
}

TEST_CASE( "combat_social_repeated_combat_ticks_queue_only_one_request",
           "[npc_ai][npc_ai_combat_social][npc_ai_async]" )
{
    reset_combat_executor reset;
    npc &who = prepare_combat_follower();
    place_visible_zombie( who );
    npc_ai::set_ai_request_executor_for_test(
    []( const std::string & ) {
        return npc_ai::ai_response{ true, "DECISION=SILENT", "" };
    },
    false );
    npc_ai::begin_ai_session();

    const npc_ai::combat_social_process_result first = npc_ai::process_combat_social( who );
    REQUIRE( first.request_queued );
    CHECK( first.event.type == npc_ai::combat_social_event_type::combat_start );
    for( int i = 0; i < 20; ++i ) {
        calendar::turn += 1_turns;
        who.regen_ai_cache();
        CHECK_FALSE( npc_ai::process_combat_social( who ).request_queued );
    }
    CHECK( npc_ai::get_ai_request_queue().pending_count() == 1 );
}

TEST_CASE( "combat_social_reuses_snapshot_within_turn_and_polls_idle_periodically",
           "[npc_ai][npc_ai_combat_social][npc_ai_performance]" )
{
    npc &who = prepare_combat_follower();
    npc_ai::set_profiling_enabled_for_test( true );
    npc_ai::process_combat_social( who );
    npc_ai::reset_profile();

    for( int call = 0; call < 100; ++call ) {
        npc_ai::process_combat_social( who );
    }
    npc_ai::profile_report report = npc_ai::profile_snapshot();
    CHECK( report[static_cast<std::size_t>(
                      npc_ai::profile_subsystem::combat_social )].calls == 100 );
    CHECK( report[static_cast<std::size_t>(
                      npc_ai::profile_subsystem::combat_snapshot )].calls == 0 );

    calendar::turn += 5_turns;
    npc_ai::process_combat_social( who );
    report = npc_ai::profile_snapshot();
    CHECK( report[static_cast<std::size_t>(
                      npc_ai::profile_subsystem::combat_snapshot )].calls == 1 );
    npc_ai::set_profiling_enabled_for_test( false );
}

TEST_CASE( "combat_social_urgent_player_grab_supersedes_ordinary_pending_event",
           "[npc_ai][npc_ai_combat_social][npc_ai_async]" )
{
    reset_combat_executor reset;
    npc &who = prepare_combat_follower();
    place_visible_zombie( who );
    npc_ai::set_ai_request_executor_for_test(
    []( const std::string & prompt ) {
        return npc_ai::ai_response{ true, "DECISION=SILENT\nTEXT=" + prompt, "" };
    },
    false );
    npc_ai::begin_ai_session();

    REQUIRE( npc_ai::process_combat_social( who ).request_queued );
    calendar::turn += 5_turns;
    get_avatar().add_effect( effect_grabbed, 10_turns, body_part_arm_l, false, 1, true );
    refresh_visibility( who );
    who.regen_ai_cache();
    const npc_ai::combat_social_process_result urgent = npc_ai::process_combat_social( who );
    REQUIRE( urgent.request_queued );
    CHECK( urgent.event.type == npc_ai::combat_social_event_type::player_grabbed );

    npc_ai::get_ai_request_queue().start();
    REQUIRE( npc_ai::get_ai_request_queue().wait_until_idle_for_test( 1s ) );
    const auto completions = npc_ai::get_ai_request_queue().take_completions(
                                 npc_ai::get_ai_request_queue().ready_completion_count() );
    REQUIRE( completions.size() == 1 );
    CHECK( completions.front().request.priority == npc_ai::ai_request_priority::immediate_danger );
    CHECK( completions.front().request.event_kind == "PLAYER_GRABBED" );
    CHECK( prompt_contains( completions.front(), "grabbed=yes" ) );
}

TEST_CASE( "combat_social_prompt_does_not_name_hidden_enemy", "[npc_ai][npc_ai_combat_social]" )
{
    npc &who = prepare_combat_follower();
    map &here = get_map();
    const tripoint_bub_ms origin = who.pos_bub( here );
    here.ter_set( tripoint_bub_ms{ origin.x() + 1, origin.y(), origin.z() }, ter_wall_metal );
    spawn_test_monster( "mon_zombie", tripoint_bub_ms{ origin.x() + 3, origin.y(), origin.z() } );
    refresh_visibility( who );
    who.regen_ai_cache();
    const npc_ai::combat_perception_snapshot snapshot =
        npc_ai::build_combat_perception_snapshot( who );
    npc_ai::combat_social_event event;
    event.type = npc_ai::combat_social_event_type::combat_start;
    event.detail = "Prueba sin informacion visual.";
    const std::string prompt = npc_ai::build_combat_social_prompt( who, snapshot, event );
    CHECK( prompt.find( "visible_enemies=0" ) != std::string::npos );
    const std::size_t perception_begin = prompt.find( "CURRENT PERCEPTION" );
    const std::size_t perception_end = prompt.find( "environment=", perception_begin );
    REQUIRE( perception_begin != std::string::npos );
    REQUIRE( perception_end != std::string::npos );
    CHECK( prompt.substr( perception_begin, perception_end - perception_begin ).find( "zombie" ) ==
           std::string::npos );
}

TEST_CASE( "combat_social_result_is_discarded_after_encounter_ends",
           "[npc_ai][npc_ai_combat_social][npc_ai_async]" )
{
    reset_combat_executor reset;
    npc &who = prepare_combat_follower();
    place_visible_zombie( who );
    const std::string response = "STALE_COMBAT_" + random_string( 20 );
    npc_ai::set_ai_request_executor_for_test(
    [&]( const std::string & ) {
        return npc_ai::ai_response{ true, "DECISION=TALK\nTEXT=" + response, "" };
    },
    false );
    npc_ai::begin_ai_session();

    REQUIRE( npc_ai::process_combat_social( who ).request_queued );
    npc_ai::get_ai_request_queue().start();
    REQUIRE( npc_ai::get_ai_request_queue().wait_until_idle_for_test( 1s ) );
    g->clear_zombies();
    refresh_visibility( who );
    who.regen_ai_cache();
    npc_ai::process_combat_social( who );
    npc_ai::process_ai_completions();
    CHECK( npc_ai::build_memory_context( who ).find( response ) == std::string::npos );
}

TEST_CASE( "combat_social_silent_result_never_becomes_speech",
           "[npc_ai][npc_ai_combat_social][npc_ai_async]" )
{
    reset_combat_executor reset;
    npc &who = prepare_combat_follower();
    place_visible_zombie( who );
    npc_ai::set_ai_request_executor_for_test(
    []( const std::string & ) {
        return npc_ai::ai_response{ true, "DECISION=SILENT", "" };
    } );
    npc_ai::begin_ai_session();

    REQUIRE( npc_ai::process_combat_social( who ).request_queued );
    REQUIRE( npc_ai::get_ai_request_queue().wait_until_idle_for_test( 1s ) );
    npc_ai::process_ai_completions();
    CHECK( npc_ai::build_memory_context( who ).find( "DECISION=SILENT" ) == std::string::npos );
}

TEST_CASE( "combat_social_valid_result_uses_main_thread_speech_application",
           "[npc_ai][npc_ai_combat_social][npc_ai_async]" )
{
    reset_combat_executor reset;
    npc &who = prepare_combat_follower();
    place_visible_zombie( who );
    const std::string response = "COMBAT_SPEECH_" + std::to_string(
                                     std::chrono::steady_clock::now().time_since_epoch().count() );
    npc_ai::set_ai_request_executor_for_test( [&]( const std::string & ) {
        return npc_ai::ai_response{ true, "DECISION=TALK\nTEXT=" + response, "" };
    } );
    npc_ai::begin_ai_session();

    REQUIRE( npc_ai::process_combat_social( who ).request_queued );
    REQUIRE( npc_ai::get_ai_request_queue().wait_until_idle_for_test( 1s ) );
    CHECK( npc_ai::build_memory_context( who ).find( response ) == std::string::npos );
    npc_ai::process_ai_completions();
    CHECK( npc_ai::build_memory_context( who ).find( response ) != std::string::npos );
    CHECK( npc_ai::get_ai_request_queue().pending_count() == 0 );
    CHECK_FALSE( npc_ai::process_combat_social( who ).request_queued );
}

TEST_CASE( "combat_social_visible_ally_grab_targets_that_ally_without_response_loop",
           "[npc_ai][npc_ai_combat_social][npc_ai_async]" )
{
    reset_combat_executor reset;
    npc &who = prepare_combat_follower();
    place_visible_zombie( who );
    npc &ally = spawn_npc( point_bub_ms{ 62, 61 }, "test_talker" );
    ally.name = "Sarah";
    ally.set_fac( faction_your_followers );
    ally.set_attitude( NPCATT_FOLLOW );
    refresh_visibility( who );
    who.regen_ai_cache();
    npc_ai::set_ai_request_executor_for_test(
    []( const std::string & ) {
        return npc_ai::ai_response{ true, "DECISION=SILENT", "" };
    },
    false );
    npc_ai::begin_ai_session();

    REQUIRE( npc_ai::process_combat_social( who ).request_queued );
    calendar::turn += 5_turns;
    ally.add_effect( effect_grabbed, 10_turns, body_part_arm_l, false, 1, true );
    refresh_visibility( who );
    who.regen_ai_cache();
    const npc_ai::combat_social_process_result urgent = npc_ai::process_combat_social( who );
    REQUIRE( urgent.request_queued );
    CHECK( urgent.event.type == npc_ai::combat_social_event_type::ally_grabbed );
    CHECK( urgent.event.target_id == ally.getID().get_value() );
    CHECK( urgent.event.target_name.find( "Sarah" ) != std::string::npos );
    CHECK( npc_ai::get_ai_request_queue().pending_count() <= 2 );
}

TEST_CASE( "combat_social_uses_anonymous_audible_evidence_outside_los",
           "[npc_ai][npc_ai_combat_social]" )
{
    npc &who = prepare_combat_follower();
    map &here = get_map();
    const tripoint_bub_ms origin = who.pos_bub( here );
    const tripoint_bub_ms hidden_sound{ origin.x() + 4, origin.y(), origin.z() };
    here.ter_set( tripoint_bub_ms{ origin.x() + 1, origin.y(), origin.z() }, ter_wall_metal );
    refresh_visibility( who );
    who.rules.clear_flag( ally_rule::ignore_noise );
    REQUIRE_FALSE( who.sees( here, hidden_sound ) );
    who.handle_sound( sounds::sound_t::alarm, "alarma", 30, hidden_sound );

    const npc_ai::combat_perception_snapshot snapshot =
        npc_ai::build_combat_perception_snapshot( who );
    REQUIRE( !snapshot.audible_events.empty() );
    CHECK( snapshot.audible_events.front().kind == "alarma" );
    npc_ai::combat_social_event event;
    event.type = npc_ai::combat_social_event_type::combat_start;
    event.detail = "Se oyo combate fuera de vision.";
    const std::string prompt = npc_ai::build_combat_social_prompt( who, snapshot, event );
    CHECK( prompt.find( "HEARD ONLY: alarma" ) != std::string::npos );
    CHECK( prompt.find( "source identity is unknown" ) != std::string::npos );
}

TEST_CASE( "combat_social_confirmed_ordinary_enemy_death_becomes_an_event",
           "[npc_ai][npc_ai_combat_social][npc_ai_async]" )
{
    reset_combat_executor reset;
    npc &who = prepare_combat_follower();
    const tripoint_bub_ms origin = who.pos_bub( get_map() );
    monster &zombie = spawn_test_monster(
                          "mon_zombie", tripoint_bub_ms{ origin.x() + 3, origin.y(), origin.z() } );
    refresh_visibility( who );
    who.regen_ai_cache();
    npc_ai::set_ai_request_executor_for_test(
    []( const std::string & ) {
        return npc_ai::ai_response{ true, "DECISION=SILENT", "" };
    } );
    npc_ai::begin_ai_session();
    REQUIRE( npc_ai::process_combat_social( who ).request_queued );
    REQUIRE( npc_ai::get_ai_request_queue().wait_until_idle_for_test( 1s ) );
    npc_ai::process_ai_completions();

    calendar::turn += 30_turns;
    zombie.die( &get_map(), &get_avatar() );
    g->remove_zombie( zombie );
    refresh_visibility( who );
    who.regen_ai_cache();
    const npc_ai::combat_social_process_result result = npc_ai::process_combat_social( who );
    REQUIRE( result.event_detected );
    CHECK( result.event.type == npc_ai::combat_social_event_type::enemy_killed );
    CHECK( result.event.detail.find( "acaba de morir" ) != std::string::npos );
}

TEST_CASE( "recent_combat_speech_deduplication_covers_self_and_other_speakers",
           "[npc_ai][npc_ai_combat_social][npc_ai_memory]" )
{
    npc &liam = prepare_combat_follower();
    npc &kim = spawn_npc( point_bub_ms{ 62, 60 }, "test_talker" );
    kim.name = "Kim";
    kim.set_fac( faction_your_followers );
    kim.set_attitude( NPCATT_FOLLOW );

    npc_ai::remember_recent_speech( liam, "¡Tenemos que movernos!",
                                    "necesidad real de retirarse o moverse" );
    CHECK( npc_ai::recent_speech_is_duplicate( liam, "tenemos que movernos" ) );
    CHECK( npc_ai::recent_speech_is_duplicate( liam, "Liam, tenemos que movernos ahora" ) );
    CHECK( npc_ai::recent_speech_is_duplicate( kim, "tenemos que movernos" ) );
    CHECK_FALSE( npc_ai::recent_speech_is_duplicate( kim, "Muévete, no puedo pasar." ) );
    CHECK( npc_ai::build_recent_speech_context( liam ).find( "Tenemos que movernos" ) !=
           std::string::npos );
}

TEST_CASE( "combat_social_output_filters_mixed_language_and_false_promises",
           "[npc_ai][npc_ai_combat_social][npc_ai_language]" )
{
    CHECK_FALSE( npc_ai::generated_text_matches_dialogue_language(
                     "This is bad, it's a boomer enorme al suroeste.", "es_ES" ) );
    CHECK_FALSE( npc_ai::generated_text_matches_dialogue_language(
                     "Seems like the coast is clear... for now.", "es_ES" ) );
    CHECK( npc_ai::generated_text_matches_dialogue_language(
               "Mierda, hay un boomer enorme al suroeste.", "es_ES" ) );
    CHECK( npc_ai::generated_text_matches_dialogue_language(
               "This is bad, it's a huge boomer southwest.", "en" ) );
    CHECK( npc_ai::combat_social_text_has_unconfirmed_tactical_promise( "¡Te cubro!" ) );
    CHECK( npc_ai::combat_social_text_has_unconfirmed_tactical_promise(
               "Quédate ahí, yo me encargo." ) );
    CHECK_FALSE( npc_ai::combat_social_text_has_unconfirmed_tactical_promise( "¡Cuidado!" ) );
}

TEST_CASE( "combat_social_director_coalesces_a_shared_normal_event",
           "[npc_ai][npc_ai_combat_social][npc_ai_async]" )
{
    reset_combat_executor reset;
    npc &liam = prepare_combat_follower();
    npc &kim = spawn_npc( point_bub_ms{ 61, 61 }, "test_talker" );
    kim.name = "Kim";
    kim.set_fac( faction_your_followers );
    kim.set_attitude( NPCATT_FOLLOW );
    place_visible_zombie( liam );
    refresh_visibility( kim );
    liam.regen_ai_cache();
    kim.regen_ai_cache();
    npc_ai::set_ai_request_executor_for_test(
    []( const std::string & ) {
        return npc_ai::ai_response{ true, "DECISION=SILENT", "" };
    },
    false );
    npc_ai::begin_ai_session();

    const bool liam_queued = npc_ai::process_combat_social( liam ).request_queued;
    const bool kim_queued = npc_ai::process_combat_social( kim ).request_queued;
    CHECK( liam_queued != kim_queued );
    CHECK( npc_ai::get_ai_request_queue().pending_count() == 1 );
}

TEST_CASE( "combat_social_director_limits_a_shared_event_to_one_request_at_20_observers",
           "[npc_ai][npc_ai_combat_social][npc_ai_async][npc_ai_scaling]" )
{
    reset_combat_executor reset;
    npc &first = prepare_combat_follower();
    std::vector<npc *> followers{ &first };
    for( int index = 0; index < 19; ++index ) {
        npc &ally = spawn_npc( point_bub_ms{ 52 + index, 62 }, "test_talker" );
        ally.name = "Social director ally " + std::to_string( index );
        ally.set_fac( faction_your_followers );
        ally.set_attitude( NPCATT_FOLLOW );
        followers.push_back( &ally );
    }

    place_visible_zombie( first );
    for( npc *ally : followers ) {
        refresh_visibility( *ally );
        ally->regen_ai_cache();
        npc_ai::reset_combat_social_state_for_test( *ally );
    }
    npc_ai::set_ai_request_executor_for_test(
    []( const std::string & ) {
        return npc_ai::ai_response{ true, "DECISION=SILENT", "" };
    },
    false );
    npc_ai::begin_ai_session();

    int requests_queued = 0;
    for( npc *ally : followers ) {
        requests_queued += npc_ai::process_combat_social( *ally ).request_queued ? 1 : 0;
    }

    INFO( "observers=" << followers.size() );
    CHECK( requests_queued == 1 );
    CHECK( npc_ai::get_ai_request_queue().pending_count() == 1 );
}

TEST_CASE( "combat_social_long_encounter_has_no_small_global_or_per_npc_speech_budget",
           "[npc_ai][npc_ai_combat_social][npc_ai_async]" )
{
    reset_combat_executor reset;
    npc &who = prepare_combat_follower();
    place_visible_zombie( who );
    refresh_visibility( who );
    who.regen_ai_cache();
    std::atomic<int> response_number{ 0 };
    npc_ai::set_ai_request_executor_for_test( [&]( const std::string & ) {
        const int number = response_number.fetch_add( 1 ) + 1;
        return npc_ai::ai_response{ true,
                                    "DECISION=TALK\nTEXT=Intervención número " +
                                    std::to_string( number ) + ".", "" };
    } );
    npc_ai::begin_ai_session();

    int event_number = 0;
    const auto speak_for_event = [&]() {
        CAPTURE( ++event_number );
        const npc_ai::combat_social_process_result result = npc_ai::process_combat_social( who );
        REQUIRE( result.request_queued );
        REQUIRE( npc_ai::get_ai_request_queue().wait_until_idle_for_test( 1s ) );
        npc_ai::process_ai_completions();
    };

    speak_for_event();
    calendar::turn += 30_turns;
    who.apply_damage( nullptr, body_part_arm_l, 5, true );
    refresh_visibility( who );
    who.regen_ai_cache();
    speak_for_event();
    calendar::turn += 30_turns;
    get_avatar().apply_damage( nullptr, body_part_arm_l, 5, true );
    refresh_visibility( who );
    who.regen_ai_cache();
    speak_for_event();
    calendar::turn += 30_turns;
    spawn_test_monster( "mon_zombie_dog", tripoint_bub_ms{ 65, 60, 0 } );
    refresh_visibility( who );
    who.regen_ai_cache();
    speak_for_event();
    npc &ally = spawn_npc( point_bub_ms{ 61, 61 }, "test_talker" );
    ally.name = "Kim";
    refresh_visibility( who );
    who.regen_ai_cache();
    CHECK_FALSE( npc_ai::process_combat_social( who ).request_queued );
    calendar::turn += 30_turns;
    ally.apply_damage( nullptr, body_part_arm_l, 5, true );
    refresh_visibility( who );
    who.regen_ai_cache();
    speak_for_event();

    CHECK( response_number == 5 );
}

TEST_CASE( "npc_to_npc_spontaneous_reply_is_allowed_once_and_cannot_loop",
           "[npc_ai][npc_ai_combat_social][npc_ai_async]" )
{
    reset_combat_executor reset;
    npc &liam = prepare_combat_follower();
    npc &kim = spawn_npc( point_bub_ms{ 62, 60 }, "test_talker" );
    kim.name = "Kim";
    kim.set_fac( faction_your_followers );
    kim.set_attitude( NPCATT_FOLLOW );
    refresh_visibility( liam );
    refresh_visibility( kim );
    npc_ai::set_ai_request_executor_for_test( []( const std::string & ) {
        return npc_ai::ai_response{ true, "DECISION=TALK\nTEXT=Te entiendo.", "" };
    } );
    npc_ai::begin_ai_session();

    REQUIRE( npc_ai::maybe_enqueue_npc_reply(
                 liam, "Esto se está poniendo feo.",
                 npc_ai::conversation_origin::npc_initiated_social, 77, 0 ) );
    CHECK_FALSE( npc_ai::maybe_enqueue_npc_reply(
                     liam, "Otra réplica.",
                     npc_ai::conversation_origin::npc_initiated_social, 77, 1 ) );
    REQUIRE( npc_ai::get_ai_request_queue().wait_until_idle_for_test( 1s ) );
    npc_ai::process_ai_completions();
    CHECK( npc_ai::get_ai_request_queue().pending_count() == 0 );
    CHECK( npc_ai::build_memory_context( kim ).find( "Te entiendo" ) != std::string::npos );
}

TEST_CASE( "combat_end_completion_is_discarded_if_a_threat_is_visible_again",
           "[npc_ai][npc_ai_combat_social][npc_ai_async]" )
{
    reset_combat_executor reset;
    npc &who = prepare_combat_follower();
    place_visible_zombie( who );
    std::atomic<int> calls{ 0 };
    const std::string marker = "FIN_FALSO_" + random_string( 20 );
    npc_ai::set_ai_request_executor_for_test( [&]( const std::string & ) {
        if( calls.fetch_add( 1 ) + 1 == 1 ) {
            return npc_ai::ai_response{ true, "DECISION=SILENT", "" };
        }
        return npc_ai::ai_response{ true,
                                    "DECISION=TALK\nTEXT=Parece que la zona está despejada. " + marker,
                                    "" };
    } );
    npc_ai::begin_ai_session();
    REQUIRE( npc_ai::process_combat_social( who ).request_queued );
    REQUIRE( npc_ai::get_ai_request_queue().wait_until_idle_for_test( 1s ) );
    npc_ai::process_ai_completions();

    calendar::turn += 30_turns;
    g->clear_zombies();
    refresh_visibility( who );
    who.regen_ai_cache();
    const npc_ai::combat_social_process_result ended = npc_ai::process_combat_social( who );
    REQUIRE( ended.request_queued );
    REQUIRE( ended.event.type == npc_ai::combat_social_event_type::combat_end );
    REQUIRE( npc_ai::get_ai_request_queue().wait_until_idle_for_test( 1s ) );

    place_visible_zombie( who );
    CHECK_FALSE( npc_ai::combat_social_situation_is_clear( who ) );
    npc_ai::process_ai_completions();
    CHECK( npc_ai::build_memory_context( who ).find( marker ) == std::string::npos );
}

TEST_CASE( "combat_social_discards_named_enemy_that_died_while_response_was_pending",
           "[npc_ai][npc_ai_combat_social][npc_ai_async]" )
{
    reset_combat_executor reset;
    npc &who = prepare_combat_follower();
    const tripoint_bub_ms origin = who.pos_bub( get_map() );
    monster &brute = spawn_test_monster(
                         "mon_zombie_brute", tripoint_bub_ms{ origin.x() + 3, origin.y(), origin.z() } );
    spawn_test_monster( "mon_zombie", tripoint_bub_ms{ origin.x() + 4, origin.y(), origin.z() } );
    refresh_visibility( who );
    who.regen_ai_cache();
    const std::string brute_name = brute.disp_name();
    const std::string marker = "STALE_NAMED_ENEMY_" + random_string( 20 );
    npc_ai::set_ai_request_executor_for_test( [&]( const std::string & ) {
        return npc_ai::ai_response{ true,
                                    "DECISION=TALK\nTEXT=¡Ese " + brute_name + " sigue vivo! " + marker,
                                    "" };
    } );
    npc_ai::begin_ai_session();

    REQUIRE( npc_ai::process_combat_social( who ).request_queued );
    REQUIRE( npc_ai::get_ai_request_queue().wait_until_idle_for_test( 1s ) );
    brute.die( &get_map(), &get_avatar() );
    g->remove_zombie( brute );
    refresh_visibility( who );
    who.regen_ai_cache();
    npc_ai::process_ai_completions();
    CHECK( npc_ai::build_memory_context( who ).find( marker ) == std::string::npos );
}

TEST_CASE( "combat_social_coalesces_own_grab_escape_and_drag_and_revalidates_it",
           "[npc_ai][npc_ai_combat_social][npc_ai_event_stream][npc_ai_async]" )
{
    reset_combat_executor reset;
    npc &who = prepare_combat_follower();
    monster &grabber = place_visible_zombie( who, 2 );
    const std::string marker = "STALE_DRAG_" + random_string( 20 );
    npc_ai::set_ai_request_executor_for_test( [&]( const std::string & prompt ) {
        if( prompt.find( "type=COMBAT_START" ) != std::string::npos ) {
            return npc_ai::ai_response{ true, "DECISION=SILENT", "" };
        }
        return npc_ai::ai_response{ true, "DECISION=TALK\nTEXT=\u00a1Ayuda, me est\u00e1 arrastrando! " + marker,
                                    "" };
    } );
    npc_ai::begin_ai_session();
    REQUIRE( npc_ai::process_combat_social( who ).request_queued );
    REQUIRE( npc_ai::get_ai_request_queue().wait_until_idle_for_test( 1s ) );
    npc_ai::process_ai_completions();

    who.add_effect( effect_grabbed, 10_turns, body_part_arm_l, false, 1, true );
    const auto record_for_observer = [&]( const npc_ai::world_event_type type,
                                          const Creature * actor, const Creature * target,
    const int importance, const std::string & detail ) {
        npc_ai::world_event event;
        event.type = type;
        event.actor = npc_ai::snapshot_entity( actor );
        event.target = npc_ai::snapshot_entity( target );
        event.importance = importance;
        event.confirmed_outcome = true;
        event.source = "combat_social_test";
        event.detail = detail;
        event.known_by_npc_ids.push_back( who.getID().get_value() );
        npc_ai::record_world_event( std::move( event ) );
    };
    record_for_observer( npc_ai::world_event_type::npc_grabbed, &grabber, &who, 100,
                         "El zombi agarr\u00f3 a Liam." );
    record_for_observer( npc_ai::world_event_type::failed_escape, &who, &grabber, 100,
                         "Liam intent\u00f3 soltarse y fall\u00f3." );
    record_for_observer( npc_ai::world_event_type::dragged, &grabber, &who, 101,
                         "El zombi est\u00e1 arrastrando a Liam." );
    refresh_visibility( who );
    who.regen_ai_cache();

    const npc_ai::combat_social_process_result result = npc_ai::process_combat_social( who );
    REQUIRE( result.request_queued );
    CHECK( result.event.type == npc_ai::combat_social_event_type::dragged );
    CHECK( result.event.encounter_generation > 0 );
    CHECK( result.event.coalesced_sequences.size() >= 2 );
    CHECK( result.event.detail.find( "fall\u00f3" ) != std::string::npos );
    REQUIRE( npc_ai::get_ai_request_queue().wait_until_idle_for_test( 1s ) );

    who.remove_effect( effect_grabbed );
    refresh_visibility( who );
    who.regen_ai_cache();
    npc_ai::process_ai_completions();
    CHECK( npc_ai::build_memory_context( who ).find( marker ) == std::string::npos );

    // A later tile of the same continuing drag keeps the victim identity even
    // though positions changed, so it is deduplicated rather than speaking per tile.
    calendar::turn += 1_turns;
    who.add_effect( effect_grabbed, 10_turns, body_part_arm_l, false, 1, true );
    who.setpos( get_map(), who.pos_bub( get_map() ) + tripoint::south );
    record_for_observer( npc_ai::world_event_type::dragged, &grabber, &who, 101,
                         "El zombi sigue arrastrando a Liam." );
    refresh_visibility( who );
    who.regen_ai_cache();
    const npc_ai::combat_social_process_result repeated = npc_ai::process_combat_social( who );
    CHECK( repeated.event_detected );
    CHECK_FALSE( repeated.request_queued );
}

TEST_CASE( "combat_social_batches_fixed_five_fact_trace_into_four_scheduled_lines",
           "[npc_ai][npc_ai_combat_social][npc_ai_batch]" )
{
    reset_combat_executor reset;
    npc &liam = prepare_combat_follower();
    npc &sarah = spawn_npc( point_bub_ms{ 62, 60 }, "test_talker" );
    sarah.name = "Sarah";
    sarah.set_fac( faction_your_followers );
    sarah.set_attitude( NPCATT_FOLLOW );
    npc &miguel = spawn_npc( point_bub_ms{ 63, 60 }, "test_talker" );
    miguel.name = "Miguel";
    miguel.set_fac( faction_your_followers );
    miguel.set_attitude( NPCATT_FOLLOW );
    place_visible_zombie( liam, 5 );
    sarah.add_effect( effect_grabbed, 30_turns, body_part_arm_l, false, 1, true );
    for( npc *ally : { &liam, &sarah, &miguel } ) {
        refresh_visibility( *ally );
        ally->regen_ai_cache();
    }

    std::atomic<int> batch_calls{ 0 };
    std::vector<std::uint64_t> trace_ids;
    std::mutex response_mutex;
    npc_ai::set_ai_request_executor_for_test(
    [&]( const std::string & prompt, const std::string & ) {
        if( prompt.find( "CPP_FIXED_SLOTS" ) == std::string::npos ) {
            return npc_ai::ai_response{ true, "DECISION=SILENT", "" };
        }
        batch_calls.fetch_add( 1 );
        std::lock_guard<std::mutex> lock( response_mutex );
        if( trace_ids.size() != 5 ) {
            return npc_ai::ai_response{ false, "", "fixed trace was incomplete" };
        }
        return npc_ai::ai_response{
            true,
            "{\"candidates\":["
            "{\"slot\":0,\"event_ids\":[" + std::to_string( trace_ids[0] ) +
            "],\"claim_level\":\"HIT_ONLY\",\"text\":\"¡Buen golpe!\"},"
            "{\"slot\":1,\"event_ids\":[" + std::to_string( trace_ids[1] ) +
            "],\"claim_level\":\"FACT_ONLY\",\"text\":\"¡Sarah, te agarró!\"},"
            "{\"slot\":2,\"event_ids\":[" + std::to_string( trace_ids[2] ) +
            "],\"claim_level\":\"DEATH_CONFIRMED\",\"text\":\"¡Uno menos!\"},"
            "{\"slot\":3,\"event_ids\":[" + std::to_string( trace_ids[3] ) +
            "],\"claim_level\":\"HIT_ONLY\",\"text\":\"¡Sarah está herida!\"}]}"
        };
    } );
    npc_ai::begin_ai_session();

    // Initialise every speaker against the same real combat state, then erase
    // those setup-only measurements.  The fixed trace below is the sole A/B
    // input represented by tests/data/npc_ai_combat_social_trace.json.
    for( npc *ally : { &liam, &sarah, &miguel } ) {
        npc_ai::process_combat_social( *ally );
    }
    REQUIRE( npc_ai::get_ai_request_queue().wait_until_idle_for_test( 1s ) );
    npc_ai::process_ai_completions();
    npc_ai::reset_combat_social_metrics();

    const int base_turn = to_turn<int>( calendar::turn );
    const std::vector<int> witnesses = {
        liam.getID().get_value(), sarah.getID().get_value(), miguel.getID().get_value()
    };
    const auto entity = []( const npc &who ) {
        npc_ai::world_entity_snapshot result = npc_ai::snapshot_entity( &who );
        result.kind = "npc";
        return result;
    };
    const auto monster_entity = []( const std::string &name ) {
        npc_ai::world_entity_snapshot result;
        result.kind = "monster";
        result.name = name;
        return result;
    };
    const auto record = [&]( const int offset, const npc_ai::world_event_type type,
                             npc_ai::world_entity_snapshot actor,
                             npc_ai::world_entity_snapshot target, const int importance,
                             const npc_ai::world_event_claim_level claim,
                             const std::string &detail ) {
        npc_ai::world_event fact;
        fact.game_turn = base_turn + offset;
        fact.type = type;
        fact.actor = std::move( actor );
        fact.target = std::move( target );
        fact.importance = importance;
        fact.confirmed_outcome = true;
        fact.claim_level = claim;
        fact.source = "fixed_five_fact_trace";
        fact.detail = detail;
        fact.known_by_npc_ids = witnesses;
        trace_ids.push_back( npc_ai::record_world_event( std::move( fact ) ) );
    };
    record( 0, npc_ai::world_event_type::npc_attack, entity( liam ),
            monster_entity( "zombi A" ), 76,
            npc_ai::world_event_claim_level::hit_confirmed,
            "Liam golpea al zombi A en el torso." );
    record( 1, npc_ai::world_event_type::npc_grabbed, monster_entity( "zombi B" ),
            entity( sarah ), 100, npc_ai::world_event_claim_level::fact_only,
            "El zombi B agarra a Sarah." );
    record( 2, npc_ai::world_event_type::enemy_killed, entity( miguel ),
            monster_entity( "zombi C" ), 84,
            npc_ai::world_event_claim_level::death_confirmed,
            "Miguel mata al zombi C." );
    record( 3, npc_ai::world_event_type::npc_attack, monster_entity( "zombi D" ),
            entity( sarah ), 82, npc_ai::world_event_claim_level::hit_confirmed,
            "Sarah recibe dano en el brazo izquierdo." );
    record( 4, npc_ai::world_event_type::enemy_killed, entity( liam ),
            monster_entity( "zombi B" ), 84,
            npc_ai::world_event_claim_level::death_confirmed,
            "Liam mata al zombi que agarraba a Sarah." );
    calendar::turn += 4_turns;

    const npc_ai::combat_social_process_result queued =
        npc_ai::process_combat_social( liam );
    REQUIRE( queued.request_queued );
    REQUIRE( npc_ai::get_ai_request_queue().wait_until_idle_for_test( 1s ) );
    npc_ai::process_ai_completions();
    CHECK( batch_calls.load() == 1 );
    CHECK( npc_ai::combat_social_metrics_snapshot().candidates_validated == 4 );

    for( int second = 0; second < 3; ++second ) {
        calendar::turn += 1_turns;
        for( npc *ally : { &liam, &sarah, &miguel } ) {
            npc_ai::process_combat_social( *ally );
        }
    }
    const npc_ai::combat_social_metrics metrics = npc_ai::combat_social_metrics_snapshot();
    CHECK( metrics.inferences_queued == 1 );
    CHECK( metrics.lines_emitted == 4 );
    CHECK( metrics.narrable_events_captured == 5 );
    CHECK( metrics.narrable_events_verbalized == 4 );
    CHECK( metrics.lines_by_speaker.size() == 3 );
}

TEST_CASE( "combat_social_batch_prompt_contains_only_facts_shared_by_its_cpp_cohort",
           "[npc_ai][npc_ai_combat_social][npc_ai_batch][npc_ai_knowledge]" )
{
    reset_combat_executor reset;
    npc &liam = prepare_combat_follower();
    npc &sarah = spawn_npc( point_bub_ms{ 62, 60 }, "test_talker" );
    sarah.name = "Sarah";
    sarah.set_fac( faction_your_followers );
    sarah.set_attitude( NPCATT_FOLLOW );
    place_visible_zombie( liam, 5 );
    for( npc *ally : { &liam, &sarah } ) {
        refresh_visibility( *ally );
        ally->regen_ai_cache();
    }

    std::mutex prompts_mutex;
    std::vector<std::string> batch_prompts;
    npc_ai::set_ai_request_executor_for_test(
    [&]( const std::string & prompt, const std::string & ) {
        if( prompt.find( "CPP_FIXED_SLOTS" ) != std::string::npos ) {
            std::lock_guard<std::mutex> lock( prompts_mutex );
            batch_prompts.push_back( prompt );
            return npc_ai::ai_response{ true, "{\"candidates\":[]}", "" };
        }
        return npc_ai::ai_response{ true, "DECISION=SILENT", "" };
    } );
    npc_ai::begin_ai_session();
    for( npc *ally : { &liam, &sarah } ) {
        npc_ai::process_combat_social( *ally );
    }
    REQUIRE( npc_ai::get_ai_request_queue().wait_until_idle_for_test( 1s ) );
    npc_ai::process_ai_completions();
    npc_ai::reset_combat_social_metrics();

    const auto record = [&]( const std::string &detail, const std::vector<int> &witnesses ) {
        npc_ai::world_event fact;
        fact.type = npc_ai::world_event_type::attack_missed;
        fact.actor = npc_ai::snapshot_entity( &liam );
        fact.target.kind = "monster";
        fact.target.name = "zombi";
        fact.importance = 66;
        fact.confirmed_outcome = true;
        fact.source = "knowledge_cohort_test";
        fact.detail = detail;
        fact.known_by_npc_ids = witnesses;
        npc_ai::record_world_event( std::move( fact ) );
    };
    record( "PRIVATE_LIAM_FACT", { liam.getID().get_value() } );
    record( "SHARED_LIAM_SARAH_FACT",
            { liam.getID().get_value(), sarah.getID().get_value() } );
    calendar::turn += 1_turns;

    REQUIRE( npc_ai::process_combat_social( liam ).request_queued );
    REQUIRE( npc_ai::get_ai_request_queue().wait_until_idle_for_test( 1s ) );
    npc_ai::process_ai_completions();
    REQUIRE( batch_prompts.size() == 2 );
    const std::string sarah_id = "speaker_id=" +
                                 std::to_string( sarah.getID().get_value() );
    int prompts_with_private = 0;
    for( const std::string &prompt : batch_prompts ) {
        CHECK( prompt.find( "SHARED_LIAM_SARAH_FACT" ) != std::string::npos );
        if( prompt.find( "PRIVATE_LIAM_FACT" ) != std::string::npos ) {
            ++prompts_with_private;
            CHECK( prompt.find( sarah_id ) == std::string::npos );
        }
    }
    CHECK( prompts_with_private == 1 );
    CHECK( npc_ai::combat_social_metrics_snapshot().discarded_knowledge == 0 );
}

TEST_CASE( "combat_social_detection_never_waits_for_ollama",
           "[npc_ai][npc_ai_combat_social][npc_ai_async]" )
{
    reset_combat_executor reset;
    npc &who = prepare_combat_follower();
    place_visible_zombie( who );
    std::mutex mutex;
    std::condition_variable cv;
    bool entered = false;
    bool release = false;
    npc_ai::set_ai_request_executor_for_test( [&]( const std::string & ) {
        std::unique_lock<std::mutex> lock( mutex );
        entered = true;
        cv.notify_all();
        cv.wait( lock, [&]() {
            return release;
        } );
        return npc_ai::ai_response{ true, "DECISION=SILENT", "" };
    } );
    npc_ai::begin_ai_session();

    REQUIRE( npc_ai::process_combat_social( who ).request_queued );
    {
        std::unique_lock<std::mutex> lock( mutex );
        REQUIRE( cv.wait_for( lock, 1s, [&]() {
            return entered;
        } ) );
    }
    CHECK( npc_ai::get_ai_request_queue().take_completions( 1 ).empty() );
    {
        std::lock_guard<std::mutex> lock( mutex );
        release = true;
    }
    cv.notify_all();
    REQUIRE( npc_ai::get_ai_request_queue().wait_until_idle_for_test( 1s ) );
}
