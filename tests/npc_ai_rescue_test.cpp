#include "cata_catch.h"

#include <algorithm>
#include <string>
#include <vector>

#include "bodypart.h"
#include "calendar.h"
#include "creature_tracker.h"
#include "effect.h"
#include "field_type.h"
#include "game.h"
#include "item.h"
#include "map.h"
#include "map_helpers.h"
#include "monster.h"
#include "npc.h"
#include "npc_ai_async.h"
#include "npc_ai_goal.h"
#include "npc_ai_rescue.h"
#include "npctalk.h"
#include "player_activity.h"
#include "player_helpers.h"
#include "point.h"
#include "trap.h"
#include "type_id.h"
#include "units.h"

namespace
{

static const efftype_id effect_grabbed( "grabbed" );
static const efftype_id effect_narcosis( "narcosis" );
static const efftype_id effect_sleep( "sleep" );
static const activity_id ACT_OPERATION( "ACT_OPERATION" );
static const faction_id faction_your_followers( "your_followers" );
static const ter_str_id ter_door_closed( "t_door_c" );
static const ter_str_id ter_door_open( "t_door_o" );
static const ter_str_id ter_wall( "t_wall" );
static const trap_str_id trap_beartrap( "tr_beartrap" );
static const vproto_id vehicle_bicycle( "bicycle" );

struct rescue_scene {
    npc *rescuer;
    npc *casualty;
    tripoint_bub_ms casualty_from;
    tripoint_bub_ms rescuer_from;
    tripoint_bub_ms rescuer_dest;
};

struct reset_request_executor {
    ~reset_request_executor() {
        npc_ai::reset_ai_request_system_for_test();
    }
};

void break_legs( npc &who, int count )
{
    for( const bodypart_id &leg : who.get_all_body_parts_of_type(
             body_part_type::type::leg, get_body_part_flags::primary_type ) ) {
        if( count-- <= 0 ) {
            break;
        }
        who.set_part_hp_cur( leg, 0 );
    }
}

rescue_scene prepare_rescue_scene()
{
    clear_map( -1, 1 );
    clear_avatar();
    set_time_to_day();
    npc_ai::clear_rescues_for_test();
    g->place_player( tripoint_bub_ms{ 60, 60, 0 } );
    // place_player/load_npcs can activate persisted test-world NPCs after the
    // first clear_map pass, so clear once more before placing this fixture.
    clear_npcs();

    map &here = get_map();
    const tripoint_bub_ms casualty_from{ 64, 65, 0 };
    const tripoint_bub_ms rescuer_from{ 65, 65, 0 };
    const tripoint_bub_ms rescuer_dest{ 66, 65, 0 };
    npc &rescuer = spawn_npc( rescuer_from.xy(), "test_talker" );
    npc &casualty = spawn_npc( casualty_from.xy(), "test_talker" );
    clear_character( rescuer );
    clear_character( casualty );
    rescuer.name = "Rescuer";
    casualty.name = "Casualty";
    rescuer.set_fac( faction_your_followers );
    casualty.set_fac( faction_your_followers );
    rescuer.set_attitude( NPCATT_FOLLOW );
    casualty.set_attitude( NPCATT_FOLLOW );
    rescuer.setpos( here, rescuer_from );
    casualty.setpos( here, casualty_from );
    npc_ai::clear_goals_for_test( rescuer );
    npc_ai::clear_goals_for_test( casualty );
    rescuer.str_max = 20;
    rescuer.str_cur = 20;
    rescuer.set_moves( 10000 );
    break_legs( casualty, 1 );
    REQUIRE( npc_ai::classify_casualty_mobility( casualty ) ==
             npc_ai::casualty_mobility::impaired );
    return { &rescuer, &casualty, casualty_from, rescuer_from, rescuer_dest };
}

void check_unchanged( const rescue_scene &scene )
{
    CHECK( scene.rescuer->pos_bub() == scene.rescuer_from );
    CHECK( scene.casualty->pos_bub() == scene.casualty_from );
}

tripoint_bub_ms rescue_destination()
{
    return tripoint_bub_ms{ 70, 65, 0 };
}

bool advance_until_attached( rescue_scene &scene )
{
    for( int turn = 0; turn < 30; ++turn ) {
        const std::optional<npc_ai::rescue_state> state =
            npc_ai::rescue_for_rescuer( *scene.rescuer );
        if( !state ) {
            return false;
        }
        if( state->phase == npc_ai::rescue_phase::attached ||
            state->phase == npc_ai::rescue_phase::dragging ) {
            return true;
        }
        scene.rescuer->set_moves( 10000 );
        const npc_ai::rescue_tick_result result = npc_ai::tick_rescue( *scene.rescuer );
        if( result == npc_ai::rescue_tick_result::cancelled ) {
            return false;
        }
    }
    return false;
}

} // namespace

TEST_CASE( "npc_rescue_classifies_mobility_without_using_total_hp_or_sleep",
           "[npc_ai][npc_ai_rescue]" )
{
    rescue_scene scene = prepare_rescue_scene();
    npc &casualty = *scene.casualty;

    for( const bodypart_id &part : casualty.get_all_body_parts() ) {
        if( part->primary_limb_type() != body_part_type::type::leg &&
            part->primary_limb_type() != body_part_type::type::foot ) {
            casualty.set_part_hp_cur( part, 1 );
        }
    }
    REQUIRE( casualty.hp_percentage() < 50 );
    for( const bodypart_id &leg : casualty.get_all_body_parts_of_type(
             body_part_type::type::leg, get_body_part_flags::primary_type ) ) {
        casualty.set_part_hp_cur( leg, casualty.get_part_hp_max( leg ) );
    }
    CHECK( casualty.enough_working_legs() );
    CHECK( npc_ai::classify_casualty_mobility( casualty ) ==
           npc_ai::casualty_mobility::can_walk );

    casualty.add_effect( effect_sleep, 10_turns );
    REQUIRE( casualty.in_sleep_state() );
    CHECK( npc_ai::classify_casualty_mobility( casualty ) ==
           npc_ai::casualty_mobility::can_walk );
}

TEST_CASE( "npc_rescue_classifies_narcosis_grabs_and_broken_legs",
           "[npc_ai][npc_ai_rescue]" )
{
    rescue_scene scene = prepare_rescue_scene();
    npc &casualty = *scene.casualty;

    casualty.add_effect( effect_narcosis, 10_turns );
    CHECK( npc_ai::classify_casualty_mobility( casualty ) ==
           npc_ai::casualty_mobility::needs_drag );
    casualty.remove_effect( effect_narcosis );

    casualty.add_effect( effect_grabbed, 10_turns );
    REQUIRE( casualty.has_effect_with_flag( json_character_flag( "GRAB" ) ) );
    CHECK( npc_ai::classify_casualty_mobility( casualty ) ==
           npc_ai::casualty_mobility::must_not_move );
    casualty.remove_effect( effect_grabbed );

    CHECK( npc_ai::classify_casualty_mobility( casualty ) ==
           npc_ai::casualty_mobility::impaired );
    break_legs( casualty, 2 );
    REQUIRE( casualty.get_working_leg_count() == 0 );
    CHECK( npc_ai::classify_casualty_mobility( casualty ) ==
           npc_ai::casualty_mobility::needs_drag );
}

TEST_CASE( "npc_rescue_drag_step_moves_pull_follow_pair_and_only_charges_rescuer",
           "[npc_ai][npc_ai_rescue]" )
{
    rescue_scene scene = prepare_rescue_scene();
    const int moves_before = scene.rescuer->get_moves();
    const int casualty_moves_before = scene.casualty->get_moves();
    const int stamina_before = scene.rescuer->get_stamina();

    const npc_ai::drag_step_result result = npc_ai::try_drag_step(
                *scene.rescuer, *scene.casualty, scene.rescuer_dest );

    REQUIRE( result.outcome == npc_ai::drag_step_outcome::moved );
    CHECK( result.move_cost > 0 );
    CHECK( scene.rescuer->pos_bub() == scene.rescuer_dest );
    CHECK( scene.casualty->pos_bub() == scene.rescuer_from );
    CHECK( rl_dist( scene.rescuer->pos_bub(), scene.casualty->pos_bub() ) == 1 );
    CHECK( scene.rescuer->pos_bub() != scene.casualty->pos_bub() );
    CHECK( scene.rescuer->get_moves() == moves_before - result.move_cost );
    CHECK( scene.casualty->get_moves() == casualty_moves_before );
    CHECK( scene.rescuer->get_stamina() == stamina_before );
    CHECK( get_creature_tracker().creature_at<npc>( scene.rescuer_dest ) == scene.rescuer );
    CHECK( get_creature_tracker().creature_at<npc>( scene.rescuer_from ) == scene.casualty );
    CHECK( get_creature_tracker().creature_at<npc>( scene.casualty_from ) == nullptr );
}

TEST_CASE( "npc_rescue_drag_step_supports_repositioned_ninety_degree_turn",
           "[npc_ai][npc_ai_rescue]" )
{
    rescue_scene scene = prepare_rescue_scene();
    REQUIRE( npc_ai::try_drag_step( *scene.rescuer, *scene.casualty,
                                    scene.rescuer_dest ) );

    map &here = get_map();
    const tripoint_bub_ms north_staging{ 65, 64, 0 };
    const tripoint_bub_ms north_dest{ 65, 63, 0 };
    scene.rescuer->setpos( here, north_staging );
    const npc_ai::drag_step_result turn = npc_ai::try_drag_step(
            *scene.rescuer, *scene.casualty, north_dest );

    REQUIRE( turn.outcome == npc_ai::drag_step_outcome::moved );
    CHECK( scene.rescuer->pos_bub() == north_dest );
    CHECK( scene.casualty->pos_bub() == north_staging );
}

TEST_CASE( "npc_rescue_open_door_is_a_separate_non_moving_tick",
           "[npc_ai][npc_ai_rescue]" )
{
    rescue_scene scene = prepare_rescue_scene();
    map &here = get_map();
    here.ter_set( scene.rescuer_dest, ter_door_closed );
    const int moves_before = scene.rescuer->get_moves();

    const npc_ai::drag_step_result blocked = npc_ai::try_drag_step(
            *scene.rescuer, *scene.casualty, scene.rescuer_dest );
    CHECK( blocked.outcome == npc_ai::drag_step_outcome::rejected );
    check_unchanged( scene );

    const npc_ai::drag_step_result opened = npc_ai::try_open_drag_door(
            *scene.rescuer, *scene.casualty, scene.rescuer_dest );
    REQUIRE( opened.outcome == npc_ai::drag_step_outcome::opened_door );
    CHECK( here.ter( scene.rescuer_dest ).obj().id == ter_door_open->id );
    CHECK( scene.rescuer->get_moves() < moves_before );
    check_unchanged( scene );

    REQUIRE( npc_ai::try_drag_step( *scene.rescuer, *scene.casualty,
                                    scene.rescuer_dest ) );
}

TEST_CASE( "npc_rescue_rejects_impassable_occupied_and_cross_z_steps_atomically",
           "[npc_ai][npc_ai_rescue]" )
{
    SECTION( "wall" ) {
        rescue_scene scene = prepare_rescue_scene();
        get_map().ter_set( scene.rescuer_dest, ter_wall );
        CHECK_FALSE( npc_ai::try_drag_step( *scene.rescuer, *scene.casualty,
                                            scene.rescuer_dest ) );
        check_unchanged( scene );
    }

    SECTION( "occupied" ) {
        rescue_scene scene = prepare_rescue_scene();
        npc &blocker = spawn_npc( scene.rescuer_dest.xy(), "test_talker" );
        blocker.setpos( get_map(), scene.rescuer_dest );
        CHECK_FALSE( npc_ai::try_drag_step( *scene.rescuer, *scene.casualty,
                                            scene.rescuer_dest ) );
        check_unchanged( scene );
    }

    SECTION( "different z-level" ) {
        rescue_scene scene = prepare_rescue_scene();
        const tripoint_bub_ms other_z{ scene.rescuer_dest.xy(), 1 };
        CHECK_FALSE( npc_ai::try_drag_step( *scene.rescuer, *scene.casualty, other_z ) );
        check_unchanged( scene );
    }
}

TEST_CASE( "npc_rescue_rejects_every_field_trap_and_vehicle_tile_in_v1",
           "[npc_ai][npc_ai_rescue]" )
{
    SECTION( "field" ) {
        rescue_scene scene = prepare_rescue_scene();
        REQUIRE( get_map().add_field( scene.rescuer_dest, fd_fire, 1 ) );
        CHECK_FALSE( npc_ai::try_drag_step( *scene.rescuer, *scene.casualty,
                                            scene.rescuer_dest ) );
        check_unchanged( scene );
    }

    SECTION( "trap" ) {
        rescue_scene scene = prepare_rescue_scene();
        get_map().trap_set( scene.rescuer_dest, trap_beartrap.id() );
        REQUIRE_FALSE( get_map().tr_at( scene.rescuer_dest ).is_null() );
        CHECK_FALSE( npc_ai::try_drag_step( *scene.rescuer, *scene.casualty,
                                            scene.rescuer_dest ) );
        check_unchanged( scene );
    }

    SECTION( "vehicle" ) {
        rescue_scene scene = prepare_rescue_scene();
        REQUIRE( get_map().add_vehicle( vehicle_bicycle, scene.rescuer_dest,
                                       0_degrees, 0, 0 ) != nullptr );
        REQUIRE( get_map().veh_at( scene.rescuer_dest ) );
        CHECK_FALSE( npc_ai::try_drag_step( *scene.rescuer, *scene.casualty,
                                            scene.rescuer_dest ) );
        check_unchanged( scene );
        CHECK_FALSE( scene.casualty->in_vehicle );
    }
}

TEST_CASE( "npc_rescue_rejects_diagonal_corner_cutting_for_both_edges",
           "[npc_ai][npc_ai_rescue]" )
{
    rescue_scene scene = prepare_rescue_scene();
    map &here = get_map();
    scene.casualty_from = tripoint_bub_ms{ 64, 66, 0 };
    scene.rescuer_from = tripoint_bub_ms{ 65, 65, 0 };
    scene.rescuer_dest = tripoint_bub_ms{ 66, 64, 0 };
    scene.casualty->setpos( here, scene.casualty_from );
    scene.rescuer->setpos( here, scene.rescuer_from );
    here.ter_set( tripoint_bub_ms{ 66, 65, 0 }, ter_wall );
    here.ter_set( tripoint_bub_ms{ 65, 64, 0 }, ter_wall );

    const npc_ai::drag_step_result result = npc_ai::try_drag_step(
            *scene.rescuer, *scene.casualty, scene.rescuer_dest );
    CHECK( result.outcome == npc_ai::drag_step_outcome::rejected );
    CHECK( result.reason == "diagonal movement cuts a blocked corner" );
    check_unchanged( scene );
}

TEST_CASE( "npc_rescue_rejects_casualty_when_strength_deficit_is_eight",
           "[npc_ai][npc_ai_rescue]" )
{
    rescue_scene scene = prepare_rescue_scene();
    scene.rescuer->str_max = 1;
    scene.rescuer->str_cur = 1;
    scene.casualty->set_stored_kcal( 1500000 );
    REQUIRE( scene.casualty->get_weight() / 12_kilogram -
             scene.rescuer->get_arm_str() >= 8 );

    const int moves_before = scene.rescuer->get_moves();
    const npc_ai::drag_step_result result = npc_ai::try_drag_step(
            *scene.rescuer, *scene.casualty, scene.rescuer_dest );
    CHECK( result.outcome == npc_ai::drag_step_outcome::rejected );
    CHECK( result.reason == "casualty is too heavy" );
    CHECK( scene.rescuer->get_moves() == moves_before );
    check_unchanged( scene );
}

TEST_CASE( "npc_rescue_claim_is_exclusive_for_rescuer_and_casualty",
           "[npc_ai][npc_ai_rescue]" )
{
    rescue_scene scene = prepare_rescue_scene();
    map &here = get_map();
    npc &second_rescuer = spawn_npc( point_bub_ms{ 63, 65 }, "test_talker" );
    npc &second_casualty = spawn_npc( point_bub_ms{ 62, 65 }, "test_talker" );
    clear_character( second_rescuer );
    clear_character( second_casualty );
    break_legs( second_casualty, 1 );

    const tripoint_abs_ms destination = here.get_abs( scene.rescuer_dest );
    const tripoint_abs_ms anchor = here.get_abs( scene.rescuer_dest + tripoint::east );
    const std::optional<npc_ai::rescue_id> first = npc_ai::claim_rescue(
                *scene.rescuer, *scene.casualty, destination, anchor );
    REQUIRE( first );
    REQUIRE( npc_ai::rescue_for_rescuer( *scene.rescuer ) );
    REQUIRE( npc_ai::rescue_for_casualty( *scene.casualty ) );
    CHECK( npc_ai::rescue_for_rescuer( *scene.rescuer )->created_turn == calendar::turn );
    CHECK_FALSE( npc_ai::claim_rescue( second_rescuer, *scene.casualty,
                                       destination, anchor ) );
    CHECK_FALSE( npc_ai::claim_rescue( *scene.rescuer, second_casualty,
                                       destination, anchor ) );

    CHECK( npc_ai::release_rescue( *first, npc_ai::rescue_phase::complete ) );
    CHECK_FALSE( npc_ai::release_rescue( *first, npc_ai::rescue_phase::complete ) );
    CHECK_FALSE( npc_ai::rescue_for_rescuer( *scene.rescuer ) );
    CHECK_FALSE( npc_ai::rescue_for_casualty( *scene.casualty ) );
}

TEST_CASE( "npc_rescue_parser_intercepts_only_an_explicit_named_person_transfer",
           "[npc_ai][npc_ai_rescue]" )
{
    CHECK( npc_ai::parse_rescue_order( "Arrastra a Kim hasta aqui" ) );
    CHECK( npc_ai::parse_rescue_order( "Drag Kim over here" ) );
    CHECK( npc_ai::parse_rescue_order( "haul Kim to this spot" ) );

    const std::vector<std::string> vanilla_lines = {
        "lleva a Kim la mochila",
        "Kim, lleva el rifle",
        "dile a Kim que salga",
        "ve con Kim",
        "sigueme",
        "quedate aqui",
        "Move there, Kim",
        "recoge eso"
    };
    for( const std::string &line : vanilla_lines ) {
        CAPTURE( line );
        CHECK_FALSE( npc_ai::parse_rescue_order( line ) );
    }

    rescue_scene scene = prepare_rescue_scene();
    scene.casualty->name = "Kim";
    bool selected_destination = false;
    const npc_ai::rescue_command_result item_order = npc_ai::execute_rescue_order(
                { scene.rescuer }, "arrastra la mochila, Kim", [&]() {
        selected_destination = true;
        return std::optional<tripoint_bub_ms>( rescue_destination() );
    } );
    CHECK_FALSE( item_order.handled );
    CHECK_FALSE( selected_destination );
}

TEST_CASE( "npc_rescue_order_creates_one_transactional_emergency_goal_and_coupled_paths",
           "[npc_ai][npc_ai_rescue]" )
{
    rescue_scene scene = prepare_rescue_scene();
    scene.casualty->name = "Kim";
    npc &second_rescuer = spawn_npc( point_bub_ms{ 63, 63 }, "test_talker" );
    clear_character( second_rescuer );
    second_rescuer.name = "Second rescuer";
    second_rescuer.set_fac( faction_your_followers );
    second_rescuer.set_attitude( NPCATT_FOLLOW );
    npc_ai::clear_goals_for_test( second_rescuer );

    const std::size_t pending_before = npc_ai::get_ai_request_queue().pending_count();
    const npc_ai::rescue_command_result result = npc_ai::execute_rescue_order(
                { scene.rescuer, &second_rescuer, scene.casualty },
                "arrastra a Kim hasta aqui", []() {
        return std::optional<tripoint_bub_ms>( rescue_destination() );
    } );

    REQUIRE( result.handled );
    REQUIRE( result.started );
    REQUIRE( result.rescuer != nullptr );
    CHECK( result.casualty == scene.casualty );
    CHECK( npc_ai::get_ai_request_queue().pending_count() == pending_before );
    REQUIRE( npc_ai::rescue_for_casualty( *scene.casualty ) );
    const bool first_has_claim = npc_ai::rescue_for_rescuer( *scene.rescuer ).has_value();
    const bool second_has_claim = npc_ai::rescue_for_rescuer( second_rescuer ).has_value();
    CHECK( first_has_claim != second_has_claim );

    const npc_ai::rescue_state state = *npc_ai::rescue_for_casualty( *scene.casualty );
    REQUIRE( state.goal_id != 0 );
    REQUIRE( npc_ai::active_goal( *result.rescuer ) );
    CHECK( npc_ai::active_goal( *result.rescuer )->id == state.goal_id );
    CHECK( npc_ai::active_goal( *result.rescuer )->kind ==
           npc_ai::ai_goal_kind::rescue_casualty );
    CHECK( npc_ai::active_goal( *result.rescuer )->priority ==
           npc_ai::ai_goal_priority::emergency );
    REQUIRE( state.casualty_path.size() >= 2 );
    REQUIRE( state.rescuer_path.size() >= 2 );
    CHECK( state.casualty_path.front() == scene.casualty->pos_abs() );
    CHECK( state.casualty_path.back() == get_map().get_abs( rescue_destination() ) );
    CHECK( state.rescuer_path.front() == state.casualty_path[1] );
    CHECK( state.rescuer_path[state.rescuer_path.size() - 2] ==
           state.casualty_destination );
    CHECK( state.rescuer_path.back() == state.rescuer_final_anchor );
}

TEST_CASE( "npc_rescue_order_rejects_walkers_forbidden_casualties_and_ambiguous_names",
           "[npc_ai][npc_ai_rescue]" )
{
    SECTION( "healthy walker" ) {
        rescue_scene scene = prepare_rescue_scene();
        scene.casualty->name = "Kim";
        for( const bodypart_id &leg : scene.casualty->get_all_body_parts_of_type(
                 body_part_type::type::leg, get_body_part_flags::primary_type ) ) {
            scene.casualty->set_part_hp_cur( leg, scene.casualty->get_part_hp_max( leg ) );
        }
        bool selected = false;
        const npc_ai::rescue_command_result result = npc_ai::execute_rescue_order(
                    { scene.rescuer }, "arrastra a Kim hasta aqui", [&]() {
            selected = true;
            return std::optional<tripoint_bub_ms>( rescue_destination() );
        } );
        CHECK( result.handled );
        CHECK_FALSE( result.started );
        CHECK_FALSE( selected );
        CHECK_FALSE( npc_ai::rescue_for_casualty( *scene.casualty ) );
    }

    SECTION( "must not move" ) {
        rescue_scene scene = prepare_rescue_scene();
        scene.casualty->name = "Kim";
        scene.casualty->add_effect( effect_grabbed, 10_turns );
        bool selected = false;
        const npc_ai::rescue_command_result result = npc_ai::execute_rescue_order(
                    { scene.rescuer }, "drag Kim over here", [&]() {
            selected = true;
            return std::optional<tripoint_bub_ms>( rescue_destination() );
        } );
        CHECK( result.handled );
        CHECK_FALSE( result.started );
        CHECK_FALSE( selected );
        CHECK_FALSE( npc_ai::rescue_for_casualty( *scene.casualty ) );
    }

    SECTION( "same name is not resolved arbitrarily" ) {
        rescue_scene scene = prepare_rescue_scene();
        scene.casualty->name = "Kim";
        npc &other_kim = spawn_npc( point_bub_ms{ 62, 65 }, "test_talker" );
        clear_character( other_kim );
        other_kim.name = "Kim";
        other_kim.set_fac( faction_your_followers );
        other_kim.set_attitude( NPCATT_FOLLOW );
        break_legs( other_kim, 1 );
        bool selected = false;
        const npc_ai::rescue_command_result result = npc_ai::execute_rescue_order(
                    { scene.rescuer }, "haul Kim over here", [&]() {
            selected = true;
            return std::optional<tripoint_bub_ms>( rescue_destination() );
        } );
        CHECK( result.handled );
        CHECK_FALSE( result.started );
        CHECK_FALSE( selected );
        CHECK_FALSE( npc_ai::rescue_for_casualty( *scene.casualty ) );
        CHECK_FALSE( npc_ai::rescue_for_casualty( other_kim ) );
    }
}

TEST_CASE( "npc_rescue_goal_failure_releases_claim_and_finish_is_idempotent",
           "[npc_ai][npc_ai_rescue]" )
{
    SECTION( "no safe final anchor" ) {
        rescue_scene scene = prepare_rescue_scene();
        map &here = get_map();
        const tripoint_bub_ms destination = rescue_destination();
        for( const tripoint_bub_ms &nearby : here.points_in_radius( destination, 1 ) ) {
            if( nearby != destination ) {
                here.ter_set( nearby, ter_wall );
            }
        }
        std::string reason;
        CHECK_FALSE( npc_ai::begin_rescue( *scene.rescuer, *scene.casualty,
                                           destination, &reason ) );
        CHECK_FALSE( reason.empty() );
        CHECK_FALSE( npc_ai::rescue_for_rescuer( *scene.rescuer ) );
        CHECK_FALSE( npc_ai::rescue_for_casualty( *scene.casualty ) );
        CHECK_FALSE( npc_ai::active_goal( *scene.rescuer ) );
    }

    SECTION( "goal creation failure" ) {
        rescue_scene scene = prepare_rescue_scene();
        npc_ai::set_rescue_goal_factory_for_test( []( const npc &, const std::string & ) {
            return std::uint64_t{ 0 };
        } );
        std::string reason;
        const std::optional<npc_ai::rescue_id> result = npc_ai::begin_rescue(
                    *scene.rescuer, *scene.casualty, rescue_destination(), &reason );
        npc_ai::set_rescue_goal_factory_for_test( npc_ai::rescue_goal_factory() );
        CHECK_FALSE( result );
        CHECK_FALSE( reason.empty() );
        CHECK_FALSE( npc_ai::rescue_for_rescuer( *scene.rescuer ) );
        CHECK_FALSE( npc_ai::rescue_for_casualty( *scene.casualty ) );
    }

    SECTION( "double finish" ) {
        rescue_scene scene = prepare_rescue_scene();
        const std::optional<npc_ai::rescue_id> id = npc_ai::begin_rescue(
                    *scene.rescuer, *scene.casualty, rescue_destination() );
        REQUIRE( id );
        CHECK( npc_ai::finish_rescue( *id, npc_ai::rescue_finish_outcome::cancelled,
                                      "test cancellation" ) );
        CHECK_FALSE( npc_ai::finish_rescue( *id, npc_ai::rescue_finish_outcome::cancelled,
                                            "duplicate cancellation" ) );
        CHECK_FALSE( npc_ai::rescue_for_rescuer( *scene.rescuer ) );
        CHECK_FALSE( npc_ai::rescue_for_casualty( *scene.casualty ) );
    }
}

TEST_CASE( "npc_rescue_linked_impaired_casualty_cannot_take_an_independent_turn",
           "[npc_ai][npc_ai_rescue]" )
{
    rescue_scene scene = prepare_rescue_scene();
    REQUIRE( npc_ai::begin_rescue( *scene.rescuer, *scene.casualty,
                                   rescue_destination() ) );
    REQUIRE( advance_until_attached( scene ) );
    const tripoint_bub_ms casualty_before = scene.casualty->pos_bub();
    scene.casualty->set_moves( 1000 );

    scene.casualty->move();

    CHECK( scene.casualty->pos_bub() == casualty_before );
    CHECK( scene.casualty->get_moves() <= 0 );
    REQUIRE( npc_ai::rescue_for_casualty( *scene.casualty ) );
    CHECK( npc_ai::rescue_for_casualty( *scene.casualty )->phase ==
           npc_ai::rescue_phase::attached );
}

TEST_CASE( "npc_rescue_completes_only_with_casualty_at_destination_and_preserves_orders",
           "[npc_ai][npc_ai_rescue]" )
{
    rescue_scene scene = prepare_rescue_scene();
    const npc_attitude attitude_before = scene.rescuer->get_attitude();
    const npc_mission mission_before = scene.rescuer->mission;
    const tripoint_bub_ms destination = rescue_destination();
    REQUIRE( npc_ai::begin_rescue( *scene.rescuer, *scene.casualty, destination ) );
    tripoint_abs_ms expected_anchor;
    REQUIRE( npc_ai::rescue_for_rescuer( *scene.rescuer ) );
    expected_anchor = npc_ai::rescue_for_rescuer( *scene.rescuer )->rescuer_final_anchor;

    for( int turn = 0; turn < 100 &&
         npc_ai::rescue_for_rescuer( *scene.rescuer ); ++turn ) {
        const npc_ai::rescue_state before =
            *npc_ai::rescue_for_rescuer( *scene.rescuer );
        INFO( "turn=" << turn << " phase=" << static_cast<int>( before.phase ) <<
              " rescuer=" << scene.rescuer->pos_bub().to_string() <<
              " casualty=" << scene.casualty->pos_bub().to_string() <<
              " casualty_path=" << before.casualty_path.size() <<
              " next=" << get_map().get_bub( before.casualty_path[1] ).to_string() <<
              " destination=" << get_map().get_bub( before.casualty_destination ).to_string() <<
              " anchor=" << get_map().get_bub( before.rescuer_final_anchor ).to_string() );
        scene.rescuer->set_moves( 10000 );
        const npc_ai::rescue_tick_result result = npc_ai::tick_rescue( *scene.rescuer );
        std::string tick_failure_reason;
        if( result == npc_ai::rescue_tick_result::cancelled ) {
            const std::vector<npc_ai::ai_goal> history =
                npc_ai::goal_history( *scene.rescuer );
            if( !history.empty() ) {
                tick_failure_reason = history.back().failure_reason;
            }
        }
        INFO( "failure_reason=" << tick_failure_reason );
        REQUIRE( result != npc_ai::rescue_tick_result::cancelled );
    }

    CHECK_FALSE( npc_ai::rescue_for_rescuer( *scene.rescuer ) );
    CHECK( scene.casualty->pos_bub() == destination );
    CHECK( scene.rescuer->pos_abs() == expected_anchor );
    CHECK( scene.rescuer->get_attitude() == attitude_before );
    CHECK( scene.rescuer->mission == mission_before );
}

TEST_CASE( "npc_rescue_positioning_path_does_not_cross_the_casualty",
           "[npc_ai][npc_ai_rescue]" )
{
    rescue_scene scene = prepare_rescue_scene();
    map &here = get_map();
    scene.rescuer->setpos( here, tripoint_bub_ms{ 64, 68, 0 } );
    REQUIRE( scene.rescuer->is_active() );
    REQUIRE( npc_ai::begin_rescue( *scene.rescuer, *scene.casualty,
                                   rescue_destination() ) );
    scene.casualty->setpos( here, tripoint_bub_ms{ 63, 65, 0 } );
    REQUIRE( scene.casualty->is_active() );
    CHECK_FALSE( npc_ai::consume_linked_casualty_turn( *scene.casualty ) );
    REQUIRE( npc_ai::rescue_for_rescuer( *scene.rescuer ) );

    scene.rescuer->set_moves( 10000 );
    const npc_ai::rescue_tick_result result = npc_ai::tick_rescue( *scene.rescuer );

    CHECK( result == npc_ai::rescue_tick_result::consumed_turn );
    CHECK( std::find( scene.rescuer->path.begin(), scene.rescuer->path.end(),
                      scene.casualty->pos_bub() ) == scene.rescuer->path.end() );
    CHECK( scene.rescuer->pos_bub() != scene.casualty->pos_bub() );
    REQUIRE( npc_ai::rescue_for_rescuer( *scene.rescuer ) );
    CHECK( npc_ai::rescue_for_rescuer( *scene.rescuer )->casualty_path.front() ==
           scene.casualty->pos_abs() );
}

TEST_CASE( "npc_rescue_invalid_link_and_recovered_casualty_cancel_cleanly",
           "[npc_ai][npc_ai_rescue]" )
{
    SECTION( "linked pair separates" ) {
        rescue_scene scene = prepare_rescue_scene();
        REQUIRE( npc_ai::begin_rescue( *scene.rescuer, *scene.casualty,
                                       rescue_destination() ) );
        REQUIRE( advance_until_attached( scene ) );
        scene.rescuer->setpos( get_map(), tripoint_bub_ms{ 80, 80, 0 } );
        CHECK( npc_ai::tick_rescue( *scene.rescuer ) ==
               npc_ai::rescue_tick_result::cancelled );
        CHECK_FALSE( npc_ai::rescue_for_rescuer( *scene.rescuer ) );
        CHECK_FALSE( npc_ai::rescue_for_casualty( *scene.casualty ) );
    }

    SECTION( "casualty can walk again" ) {
        rescue_scene scene = prepare_rescue_scene();
        REQUIRE( npc_ai::begin_rescue( *scene.rescuer, *scene.casualty,
                                       rescue_destination() ) );
        REQUIRE( advance_until_attached( scene ) );
        for( const bodypart_id &leg : scene.casualty->get_all_body_parts_of_type(
                 body_part_type::type::leg, get_body_part_flags::primary_type ) ) {
            scene.casualty->set_part_hp_cur( leg, scene.casualty->get_part_hp_max( leg ) );
        }
        CHECK_FALSE( npc_ai::consume_linked_casualty_turn( *scene.casualty ) );
        CHECK_FALSE( npc_ai::rescue_for_rescuer( *scene.rescuer ) );
        CHECK_FALSE( npc_ai::rescue_for_casualty( *scene.casualty ) );
    }
}

TEST_CASE( "npc_rescue_new_follow_guard_and_move_orders_cancel_before_winning",
           "[npc_ai][npc_ai_rescue]" )
{
    SECTION( "guard" ) {
        rescue_scene scene = prepare_rescue_scene();
        REQUIRE( npc_ai::begin_rescue( *scene.rescuer, *scene.casualty,
                                       rescue_destination() ) );
        CHECK( npc_ai::cancel_rescues_for_new_order( { scene.rescuer },
                "superseded by guard" ) == 1 );
        talk_function::assign_guard( *scene.rescuer );
        CHECK_FALSE( npc_ai::rescue_for_rescuer( *scene.rescuer ) );
        CHECK( scene.rescuer->mission == NPC_MISSION_GUARD_ALLY );
    }

    SECTION( "follow" ) {
        rescue_scene scene = prepare_rescue_scene();
        talk_function::assign_guard( *scene.rescuer );
        REQUIRE( npc_ai::begin_rescue( *scene.rescuer, *scene.casualty,
                                       rescue_destination() ) );
        CHECK( npc_ai::cancel_rescues_for_new_order( { scene.rescuer },
                "superseded by follow" ) == 1 );
        talk_function::stop_guard( *scene.rescuer );
        CHECK_FALSE( npc_ai::rescue_for_rescuer( *scene.rescuer ) );
        CHECK( scene.rescuer->get_attitude() == NPCATT_FOLLOW );
    }

    SECTION( "explicit movement" ) {
        rescue_scene scene = prepare_rescue_scene();
        REQUIRE( npc_ai::begin_rescue( *scene.rescuer, *scene.casualty,
                                       rescue_destination() ) );
        CHECK( npc_ai::cancel_rescues_for_new_order( { scene.rescuer },
                "superseded by movement" ) == 1 );
        scene.rescuer->goto_to_this_pos = get_map().get_abs( tripoint_bub_ms{ 61, 61, 0 } );
        CHECK_FALSE( npc_ai::rescue_for_rescuer( *scene.rescuer ) );
        CHECK( scene.rescuer->goto_to_this_pos.has_value() );
    }

    SECTION( "incompatible replacement rescue" ) {
        rescue_scene scene = prepare_rescue_scene();
        REQUIRE( npc_ai::begin_rescue( *scene.rescuer, *scene.casualty,
                                       rescue_destination() ) );
        npc &healthy_kim = spawn_npc( point_bub_ms{ 62, 68 }, "test_talker" );
        clear_character( healthy_kim );
        healthy_kim.name = "Kim";
        healthy_kim.set_fac( faction_your_followers );
        healthy_kim.set_attitude( NPCATT_FOLLOW );
        bool selected = false;

        const npc_ai::rescue_command_result replacement = npc_ai::execute_rescue_order(
                    { scene.rescuer }, "drag Kim over here", [&]() {
            selected = true;
            return std::optional<tripoint_bub_ms>( rescue_destination() );
        } );

        CHECK( replacement.handled );
        CHECK_FALSE( replacement.started );
        CHECK_FALSE( selected );
        CHECK_FALSE( npc_ai::rescue_for_rescuer( *scene.rescuer ) );
        CHECK_FALSE( npc_ai::rescue_for_casualty( *scene.casualty ) );
    }
}

TEST_CASE( "npc_rescue_operation_and_fire_early_returns_do_not_leave_a_stuck_claim",
           "[npc_ai][npc_ai_rescue]" )
{
    SECTION( "operation" ) {
        rescue_scene scene = prepare_rescue_scene();
        REQUIRE( npc_ai::begin_rescue( *scene.rescuer, *scene.casualty,
                                       rescue_destination() ) );
        scene.rescuer->assign_activity( ACT_OPERATION, 10000 );
        scene.rescuer->activity.values = { 1, 0, 0, 0 };
        scene.rescuer->activity.str_values = { "install", "bio_power_storage", "", "false" };
        scene.rescuer->set_moves( 1000 );
        scene.rescuer->move();
        CHECK_FALSE( npc_ai::rescue_for_rescuer( *scene.rescuer ) );
        CHECK_FALSE( npc_ai::rescue_for_casualty( *scene.casualty ) );
    }

    SECTION( "dangerous fire field while linked" ) {
        rescue_scene scene = prepare_rescue_scene();
        REQUIRE( npc_ai::begin_rescue( *scene.rescuer, *scene.casualty,
                                       rescue_destination() ) );
        REQUIRE( advance_until_attached( scene ) );
        REQUIRE( get_map().add_field( scene.rescuer->pos_bub(), fd_fire, 2 ) );
        scene.rescuer->set_moves( 1000 );
        scene.rescuer->move();
        CHECK_FALSE( npc_ai::rescue_for_rescuer( *scene.rescuer ) );
        CHECK_FALSE( npc_ai::rescue_for_casualty( *scene.casualty ) );
    }

    SECTION( "adjacent melee threat" ) {
        reset_request_executor reset_executor;
        npc_ai::set_ai_request_executor_for_test( []( const std::string & ) {
            return npc_ai::ai_response{ true, "DECISION=SILENT", "" };
        }, false );
        rescue_scene scene = prepare_rescue_scene();
        scene.rescuer->rules.engagement = combat_engagement::ALL;
        scene.rescuer->rules.clear_flag( ally_rule::forbid_engage );
        scene.rescuer->rules.clear_flag( ally_rule::follow_close );
        REQUIRE( npc_ai::begin_rescue( *scene.rescuer, *scene.casualty,
                                       rescue_destination() ) );
        REQUIRE( advance_until_attached( scene ) );
        map &here = get_map();
        tripoint_bub_ms hostile_pos = scene.rescuer->pos_bub() + tripoint::north;
        if( get_creature_tracker().creature_at( hostile_pos ) != nullptr ) {
            hostile_pos = scene.rescuer->pos_bub() + tripoint::south;
        }
        REQUIRE( get_creature_tracker().creature_at( hostile_pos ) == nullptr );
        monster &hostile = spawn_test_monster( "mon_zombie_brute", hostile_pos );
        hostile.set_dest( scene.rescuer->pos_abs() );
        here.invalidate_map_cache( scene.rescuer->posz() );
        here.build_map_cache( scene.rescuer->posz(), true );
        here.invalidate_visibility_cache();
        here.update_visibility_cache( scene.rescuer->posz() );
        scene.rescuer->recalc_sight_limits();
        scene.rescuer->regen_ai_cache();
        REQUIRE( scene.rescuer->current_target() == &hostile );
        const int moves_before = 1000;
        scene.rescuer->set_moves( moves_before );

        scene.rescuer->move();

        CHECK_FALSE( npc_ai::rescue_for_rescuer( *scene.rescuer ) );
        CHECK_FALSE( npc_ai::rescue_for_casualty( *scene.casualty ) );
        CHECK( scene.rescuer->get_moves() < moves_before );
    }
}
