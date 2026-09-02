#include "cata_catch.h"

#include <algorithm>
#include <string>

#include "avatar.h"
#include "faction.h"
#include "field_type.h"
#include "game.h"
#include "map.h"
#include "map_helpers.h"
#include "monster.h"
#include "npc.h"
#include "npc_ai_context.h"
#include "npc_ai_perception.h"
#include "player_helpers.h"
#include "point.h"
#include "type_id.h"

namespace
{

static const furn_str_id furn_woodstove( "f_woodstove" );
static const ter_str_id ter_wall_metal( "t_wall_metal" );
static const trap_str_id trap_glimmer_floor( "tr_glimmer_floor" );
static const ter_str_id ter_gutter_downspout( "t_gutter_downspout" );
static const ter_str_id ter_stairs_up( "t_stairs_up" );
static const ter_str_id ter_floor( "t_floor" );

void refresh_visibility( npc &observer )
{
    map &here = get_map();
    for( int z = observer.posz() - 2; z <= observer.posz() + 2; ++z ) {
        here.invalidate_map_cache( z );
        here.build_map_cache( z, true );
    }
    here.invalidate_visibility_cache();
    here.update_visibility_cache( observer.posz() );
    observer.recalc_sight_limits();
}

npc &prepare_observer();
tripoint_bub_ms relative_tile( const npc &observer, int dx, int dy = 0, int dz = 0 );
const npc_ai::sensory_creature_observation *creature_at(
    const npc_ai::npc_sensory_snapshot &snapshot, int dx, int dy, int dz );

TEST_CASE( "npc_ai_snapshot_tracks_legitimately_visible_player_across_z_levels",
           "[npc_ai][npc_ai_perception][npc_ai_spatial]" )
{
    npc &observer = prepare_observer();
    g->place_player( relative_tile( observer, 0, 0, 1 ) );
    refresh_visibility( observer );

    const npc_ai::npc_sensory_snapshot snapshot = npc_ai::build_sensory_snapshot( observer );
    const npc_ai::sensory_creature_observation *player = creature_at( snapshot, 0, 0, 1 );

    REQUIRE( player != nullptr );
    CHECK( player->player );
    CHECK( player->knowledge == npc_ai::sensory_knowledge::currently_perceived );
    CHECK( player->dz == 1 );
    CHECK( player->distance == 1 );
}

TEST_CASE( "npc_ai_snapshot_uses_real_multi_z_visibility_for_notable_state",
           "[npc_ai][npc_ai_perception][npc_ai_spatial]" )
{
    npc &observer = prepare_observer();
    map &here = get_map();
    const tripoint_bub_ms elevated_fire = relative_tile( observer, 3, 0, 2 );
    here.ter_set( elevated_fire, ter_floor );
    REQUIRE( here.add_field( elevated_fire, fd_fire, 2 ) );
    refresh_visibility( observer );

    const npc_ai::npc_sensory_snapshot snapshot = npc_ai::build_sensory_snapshot( observer );
    const npc_ai::sensory_tile_observation *fire = snapshot.current_tile_at( 3, 0, 2 );

    REQUIRE( fire != nullptr );
    CHECK( fire->fire.value );
    CHECK( fire->fire_intensity == 2 );
    CHECK( fire->dz == 2 );
}

TEST_CASE( "npc_ai_snapshot_exposes_visible_vertical_traversal_flags",
           "[npc_ai][npc_ai_perception][npc_ai_spatial]" )
{
    npc &observer = prepare_observer();
    map &here = get_map();
    here.ter_set( relative_tile( observer, 1 ), ter_stairs_up );
    here.ter_set( relative_tile( observer, 0, 1 ), ter_gutter_downspout );
    refresh_visibility( observer );

    const npc_ai::npc_sensory_snapshot snapshot = npc_ai::build_sensory_snapshot( observer );
    const npc_ai::sensory_tile_observation *stairs = snapshot.current_tile_at( 1, 0, 0 );
    const npc_ai::sensory_tile_observation *downspout = snapshot.current_tile_at( 0, 1, 0 );

    REQUIRE( stairs != nullptr );
    CHECK( stairs->goes_up );
    REQUIRE( downspout != nullptr );
    CHECK( downspout->climbable );
}

npc &prepare_observer()
{
    g->faction_manager_ptr->create_if_needed();
    clear_map( -2, 2 );
    clear_avatar();
    set_time_to_day();
    g->place_player( tripoint_bub_ms{ 60, 60, 0 } );

    npc &observer = spawn_npc( point_bub_ms{ 65, 60 }, "test_talker" );
    observer.name = "Sensory observer";
    refresh_visibility( observer );
    return observer;
}

tripoint_bub_ms relative_tile( const npc &observer, const int dx, const int dy,
                               const int dz )
{
    const tripoint_bub_ms origin = observer.pos_bub( get_map() );
    return tripoint_bub_ms{ origin.x() + dx, origin.y() + dy, origin.z() + dz };
}

const npc_ai::sensory_creature_observation *creature_at(
    const npc_ai::npc_sensory_snapshot &snapshot, const int dx, const int dy, const int dz )
{
    const auto found = std::find_if( snapshot.creatures.begin(), snapshot.creatures.end(),
    [&]( const npc_ai::sensory_creature_observation & observation ) {
        return observation.dx == dx && observation.dy == dy && observation.dz == dz;
    } );
    return found == snapshot.creatures.end() ? nullptr : &*found;
}

} // namespace

TEST_CASE( "npc_ai_snapshot_reports_live_fire_state_on_visible_wood_stove",
           "[npc_ai][npc_ai_perception]" )
{
    npc &observer = prepare_observer();
    map &here = get_map();
    const tripoint_bub_ms stove_position = relative_tile( observer, 2 );
    here.furn_set( stove_position, furn_woodstove );
    refresh_visibility( observer );

    const npc_ai::npc_sensory_snapshot unlit_snapshot = npc_ai::build_sensory_snapshot( observer );
    const npc_ai::sensory_tile_observation *unlit_tile = unlit_snapshot.current_tile_at( 2, 0, 0 );

    REQUIRE( unlit_tile != nullptr );
    CHECK( unlit_tile->furniture_id == furn_woodstove.str() );
    CHECK( unlit_tile->fire.knowledge == npc_ai::sensory_knowledge::currently_perceived );
    CHECK_FALSE( unlit_tile->fire.value );
    CHECK( unlit_tile->fire_intensity == 0 );

    REQUIRE( here.add_field( stove_position, fd_fire, 2 ) );

    const npc_ai::npc_sensory_snapshot lit_snapshot = npc_ai::build_sensory_snapshot( observer );
    const npc_ai::sensory_tile_observation *lit_tile = lit_snapshot.current_tile_at( 2, 0, 0 );

    REQUIRE( lit_tile != nullptr );
    CHECK( lit_tile->furniture_id == furn_woodstove.str() );
    CHECK( lit_tile->fire.knowledge == npc_ai::sensory_knowledge::currently_perceived );
    CHECK( lit_tile->fire.value );
    CHECK( lit_tile->fire_intensity == 2 );

    const std::string rendered = npc_ai::render_sensory_snapshot( lit_snapshot );
    CHECK( rendered.find( "f_woodstove" ) != std::string::npos );
    CHECK( rendered.find( "fuego=true" ) != std::string::npos );
    CHECK( rendered.find( "intensidad_fuego=2" ) != std::string::npos );
}

TEST_CASE( "npc_ai_snapshot_reports_visible_bare_fire_position_and_intensity",
           "[npc_ai][npc_ai_perception]" )
{
    npc &observer = prepare_observer();
    map &here = get_map();
    const tripoint_bub_ms fire_position = relative_tile( observer, -2, 1 );
    REQUIRE( here.add_field( fire_position, fd_fire, 3 ) );
    refresh_visibility( observer );

    const npc_ai::npc_sensory_snapshot snapshot = npc_ai::build_sensory_snapshot( observer );
    const npc_ai::sensory_tile_observation *tile = snapshot.current_tile_at( -2, 1, 0 );

    REQUIRE( tile != nullptr );
    CHECK( tile->dx == -2 );
    CHECK( tile->dy == 1 );
    CHECK( tile->dz == 0 );
    CHECK( tile->fire.value );
    CHECK( tile->fire_intensity == 3 );
}

TEST_CASE( "npc_ai_snapshot_does_not_report_fire_behind_opaque_wall",
           "[npc_ai][npc_ai_perception]" )
{
    npc &observer = prepare_observer();
    map &here = get_map();
    here.ter_set( relative_tile( observer, 1 ), ter_wall_metal );
    const tripoint_bub_ms hidden_fire_position = relative_tile( observer, 3 );
    REQUIRE( here.add_field( hidden_fire_position, fd_fire, 2 ) );
    refresh_visibility( observer );

    const npc_ai::npc_sensory_snapshot snapshot = npc_ai::build_sensory_snapshot( observer );
    CHECK( snapshot.current_tile_at( 3, 0, 0 ) == nullptr );
    const npc_ai::sensory_bool fire = snapshot.current_fire_at( 3, 0, 0 );
    CHECK( fire.knowledge == npc_ai::sensory_knowledge::unknown );
}

TEST_CASE( "npc_ai_snapshot_only_reports_legitimately_visible_hostile_creatures",
           "[npc_ai][npc_ai_perception]" )
{
    npc &observer = prepare_observer();
    map &here = get_map();

    SECTION( "visible hostile creature" ) {
        spawn_test_monster( "mon_zombie", relative_tile( observer, 4 ) );
        refresh_visibility( observer );

        const npc_ai::npc_sensory_snapshot snapshot = npc_ai::build_sensory_snapshot( observer );
        const npc_ai::sensory_creature_observation *creature = creature_at( snapshot, 4, 0, 0 );

        REQUIRE( creature != nullptr );
        CHECK( creature->knowledge == npc_ai::sensory_knowledge::currently_perceived );
        CHECK( creature->distance == 4 );
        CHECK( creature->hostile );

        const std::string prompt = npc_ai::build_npc_prompt(
                                       observer, "¿Ves algún zombi?" );
        CHECK( npc_ai::classify_context_intent( "¿Ves algún zombi?" ) ==
               npc_ai::context_intent::perception_brief );
        CHECK( prompt.find( "SENSORIAL ACTUAL DEL NPC" ) != std::string::npos );
        CHECK( prompt.find( "zombie" ) != std::string::npos );
        CHECK( prompt.find( "hostil=" ) != std::string::npos );
    }

    SECTION( "hostile creature behind opaque wall" ) {
        here.ter_set( relative_tile( observer, 1 ), ter_wall_metal );
        spawn_test_monster( "mon_zombie", relative_tile( observer, 3 ) );
        refresh_visibility( observer );

        const npc_ai::npc_sensory_snapshot snapshot = npc_ai::build_sensory_snapshot( observer );
        CHECK( creature_at( snapshot, 3, 0, 0 ) == nullptr );
        const std::string prompt = npc_ai::build_npc_prompt(
                                       observer, "¿Hay enemigos?" );
        CHECK( prompt.find( "zombie" ) == std::string::npos );
    }
}

TEST_CASE( "npc_ai_snapshot_filters_nonvisual_fields_and_hidden_traps",
           "[npc_ai][npc_ai_perception]" )
{
    npc &observer = prepare_observer();
    map &here = get_map();
    const tripoint_bub_ms target = relative_tile( observer, 2 );
    REQUIRE( here.add_field( target, fd_last_known, 1 ) );
    here.trap_set( target, trap_glimmer_floor );
    refresh_visibility( observer );

    const npc_ai::npc_sensory_snapshot snapshot = npc_ai::build_sensory_snapshot( observer );
    const npc_ai::sensory_tile_observation *tile = snapshot.current_tile_at( 2, 0, 0 );

    REQUIRE( tile != nullptr );
    CHECK( tile->fields.empty() );
    CHECK( tile->trap.knowledge == npc_ai::sensory_knowledge::unknown );
}

TEST_CASE( "npc_ai_current_sensory_queries_override_conversation_memory",
           "[npc_ai][npc_ai_perception]" )
{
    CHECK( npc_ai::is_current_sensory_query( u8"¿La estufa está encendida?" ) );
    CHECK( npc_ai::is_current_sensory_query( u8"¿Qué ves ahora?" ) );
    CHECK_FALSE( npc_ai::is_current_sensory_query(
                     u8"¿Recuerdas si la estufa estaba encendida antes?" ) );
}
