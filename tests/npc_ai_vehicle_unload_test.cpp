#include "cata_catch.h"

#include <set>
#include <string>
#include <vector>

#include "avatar.h"
#include "calendar.h"
#include "clzones.h"
#include "faction.h"
#include "game.h"
#include "item.h"
#include "map.h"
#include "map_helpers.h"
#include "npc.h"
#include "npc_ai_goal.h"
#include "npc_ai_vehicle_unload.h"
#include "player_activity.h"
#include "player_helpers.h"
#include "point.h"
#include "type_id.h"
#include "vehicle.h"
#include "veh_type.h"
#include "vpart_position.h"

namespace
{

static const activity_id ACT_MOVE_LOOT( "ACT_MOVE_LOOT" );
static const faction_id faction_free_merchants( "free_merchants" );
static const faction_id faction_your_followers( "your_followers" );
static const itype_id itype_anvil( "anvil" );
static const itype_id itype_funnel( "funnel" );
static const itype_id itype_hose( "hose" );
static const itype_id itype_jumper_cable( "jumper_cable" );
static const itype_id itype_plastic_sheet( "plastic_sheet" );
static const itype_id itype_shotgun_d( "shotgun_d" );
static const itype_id itype_test_apple( "test_apple" );
static const ter_str_id ter_concrete_wall( "t_concrete_wall" );
static const vproto_id vehicle_prototype_hatchback( "car_hatch" );
static const vproto_id vehicle_prototype_test_shopping_cart( "test_shopping_cart" );
static const zone_type_id zone_type_LOOT_FOOD( "LOOT_FOOD" );
static const zone_type_id zone_type_LOOT_UNSORTED( "LOOT_UNSORTED" );

void refresh_visibility( npc &observer )
{
    map &here = get_map();
    here.invalidate_map_cache( observer.posz() );
    here.build_map_cache( observer.posz(), true );
    here.invalidate_visibility_cache();
    here.update_visibility_cache( observer.posz() );
    observer.recalc_sight_limits();
}

npc &prepare_logistics_npc()
{
    g->faction_manager_ptr->create_if_needed();
    clear_map( -1, 1 );
    clear_zones();
    clear_avatar();
    set_time_to_day();
    g->place_player( tripoint_bub_ms{ 60, 60, 0 } );

    npc &who = spawn_npc( point_bub_ms{ 65, 60 }, "test_talker" );
    who.name = "Logistics worker";
    clear_character( who );
    who.setpos( get_map(), tripoint_bub_ms{ 65, 60, 0 } );
    who.set_fac( faction_your_followers );
    who.set_attitude( NPCATT_FOLLOW );
    npc_ai::cancel_vehicle_unload_task( who );
    refresh_visibility( who );
    return who;
}

vehicle &add_cart( npc &who, const tripoint_bub_ms &position )
{
    vehicle *cart = get_map().add_vehicle( vehicle_prototype_test_shopping_cart, position,
                                          0_degrees, 0, 0 );
    REQUIRE( cart != nullptr );
    cart->set_owner( who );
    return *cart;
}

vehicle &add_hatchback( npc &who, const tripoint_bub_ms &position )
{
    vehicle *car = get_map().add_vehicle( vehicle_prototype_hatchback, position,
                                         0_degrees, 0, 0 );
    REQUIRE( car != nullptr );
    car->set_owner( who );
    for( const vpart_reference &cargo : car->get_avail_parts( VPFLAG_CARGO ) ) {
        cargo.items().clear();
    }
    return *car;
}

void add_owned_item( npc &who, vehicle &veh, vehicle_part &part, item it )
{
    it.set_owner( who );
    REQUIRE( veh.add_item( get_map(), part, it ) );
}

void add_owned_apple( npc &who, vehicle &cart, const tripoint_bub_ms &position )
{
    const std::optional<vpart_reference> cargo = get_map().veh_at( position ).cargo();
    REQUIRE( cargo );
    item apple( itype_test_apple, calendar::turn );
    apple.set_owner( who );
    REQUIRE( cart.add_item( get_map(), cargo->part(), apple ) );
}

int ground_items_with_var( map &here, const tripoint_bub_ms &center, const int radius,
                           const std::string &var )
{
    int found = 0;
    for( const tripoint_bub_ms &position : here.points_in_radius( center, radius ) ) {
        for( const item &it : here.i_at( position ) ) {
            found += it.has_var( var ) ? 1 : 0;
        }
    }
    return found;
}

const item *ground_item_with_var( map &here, const tripoint_bub_ms &center, const int radius,
                                  const std::string &var )
{
    for( const tripoint_bub_ms &position : here.points_in_radius( center, radius ) ) {
        for( const item &it : here.i_at( position ) ) {
            if( it.has_var( var ) ) {
                return &it;
            }
        }
    }
    return nullptr;
}

bool vehicle_cargo_empty( vehicle &veh )
{
    for( const vpart_reference &part : veh.get_avail_parts( VPFLAG_CARGO ) ) {
        if( !part.items().empty() ) {
            return false;
        }
    }
    return true;
}

void add_zone( const zone_type_id &type, const tripoint_abs_ms &position )
{
    zone_manager::get_manager().add( type.str(), type, faction_your_followers, false, true,
                                     position, position, nullptr, true );
}

void run_unload_task( npc &who, const int limit = 500 )
{
    for( int turn = 0; turn < limit &&
         ( npc_ai::has_vehicle_unload_task( who ) || who.has_player_activity() ); ++turn ) {
        who.set_moves( 1000 );
        if( who.has_player_activity() ) {
            who.do_player_activity();
        } else {
            npc_ai::process_vehicle_unload_task( who );
        }
    }
}

} // namespace

TEST_CASE( "npc_ai_vehicle_unload_moves_visible_cargo_to_nearby_ground",
           "[npc_ai][npc_ai_vehicle_unload]" )
{
    npc &who = prepare_logistics_npc();
    map &here = get_map();
    const tripoint_bub_ms source = who.pos_bub( here ) + tripoint::east;
    vehicle &cart = add_cart( who, source );
    add_owned_apple( who, cart, source );
    add_zone( zone_type_LOOT_FOOD, who.pos_abs() );
    refresh_visibility( who );

    const npc_ai::vehicle_unload_command_result result =
        npc_ai::try_handle_vehicle_unload_command( who, "Descarga el camión y ordena todo." );

    REQUIRE( result.handled );
    REQUIRE( result.started );
    run_unload_task( who );

    CHECK_FALSE( npc_ai::has_vehicle_unload_task( who ) );
    const std::optional<vpart_reference> cargo = here.veh_at( source ).cargo();
    REQUIRE( cargo );
    CHECK( cargo->items().empty() );
    CHECK( ground_items_with_var( here, source, 2, "unload_test" ) == 0 );
    CHECK_FALSE( ( here.i_at( source + tripoint::north ).empty() &&
                   here.i_at( source + tripoint::south ).empty() &&
                   here.i_at( source + tripoint::east ).empty() &&
                   here.i_at( source + tripoint::west ).empty() ) );
}

TEST_CASE( "targeted_move_loot_does_not_scan_other_unsorted_zones",
           "[npc_ai][npc_ai_vehicle_unload]" )
{
    npc &who = prepare_logistics_npc();
    map &here = get_map();
    const tripoint_bub_ms source = who.pos_bub( here ) + tripoint::east;
    const tripoint_bub_ms unrelated = who.pos_bub( here ) + tripoint::north;
    vehicle &cart = add_cart( who, source );
    add_owned_apple( who, cart, source );
    item other_apple( itype_test_apple, calendar::turn );
    other_apple.set_owner( who );
    here.add_item_or_charges( unrelated, other_apple );
    add_zone( zone_type_LOOT_UNSORTED, here.get_abs( unrelated ) );
    add_zone( zone_type_LOOT_FOOD, who.pos_abs() );

    player_activity activity( ACT_MOVE_LOOT );
    activity.coord_set.insert( here.get_abs( source ) );
    who.assign_activity( activity );
    process_activity( who );

    const std::optional<vpart_reference> cargo = here.veh_at( source ).cargo();
    REQUIRE( cargo );
    CHECK( cargo->items().empty() );
    CHECK_FALSE( here.i_at( unrelated ).empty() );
}

TEST_CASE( "npc_ai_vehicle_unload_uses_ground_without_creating_missing_zones",
           "[npc_ai][npc_ai_vehicle_unload]" )
{
    npc &who = prepare_logistics_npc();
    map &here = get_map();
    const tripoint_bub_ms source = who.pos_bub( here ) + tripoint::east;
    vehicle &cart = add_cart( who, source );
    add_owned_apple( who, cart, source );
    refresh_visibility( who );
    const std::size_t zones_before = zone_manager::get_manager().size();

    const npc_ai::vehicle_unload_command_result result =
        npc_ai::try_handle_vehicle_unload_command( who, "Descarga el vehiculo." );
    REQUIRE( result.started );
    run_unload_task( who );

    CHECK_FALSE( npc_ai::has_vehicle_unload_task( who ) );
    const std::optional<vpart_reference> cargo = here.veh_at( source ).cargo();
    REQUIRE( cargo );
    CHECK( cargo->items().empty() );
    CHECK( zone_manager::get_manager().size() == zones_before );
}

TEST_CASE( "npc_ai_vehicle_unload_prefers_adjacent_unsorted_zone",
           "[npc_ai][npc_ai_vehicle_unload]" )
{
    npc &who = prepare_logistics_npc();
    map &here = get_map();
    const tripoint_bub_ms source = who.pos_bub( here ) + tripoint::east;
    const tripoint_bub_ms destination = source + tripoint::north;
    vehicle &cart = add_cart( who, source );
    add_owned_apple( who, cart, source );
    add_zone( zone_type_LOOT_UNSORTED, here.get_abs( destination ) );
    refresh_visibility( who );

    REQUIRE( npc_ai::try_handle_vehicle_unload_command( who, "Descarga el vehiculo." ).started );
    run_unload_task( who );

    const std::optional<vpart_reference> cargo = here.veh_at( source ).cargo();
    REQUIRE( cargo );
    CHECK( cargo->items().empty() );
    CHECK_FALSE( here.i_at( destination ).empty() );
}

TEST_CASE( "npc_ai_vehicle_unload_scans_every_real_hatchback_storage_part",
           "[npc_ai][npc_ai_vehicle_unload]" )
{
    npc &who = prepare_logistics_npc();
    map &here = get_map();
    const tripoint_bub_ms origin = who.pos_bub( here ) + tripoint{ 4, 0, 0 };
    vehicle &car = add_hatchback( who, origin );
    std::set<std::string> part_types;
    int expected = 0;
    for( const vpart_reference &cargo : car.get_avail_parts( VPFLAG_CARGO ) ) {
        item marker( itype_test_apple, calendar::turn );
        marker.set_var( "hatchback_storage_marker", std::to_string( cargo.part_index() ) );
        add_owned_item( who, car, cargo.part(), marker );
        part_types.insert( cargo.info().id.str() );
        ++expected;
    }
    REQUIRE( expected >= 8 );
    CHECK( part_types.count( "hatch" ) > 0 );
    CHECK( part_types.count( "seat" ) > 0 );
    CHECK( part_types.count( "seat_back" ) > 0 );
    CHECK( part_types.count( "door" ) > 0 );
    refresh_visibility( who );

    const npc_ai::vehicle_unload_command_result result =
        npc_ai::try_handle_vehicle_unload_command( who, "Liam, descarga completamente el auto." );
    REQUIRE( result.started );
    run_unload_task( who );

    CHECK_FALSE( npc_ai::has_vehicle_unload_task( who ) );
    CHECK( vehicle_cargo_empty( car ) );
    CHECK( ground_items_with_var( here, origin, 8, "hatchback_storage_marker" ) == expected );
}

TEST_CASE( "npc_ai_vehicle_unload_moves_long_gun_without_inventory_pocket",
           "[npc_ai][npc_ai_vehicle_unload]" )
{
    npc &who = prepare_logistics_npc();
    map &here = get_map();
    const tripoint_bub_ms source = who.pos_bub( here ) + tripoint::east;
    vehicle &cart = add_cart( who, source );
    const std::optional<vpart_reference> cargo = here.veh_at( source ).cargo();
    REQUIRE( cargo );
    item shotgun( itype_shotgun_d, calendar::turn );
    shotgun.set_var( "unload_long_gun", "same_item" );
    shotgun.set_damage( 1000 );
    shotgun.is_favorite = true;
    REQUIRE_FALSE( who.can_pickVolume( shotgun ) );
    REQUIRE( who.can_lift( shotgun ) );
    add_owned_item( who, cart, cargo->part(), shotgun );
    refresh_visibility( who );

    REQUIRE( npc_ai::try_handle_vehicle_unload_command( who, "Descarga el vehiculo." ).started );
    run_unload_task( who );

    CHECK( cargo->items().empty() );
    CHECK( ground_items_with_var( here, source, 2, "unload_long_gun" ) == 1 );
    const item *moved_shotgun = ground_item_with_var( here, source, 2, "unload_long_gun" );
    REQUIRE( moved_shotgun != nullptr );
    CHECK( moved_shotgun->typeId() == itype_shotgun_d );
    CHECK( moved_shotgun->damage() == 1000 );
    CHECK( moved_shotgun->is_favorite );
    CHECK( moved_shotgun->get_var( "unload_long_gun" ) == "same_item" );
}

TEST_CASE( "npc_ai_vehicle_unload_leaves_item_that_is_physically_too_heavy",
           "[npc_ai][npc_ai_vehicle_unload]" )
{
    npc &who = prepare_logistics_npc();
    who.str_max = 1;
    who.str_cur = 1;
    get_avatar().str_max = 1;
    get_avatar().str_cur = 1;
    map &here = get_map();
    const tripoint_bub_ms source = who.pos_bub( here ) + tripoint::east;
    vehicle &cart = add_cart( who, source );
    const std::optional<vpart_reference> cargo = here.veh_at( source ).cargo();
    REQUIRE( cargo );
    item anvil( itype_anvil, calendar::turn );
    REQUIRE_FALSE( who.can_lift( anvil ) );
    add_owned_item( who, cart, cargo->part(), anvil );
    refresh_visibility( who );

    REQUIRE( npc_ai::try_handle_vehicle_unload_command( who, "Descarga el vehiculo." ).started );
    run_unload_task( who );

    CHECK_FALSE( cargo->items().empty() );
    CHECK( ground_items_with_var( here, source, 2, "never_set" ) == 0 );
    const std::vector<npc_ai::ai_goal> goals = npc_ai::goal_history( who );
    REQUIRE_FALSE( goals.empty() );
    CHECK( goals.back().status == npc_ai::ai_goal_status::failed );
}

TEST_CASE( "npc_ai_vehicle_unload_moves_hatchback_mixed_cargo",
           "[npc_ai][npc_ai_vehicle_unload]" )
{
    npc &who = prepare_logistics_npc();
    map &here = get_map();
    const tripoint_bub_ms origin = who.pos_bub( here ) + tripoint{ 4, 0, 0 };
    vehicle &car = add_hatchback( who, origin );
    vehicle_part *hatch = nullptr;
    vehicle_part *rear_seat = nullptr;
    for( const vpart_reference &cargo : car.get_avail_parts( VPFLAG_CARGO ) ) {
        if( hatch == nullptr && cargo.info().id.str() == "hatch" ) {
            hatch = &cargo.part();
        } else if( rear_seat == nullptr && cargo.info().id.str() == "seat_back" ) {
            rear_seat = &cargo.part();
        }
    }
    REQUIRE( hatch != nullptr );
    REQUIRE( rear_seat != nullptr );

    const std::vector<itype_id> hatch_items = {
        itype_funnel, itype_jumper_cable, itype_hose, itype_plastic_sheet
    };
    const std::vector<itype_id> seat_items = {
        itype_test_apple, itype_shotgun_d, itype_test_apple, itype_test_apple
    };
    int sequence = 0;
    for( const itype_id &type : hatch_items ) {
        item it( type, calendar::turn );
        it.set_var( "hatchback_mix", std::to_string( sequence++ ) );
        add_owned_item( who, car, *hatch, it );
    }
    for( const itype_id &type : seat_items ) {
        item it( type, calendar::turn );
        it.set_var( "hatchback_mix", std::to_string( sequence++ ) );
        add_owned_item( who, car, *rear_seat, it );
    }
    refresh_visibility( who );

    REQUIRE( npc_ai::try_handle_vehicle_unload_command( who, "Descarga el auto." ).started );
    run_unload_task( who );

    CHECK( car.get_items( *hatch ).empty() );
    CHECK( car.get_items( *rear_seat ).empty() );
    CHECK( ground_items_with_var( here, origin, 8, "hatchback_mix" ) == sequence );
}

TEST_CASE( "npc_ai_vehicle_unload_does_not_claim_foreign_cargo_was_unloaded",
           "[npc_ai][npc_ai_vehicle_unload]" )
{
    npc &who = prepare_logistics_npc();
    map &here = get_map();
    const tripoint_bub_ms source = who.pos_bub( here ) + tripoint::east;
    vehicle &cart = add_cart( who, source );
    const std::optional<vpart_reference> cargo = here.veh_at( source ).cargo();
    REQUIRE( cargo );
    item foreign_apple( itype_test_apple, calendar::turn );
    foreign_apple.set_owner( faction_free_merchants );
    REQUIRE( cart.add_item( here, cargo->part(), foreign_apple ) );
    add_zone( zone_type_LOOT_FOOD, who.pos_abs() );
    refresh_visibility( who );

    const npc_ai::vehicle_unload_command_result result =
        npc_ai::try_handle_vehicle_unload_command( who, "Descarga el vehiculo." );
    REQUIRE( result.started );
    run_unload_task( who );

    CHECK_FALSE( npc_ai::has_vehicle_unload_task( who ) );
    CHECK_FALSE( cargo->items().empty() );
    const std::vector<npc_ai::ai_goal> goals = npc_ai::goal_history( who );
    REQUIRE_FALSE( goals.empty() );
    CHECK( goals.back().status == npc_ai::ai_goal_status::failed );
}

TEST_CASE( "npc_ai_vehicle_unload_does_not_select_hidden_vehicles",
           "[npc_ai][npc_ai_vehicle_unload]" )
{
    npc &who = prepare_logistics_npc();
    map &here = get_map();
    const tripoint_bub_ms wall = who.pos_bub( here ) + tripoint::east;
    const tripoint_bub_ms source = wall + tripoint::east;
    here.ter_set( wall, ter_concrete_wall );
    add_cart( who, source );
    refresh_visibility( who );
    REQUIRE_FALSE( who.sees( here, source ) );

    const npc_ai::vehicle_unload_command_result result =
        npc_ai::try_handle_vehicle_unload_command( who, "Descarga el camion." );

    CHECK( result.handled );
    CHECK_FALSE( result.started );
    CHECK_FALSE( npc_ai::has_vehicle_unload_task( who ) );
}

TEST_CASE( "npc_ai_vehicle_unload_requires_disambiguation_for_multiple_visible_vehicles",
           "[npc_ai][npc_ai_vehicle_unload]" )
{
    npc &who = prepare_logistics_npc();
    map &here = get_map();
    add_cart( who, who.pos_bub( here ) + tripoint::east );
    add_cart( who, who.pos_bub( here ) + tripoint::west );
    refresh_visibility( who );

    const npc_ai::vehicle_unload_command_result result =
        npc_ai::try_handle_vehicle_unload_command( who, "Descarga el vehiculo." );

    CHECK( result.handled );
    CHECK_FALSE( result.started );
    CHECK_FALSE( npc_ai::has_vehicle_unload_task( who ) );
}
