#include "cata_catch.h"

#include <algorithm>
#include <string>

#include "avatar.h"
#include "calendar.h"
#include "field_type.h"
#include "flag.h"
#include "game.h"
#include "item.h"
#include "map.h"
#include "map_helpers.h"
#include "npc.h"
#include "npc_ai_context.h"
#include "npc_ai_perception.h"
#include "player_helpers.h"
#include "point.h"
#include "type_id.h"

namespace
{

static const furn_str_id furn_chair( "f_chair" );
static const itype_id itype_shot_hull( "shot_hull" );
static const ter_str_id ter_wall_metal( "t_wall_metal" );

void refresh_visibility( npc &observer )
{
    map &here = get_map();
    here.invalidate_map_cache( observer.posz() );
    here.build_map_cache( observer.posz(), true );
    here.invalidate_visibility_cache();
    here.update_visibility_cache( observer.posz() );
    observer.recalc_sight_limits();
}

npc &prepare_scene_observer()
{
    clear_map( -1, 1 );
    clear_avatar();
    set_time_to_day();
    g->place_player( tripoint_bub_ms{ 60, 60, 0 } );
    npc &observer = spawn_npc( point_bub_ms{ 65, 60 }, "test_talker" );
    observer.name = "Scene observer";
    refresh_visibility( observer );
    return observer;
}

tripoint_bub_ms relative_tile( const npc &observer, const int dx, const int dy = 0 )
{
    const tripoint_bub_ms origin = observer.pos_bub( get_map() );
    return tripoint_bub_ms{ origin.x() + dx, origin.y() + dy, origin.z() };
}

bool relationship_between( const npc_ai::sensory_spatial_relationship &relationship,
                           const std::string &left, const std::string &right,
                           const std::string &relation )
{
    const bool ids_match = ( relationship.subject_id == left && relationship.object_id == right ) ||
                           ( relationship.subject_id == right && relationship.object_id == left );
    return ids_match && relationship.relation == relation;
}

} // namespace

TEST_CASE( "npc_ai_scene_snapshot_classifies_visible_forensic_evidence",
           "[npc_ai][npc_ai_scene]" )
{
    npc &observer = prepare_scene_observer();
    map &here = get_map();
    const tripoint_bub_ms corpse_tile = relative_tile( observer, 8 );
    const tripoint_bub_ms casing_tile = relative_tile( observer, 9 );

    here.add_item_or_charges( corpse_tile,
                              item::make_corpse( mtype_id::NULL_ID(), calendar::turn, "Victim" ) );
    REQUIRE( here.add_field( corpse_tile, fd_blood, 3 ) );
    REQUIRE( here.add_field( relative_tile( observer, 8, 1 ), fd_blood, 2 ) );
    item casing( itype_shot_hull, calendar::turn );
    casing.set_flag( flag_CASING );
    here.add_item_or_charges( casing_tile, casing );

    for( int index = 0; index < 40; ++index ) {
        const int x = 2 + index % 7;
        const int y = -6 + index / 7;
        const tripoint_bub_ms furniture = relative_tile( observer, x, y );
        if( furniture != corpse_tile && furniture != casing_tile ) {
            here.furn_set( furniture, furn_chair );
        }
    }
    refresh_visibility( observer );

    const npc_ai::npc_sensory_snapshot snapshot = npc_ai::build_sensory_snapshot( observer );
    const npc_ai::sensory_tile_observation *corpse_observation =
        snapshot.current_tile_at( 8, 0, 0 );
    const npc_ai::sensory_tile_observation *casing_observation =
        snapshot.current_tile_at( 9, 0, 0 );

    REQUIRE( corpse_observation != nullptr );
    REQUIRE( casing_observation != nullptr );
    REQUIRE_FALSE( corpse_observation->items.empty() );
    const auto corpse_item = std::find_if( corpse_observation->items.begin(),
    corpse_observation->items.end(), []( const npc_ai::sensory_item_observation & item ) {
        return item.corpse;
    } );
    REQUIRE( corpse_item != corpse_observation->items.end() );
    CHECK( std::any_of( corpse_observation->fields.begin(), corpse_observation->fields.end(),
    []( const npc_ai::sensory_field_observation & field ) {
        return field.blood && field.intensity == 3;
    } ) );
    const auto casing_item = std::find_if( casing_observation->items.begin(),
    casing_observation->items.end(), []( const npc_ai::sensory_item_observation & item ) {
        return item.casing;
    } );
    REQUIRE( casing_item != casing_observation->items.end() );

    CHECK( std::any_of( snapshot.relationships.begin(), snapshot.relationships.end(),
    [&]( const npc_ai::sensory_spatial_relationship & relationship ) {
        return relationship_between( relationship, corpse_item->id, fd_blood.str(), "misma_casilla" );
    } ) );
    CHECK( std::any_of( snapshot.relationships.begin(), snapshot.relationships.end(),
    [&]( const npc_ai::sensory_spatial_relationship & relationship ) {
        return relationship_between( relationship, casing_item->id, corpse_item->id, "adyacente" );
    } ) );

    const std::string rendered = npc_ai::render_sensory_snapshot( snapshot, true );
    CHECK( rendered.find( "fd_blood" ) != std::string::npos );
    CHECK( rendered.find( "cadaver=true" ) != std::string::npos );
    CHECK( rendered.find( "casquillo=true" ) != std::string::npos );
    CHECK( rendered.find( "RELACION ACTUAL" ) != std::string::npos );
    CHECK( rendered.find( "MODO DE INSPECCION DETALLADA ACTIVO" ) != std::string::npos );
}

TEST_CASE( "npc_ai_scene_inspection_intent_recognizes_broad_requests",
           "[npc_ai][npc_ai_scene]" )
{
    CHECK( npc_ai::is_scene_inspection_query( "Dime absolutamente todo lo que ves." ) );
    CHECK( npc_ai::is_scene_inspection_query( "Que hay aqui?" ) );
    CHECK( npc_ai::is_scene_inspection_query( "Analiza la escena." ) );
    CHECK( npc_ai::is_scene_inspection_query( "Describe esta habitacion." ) );
    CHECK_FALSE( npc_ai::is_scene_inspection_query( "Enciende la cocina." ) );
}

TEST_CASE( "npc_ai_scene_snapshot_does_not_expose_hidden_blood",
           "[npc_ai][npc_ai_scene]" )
{
    npc &observer = prepare_scene_observer();
    map &here = get_map();
    here.ter_set( relative_tile( observer, 1 ), ter_wall_metal );
    REQUIRE( here.add_field( relative_tile( observer, 3 ), fd_blood, 3 ) );
    refresh_visibility( observer );

    const npc_ai::npc_sensory_snapshot snapshot = npc_ai::build_sensory_snapshot( observer );
    CHECK( snapshot.current_tile_at( 3, 0, 0 ) == nullptr );
    CHECK( std::none_of( snapshot.relationships.begin(), snapshot.relationships.end(),
    []( const npc_ai::sensory_spatial_relationship & relationship ) {
        return relationship.subject_id == fd_blood.str() || relationship.object_id == fd_blood.str();
    } ) );
}
