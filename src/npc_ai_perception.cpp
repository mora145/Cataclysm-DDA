#include "npc_ai_perception.h"

#include <algorithm>
#include <cstddef>
#include <cstdlib>
#include <map>
#include <sstream>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "character.h"
#include "creature.h"
#include "field.h"
#include "field_type.h"
#include "flag.h"
#include "game.h"
#include "game_constants.h"
#include "item.h"
#include "item_location.h"
#include "map.h"
#include "mapdata.h"
#include "map_scale_constants.h"
#include "npc.h"
#include "output.h"
#include "npc_ai_self.h"
#include "npc_ai_profiler.h"
#include "trap.h"
#include "vehicle.h"
#include "vpart_position.h"

namespace
{

constexpr std::size_t sensory_radius_xy = 6;
constexpr std::size_t ordinary_tile_render_limit = 32;
constexpr std::size_t item_render_limit_per_tile = 8;
constexpr std::size_t detailed_ordinary_tile_render_limit = 96;
constexpr std::size_t relationship_limit = 128;

bool is_blood_field( const std::string &id )
{
    return id == fd_blood.str() || id == fd_blood_insect.str() ||
           id == fd_blood_invertebrate.str() || id == fd_blood_veggy.str();
}

bool is_gore_field( const std::string &id )
{
    return id == fd_gibs_flesh.str() || id == fd_gibs_insect.str() ||
           id == fd_gibs_invertebrate.str() || id == fd_gibs_veggy.str();
}

bool tile_has_corpse( const npc_ai::sensory_tile_observation &tile )
{
    return std::any_of( tile.items.begin(), tile.items.end(),
    []( const npc_ai::sensory_item_observation & item ) {
        return item.corpse;
    } );
}

bool tile_has_casing( const npc_ai::sensory_tile_observation &tile )
{
    return std::any_of( tile.items.begin(), tile.items.end(),
    []( const npc_ai::sensory_item_observation & item ) {
        return item.casing;
    } );
}

bool tile_has_weapon( const npc_ai::sensory_tile_observation &tile )
{
    return std::any_of( tile.items.begin(), tile.items.end(),
    []( const npc_ai::sensory_item_observation & item ) {
        return item.weapon;
    } );
}

bool tile_has_blood( const npc_ai::sensory_tile_observation &tile )
{
    return std::any_of( tile.fields.begin(), tile.fields.end(),
    []( const npc_ai::sensory_field_observation & field ) {
        return field.blood;
    } );
}

bool tile_has_gore( const npc_ai::sensory_tile_observation &tile )
{
    return std::any_of( tile.fields.begin(), tile.fields.end(),
    []( const npc_ai::sensory_field_observation & field ) {
        return field.gore;
    } );
}

bool tile_has_smoke( const npc_ai::sensory_tile_observation &tile )
{
    return std::any_of( tile.fields.begin(), tile.fields.end(),
    []( const npc_ai::sensory_field_observation & field ) {
        return field.smoke;
    } );
}

std::string item_kind( const item &it )
{
    if( it.is_gun() ) {
        return "arma de fuego";
    }
    if( it.is_magazine() ) {
        return "cargador de munición; NO es un arma";
    }
    if( it.is_ammo() ) {
        return "munición";
    }
    return "objeto";
}

std::string held_item_name( const Character &character )
{
    const item_location held = character.get_wielded_item();
    return held ? remove_color_tags( held->tname() ) : std::string();
}

int tile_priority( const npc_ai::sensory_tile_observation &tile )
{
    if( tile_has_corpse( tile ) ) {
        return 0;
    }
    if( tile_has_blood( tile ) ) {
        return 1;
    }
    if( tile_has_gore( tile ) ) {
        return 2;
    }
    if( tile_has_casing( tile ) ) {
        return 3;
    }
    if( tile_has_weapon( tile ) ) {
        return 4;
    }
    if( tile.fire.value ) {
        return 5;
    }
    if( tile_has_smoke( tile ) ) {
        return 6;
    }
    if( tile.dangerous_field ) {
        return 7;
    }
    if( tile.goes_up || tile.goes_down || tile.ladder || tile.ramp || tile.climbable ) {
        return 8;
    }
    if( tile.door != npc_ai::sensory_door_state::not_a_door || tile.window ) {
        return 9;
    }
    if( tile.vehicle.knowledge == npc_ai::sensory_knowledge::currently_perceived ) {
        return 10;
    }
    if( tile.fire_container ) {
        return 11;
    }
    if( tile.trap.knowledge == npc_ai::sensory_knowledge::currently_perceived ) {
        return 12;
    }
    if( !tile.furniture_id.empty() || !tile.items.empty() || !tile.fields.empty() || !tile.passable ) {
        return 13;
    }
    if( tile.dx == 0 && tile.dy == 0 && tile.dz == 0 ) {
        return 14;
    }
    return 15;
}

bool is_notable_tile( const npc_ai::sensory_tile_observation &tile )
{
    return tile_priority( tile ) < 15;
}

std::string relative_position( int dx, int dy, int dz, int distance )
{
    return "dx=" + std::to_string( dx ) +
           ", dy=" + std::to_string( dy ) +
           ", dz=" + std::to_string( dz ) +
           ", distancia=" + std::to_string( distance );
}

struct evidence_node {
    std::string kind;
    std::string id;
    std::string name;
    int dx = 0;
    int dy = 0;
    int dz = 0;
    int priority = 0;
    int distance = 0;
};

bool relationship_evidence( const npc_ai::sensory_item_observation &item )
{
    return item.corpse || item.casing || item.weapon;
}

bool relationship_evidence( const npc_ai::sensory_field_observation &field )
{
    return field.blood || field.gore || field.fire || field.smoke || field.dangerous;
}

void build_spatial_relationships( npc_ai::npc_sensory_snapshot &snapshot )
{
    std::vector<evidence_node> evidence;
    for( const npc_ai::sensory_tile_observation &tile : snapshot.tiles ) {
        for( const npc_ai::sensory_item_observation &item : tile.items ) {
            if( relationship_evidence( item ) ) {
                const int priority = item.corpse ? 0 : item.casing ? 2 : 3;
                evidence.push_back( { "objeto", item.id, item.name, tile.dx, tile.dy, tile.dz,
                                      priority, tile.distance } );
            }
        }
        for( const npc_ai::sensory_field_observation &field : tile.fields ) {
            if( relationship_evidence( field ) ) {
                const int priority = field.blood || field.gore ? 1 : field.fire ? 4 :
                                     field.smoke ? 5 : 6;
                evidence.push_back( { "campo", field.id, field.name, tile.dx, tile.dy, tile.dz,
                                      priority, tile.distance } );
            }
        }
    }

    std::stable_sort( evidence.begin(), evidence.end(), []( const evidence_node &lhs,
    const evidence_node &rhs ) {
        return std::tie( lhs.priority, lhs.distance, lhs.dz, lhs.dy, lhs.dx ) <
               std::tie( rhs.priority, rhs.distance, rhs.dz, rhs.dy, rhs.dx );
    } );
    if( evidence.size() > 64 ) {
        evidence.resize( 64 );
    }

    for( std::size_t left = 0; left < evidence.size() &&
         snapshot.relationships.size() < relationship_limit; ++left ) {
        for( std::size_t right = left + 1; right < evidence.size() &&
             snapshot.relationships.size() < relationship_limit; ++right ) {
            const evidence_node &subject = evidence[left];
            const evidence_node &object = evidence[right];
            const int dx = std::abs( subject.dx - object.dx );
            const int dy = std::abs( subject.dy - object.dy );
            const int dz = std::abs( subject.dz - object.dz );
            const int distance = std::max( { dx, dy, dz } );
            if( distance > 3 ) {
                continue;
            }
            npc_ai::sensory_spatial_relationship relationship;
            relationship.subject_kind = subject.kind;
            relationship.subject_id = subject.id;
            relationship.subject_name = subject.name;
            relationship.subject_dx = subject.dx;
            relationship.subject_dy = subject.dy;
            relationship.subject_dz = subject.dz;
            relationship.relation = distance == 0 ? "misma_casilla" :
                                    distance == 1 ? "adyacente" : "cerca";
            relationship.object_kind = object.kind;
            relationship.object_id = object.id;
            relationship.object_name = object.name;
            relationship.object_dx = object.dx;
            relationship.object_dy = object.dy;
            relationship.object_dz = object.dz;
            snapshot.relationships.push_back( std::move( relationship ) );
        }
    }
}

std::string render_self_inventory( const npc &who )
{
    struct inventory_summary {
        std::string id;
        std::string name;
        std::string kind;
        std::string container_name;
        int count = 0;
        bool gun = false;
        bool worn = false;
        bool contained = false;
    };

    std::ostringstream output;
    output << "\n=== TU EQUIPO E INVENTARIO ACTUAL ===\n"
           << "Estos datos representan objetos que TÚ posees actualmente.\n";

    const item_location wielded = who.get_wielded_item();
    const item *wielded_ptr = wielded ? wielded.get_item() : nullptr;
    if( wielded ) {
        output << "En tus manos: " << remove_color_tags( wielded->tname() )
               << " [id=" << wielded->typeId().str()
               << "; tipo=" << item_kind( *wielded ) << "]\n";
    } else {
        output << "En tus manos: nada.\n";
    }

    std::map<std::string, inventory_summary> summaries;
    who.visit_items( [&]( item * it, item * parent ) {
        if( it == nullptr || it == wielded_ptr ) {
            return VisitResponse::NEXT;
        }
        const std::string id = it->typeId().str();
        inventory_summary &entry = summaries[id];
        if( entry.count == 0 ) {
            entry.id = id;
            entry.name = remove_color_tags( it->tname() );
            entry.kind = item_kind( *it );
            entry.contained = parent != nullptr;
            entry.container_name = parent != nullptr ?
                                   remove_color_tags( parent->tname() ) : std::string();
        }
        ++entry.count;
        entry.gun = entry.gun || it->is_gun();
        entry.worn = entry.worn || who.is_worn( *it );
        entry.contained = entry.contained || parent != nullptr;
        return VisitResponse::NEXT;
    } );

    output << "\nARMAS QUE POSEES:\n";
    std::size_t guns = 0;
    for( const std::pair<const std::string, inventory_summary> &pair : summaries ) {
        const inventory_summary &entry = pair.second;
        if( !entry.gun ) {
            continue;
        }
        output << "- " << entry.name << " [id=" << entry.id
               << "; tipo=arma de fuego; cantidad=" << entry.count
               << "; estado=" << ( entry.worn ? "puesto" : "guardado" ) << "]\n";
        ++guns;
    }
    if( guns == 0 ) {
        output << "- No posees armas de fuego actualmente.\n";
    }

    output << "\nEQUIPO QUE LLEVAS PUESTO:\n";
    std::size_t worn = 0;
    for( const std::pair<const std::string, inventory_summary> &pair : summaries ) {
        const inventory_summary &entry = pair.second;
        if( !entry.worn ) {
            continue;
        }
        output << "- " << entry.name << " [id=" << entry.id
               << "; cantidad=" << entry.count << "; estado=puesto]\n";
        ++worn;
    }
    if( worn == 0 ) {
        output << "- No llevas ropa o equipo puesto listado.\n";
    }

    output << "\nOTROS OBJETOS QUE LLEVAS:\n";
    std::size_t general = 0;
    for( const std::pair<const std::string, inventory_summary> &pair : summaries ) {
        const inventory_summary &entry = pair.second;
        if( entry.gun || entry.worn ) {
            continue;
        }
        if( general >= 50 ) {
            break;
        }
        output << "- " << entry.name << " [id=" << entry.id
               << "; tipo=" << entry.kind << "; cantidad=" << entry.count
               << "; estado=guardado";
        if( entry.contained && !entry.container_name.empty() ) {
            output << "; contenedor=" << entry.container_name;
        }
        output << "]\n";
        ++general;
    }
    if( general == 0 ) {
        output << "- No llevas otros objetos guardados.\n";
    } else if( general >= 50 ) {
        output << "- Lista general limitada a 50 TIPOS de objeto.\n";
    }

    output << "\nREGLAS SOBRE TU INVENTARIO:\n"
           << "- 'En tus manos' describe únicamente lo que empuñas ahora.\n"
           << "- estado=guardado significa que posees el objeto, pero no lo empuñas.\n"
           << "- No confundas objetos visibles en el suelo con tu inventario.\n"
           << "- CDDA debe validar cualquier acción física posterior.\n";
    return output.str();
}

} // namespace

namespace npc_ai
{

const sensory_tile_observation *npc_sensory_snapshot::current_tile_at( const int dx,
        const int dy, const int dz ) const
{
    const auto found = std::find_if( tiles.begin(), tiles.end(), [&]( const sensory_tile_observation & tile ) {
        return tile.knowledge == sensory_knowledge::currently_perceived &&
               tile.dx == dx && tile.dy == dy && tile.dz == dz;
    } );
    return found == tiles.end() ? nullptr : &*found;
}

sensory_bool npc_sensory_snapshot::current_fire_at( const int dx, const int dy, const int dz ) const
{
    const sensory_tile_observation *tile = current_tile_at( dx, dy, dz );
    return tile == nullptr ? sensory_bool{} : tile->fire;
}

npc_sensory_snapshot build_sensory_snapshot( const npc &who, const int tile_scan_radius )
{
    npc_ai::scoped_profile profile( npc_ai::profile_subsystem::perception );
    map &here = get_map();
    const tripoint_bub_ms origin = who.pos_bub( here );
    const tripoint_abs_ms absolute = who.pos_abs();
    npc_sensory_snapshot snapshot;
    snapshot.self_x = absolute.x();
    snapshot.self_y = absolute.y();
    snapshot.self_z = absolute.z();
    const int effective_tile_scan_radius = tile_scan_radius < 0 ? MAX_VIEW_DISTANCE :
                                           std::min( tile_scan_radius, MAX_VIEW_DISTANCE );

    const std::vector<Creature *> visible_creatures = g->get_creatures_if( [&]( const Creature & other ) {
        return &other != &who && who.sees( here, other );
    } );
    snapshot.creatures.reserve( visible_creatures.size() );
    for( const Creature *other : visible_creatures ) {
        const tripoint_bub_ms position = other->pos_bub( here );
        sensory_creature_observation observation;
        observation.name = other->is_avatar() ? "el jugador" : other->disp_name();
        observation.player = other->is_avatar();
        observation.npc = other->is_npc();
        observation.dx = position.x() - origin.x();
        observation.dy = position.y() - origin.y();
        observation.dz = position.z() - origin.z();
        observation.distance = rl_dist( origin, position );
        const Creature::Attitude attitude = who.attitude_to( *other );
        observation.attitude = Creature::attitude_raw_string( attitude );
        observation.hostile = attitude == Creature::Attitude::HOSTILE;
        if( const Character *character = other->as_character() ) {
            observation.held_item = held_item_name( *character );
        }
        snapshot.creatures.push_back( std::move( observation ) );
    }

    // Creature perception is collected independently at full sight range
    // above.  Ordinary conversational prompts need grounded nearby
    // terrain/items, not an exhaustive traversal of every tile in every loaded
    // z-level.  Explicit detailed inspection retains the unlimited scan by
    // passing -1.  Enumerating the neighbourhood rather than every z-level and
    // then rejecting by distance keeps the cost proportional to the radius
    // actually requested.
    for( const tripoint_bub_ms &position :
         here.points_in_radius( origin, effective_tile_scan_radius, fov_3d_z_range ) ) {
        if( rl_dist( origin, position ) > effective_tile_scan_radius ) {
            continue;
        }
        const bool extended_z = std::abs( position.z() - origin.z() ) > 1;
        if( extended_z ) {
            const bool traversal =
                here.has_flag( ter_furn_flag::TFLAG_GOES_UP, position ) ||
                here.has_flag( ter_furn_flag::TFLAG_GOES_DOWN, position ) ||
                here.has_flag( ter_furn_flag::TFLAG_LADDER, position ) ||
                here.has_flag( ter_furn_flag::TFLAG_CLIMBABLE, position ) ||
                here.has_flag( ter_furn_flag::TFLAG_RAMP, position ) ||
                here.has_flag( ter_furn_flag::TFLAG_RAMP_UP, position ) ||
                here.has_flag( ter_furn_flag::TFLAG_RAMP_DOWN, position );
            if( !traversal && here.field_at( position ).field_count() == 0 &&
                here.i_at( position ).empty() &&
                !here.veh_at( position ) ) {
                continue;
            }
        }
        if( !who.sees( here, position ) ) {
            continue;
        }

        const int distance = rl_dist( origin, position );
        const bool inside_close_ring = distance <= static_cast<int>( sensory_radius_xy );

        sensory_tile_observation tile;
        tile.dx = position.x() - origin.x();
        tile.dy = position.y() - origin.y();
        tile.dz = position.z() - origin.z();
        tile.distance = distance;
        const ter_t &terrain = here.ter( position ).obj();
        const furn_t &furniture = here.furn( position ).obj();
        tile.terrain_id = terrain.id.str();
        tile.terrain_name = here.tername( position );
        if( here.has_furn( position ) ) {
            tile.furniture_id = furniture.id.str();
            tile.furniture_name = here.furnname( position );
        }
        tile.outside = here.is_outside( position );
        tile.passable = here.passable( position );
        tile.window = here.has_flag( ter_furn_flag::TFLAG_WINDOW, position );
        if( here.has_flag( ter_furn_flag::TFLAG_DOOR, position ) ) {
            tile.door = ( terrain.close || furniture.close ) ? sensory_door_state::open :
                        sensory_door_state::closed;
        }
        tile.goes_up = here.has_flag( ter_furn_flag::TFLAG_GOES_UP, position );
        tile.goes_down = here.has_flag( ter_furn_flag::TFLAG_GOES_DOWN, position );
        tile.ladder = here.has_flag( ter_furn_flag::TFLAG_LADDER, position );
        tile.climbable = here.has_flag( ter_furn_flag::TFLAG_CLIMBABLE, position );
        tile.ramp = here.has_flag( ter_furn_flag::TFLAG_RAMP, position ) ||
                    here.has_flag( ter_furn_flag::TFLAG_RAMP_UP, position ) ||
                    here.has_flag( ter_furn_flag::TFLAG_RAMP_DOWN, position );
        tile.fire_container = here.has_flag( ter_furn_flag::TFLAG_FIRE_CONTAINER, position );

        tile.fire.knowledge = sensory_knowledge::currently_perceived;
        for( const std::pair<const field_type_id, field_entry> &field_pair : here.field_at( position ) ) {
            const field_entry &entry = field_pair.second;
            const field_type &type = field_pair.first.obj();
            if( !entry.is_field_alive() || !type.display_field || type.get_symbol() == "&" ) {
                continue;
            }
            sensory_field_observation field_observation;
            field_observation.id = entry.get_field_type().id().str();
            field_observation.name = entry.name();
            field_observation.intensity = entry.get_field_intensity();
            field_observation.dangerous = entry.is_dangerous();
            field_observation.fire = type.has_fire;
            field_observation.blood = is_blood_field( field_observation.id );
            field_observation.gore = is_gore_field( field_observation.id );
            field_observation.smoke = field_observation.id.find( "smoke" ) != std::string::npos;
            tile.dangerous_field = tile.dangerous_field || field_observation.dangerous;
            if( field_observation.fire ) {
                tile.fire.value = true;
                tile.fire_intensity = std::max( tile.fire_intensity, field_observation.intensity );
            }
            tile.fields.push_back( std::move( field_observation ) );
        }

        if( here.could_see_items( position, who ) ) {
            for( const item &it : here.i_at( position ) ) {
                sensory_item_observation item_observation;
                item_observation.id = it.typeId().str();
                item_observation.name = remove_color_tags( it.tname() );
                item_observation.kind = item_kind( it );
                item_observation.corpse = it.is_corpse();
                item_observation.casing = it.has_flag( flag_CASING );
                item_observation.weapon = it.is_gun() || it.is_maybe_melee_weapon();
                tile.items.push_back( std::move( item_observation ) );
            }
        }

        if( here.can_see_trap_at( position, who ) ) {
            const trap &visible_trap = here.tr_at( position );
            if( !visible_trap.is_null() ) {
                tile.trap.knowledge = sensory_knowledge::currently_perceived;
                tile.trap.id = visible_trap.id.str();
                tile.trap.name = visible_trap.name();
            }
        }

        if( const optional_vpart_position vehicle_position = here.veh_at( position ) ) {
            vehicle &visible_vehicle = vehicle_position->vehicle();
            const vehicle_part &part = visible_vehicle.part( vehicle_position->part_index() );
            tile.vehicle.knowledge = sensory_knowledge::currently_perceived;
            tile.vehicle.name = visible_vehicle.name;
            tile.vehicle.part_name = part.name( false );
            tile.vehicle.part_broken = part.is_broken();
            tile.vehicle.moving = visible_vehicle.is_moving();
        }

        if( inside_close_ring || is_notable_tile( tile ) ) {
            snapshot.tiles.push_back( std::move( tile ) );
        }
    }

    build_spatial_relationships( snapshot );
    return snapshot;
}

std::string render_sensory_snapshot( const npc_sensory_snapshot &snapshot,
                                     const bool detailed_scene )
{
    std::ostringstream output;
    output << "=== INSTANTÁNEA SENSORIAL ACTUAL DEL NPC ===\n"
           << "Todo dato marcado ACTUAL fue percibido en esta evaluación.\n"
           << "Tu posición exacta: x=" << snapshot.self_x << ", y=" << snapshot.self_y
           << ", z=" << snapshot.self_z << ".\n\n"
           << "=== CRIATURAS PERCIBIDAS AHORA ===\n";

    std::vector<const sensory_creature_observation *> creatures;
    creatures.reserve( snapshot.creatures.size() );
    for( const sensory_creature_observation &creature : snapshot.creatures ) {
        creatures.push_back( &creature );
    }
    std::sort( creatures.begin(), creatures.end(), []( const auto * lhs, const auto * rhs ) {
        const int lhs_priority = lhs->hostile ? 0 : lhs->player ? 1 : 2;
        const int rhs_priority = rhs->hostile ? 0 : rhs->player ? 1 : 2;
        return std::tie( lhs_priority, lhs->distance, lhs->name ) <
               std::tie( rhs_priority, rhs->distance, rhs->name );
    } );
    if( creatures.empty() ) {
        output << "- Ninguna criatura percibida actualmente.\n";
    } else {
        for( const sensory_creature_observation *creature : creatures ) {
            output << "- ACTUAL: " << creature->name << " ["
                   << relative_position( creature->dx, creature->dy, creature->dz, creature->distance )
                   << "; actitud=" << creature->attitude
                   << "; hostil=" << ( creature->hostile ? "sí" : "no" );
            if( !creature->held_item.empty() ) {
                output << "; en_las_manos=" << creature->held_item;
            }
            output << "]\n";
        }
    }

    output << "\n=== CASILLAS Y ESTADOS OBSERVABLES AHORA ===\n";
    std::vector<const sensory_tile_observation *> tiles;
    for( const sensory_tile_observation &tile : snapshot.tiles ) {
        if( is_notable_tile( tile ) ) {
            tiles.push_back( &tile );
        }
    }
    std::sort( tiles.begin(), tiles.end(), []( const auto * lhs, const auto * rhs ) {
        return std::make_tuple( tile_priority( *lhs ), lhs->distance, lhs->dz, lhs->dy, lhs->dx ) <
               std::make_tuple( tile_priority( *rhs ), rhs->distance, rhs->dz, rhs->dy, rhs->dx );
    } );

    std::size_t ordinary_tiles = 0;
    const std::size_t ordinary_limit = detailed_scene ? detailed_ordinary_tile_render_limit :
                                       ordinary_tile_render_limit;
    for( const sensory_tile_observation *tile : tiles ) {
        const bool ordinary = tile_priority( *tile ) >= 13;
        if( ordinary && ordinary_tiles >= ordinary_limit ) {
            continue;
        }
        ordinary_tiles += ordinary ? 1 : 0;
        output << "- ACTUAL [" << relative_position( tile->dx, tile->dy, tile->dz, tile->distance )
               << "]: terreno=" << tile->terrain_name << " (" << tile->terrain_id << ")";
        if( !tile->furniture_id.empty() ) {
            output << "; mueble=" << tile->furniture_name << " (" << tile->furniture_id << ")";
        }
        output << "; transitable=" << ( tile->passable ? "sí" : "no" )
               << "; fuego=" << ( tile->fire.value ? "true" : "false" );
        if( tile->fire.value ) {
            output << "; intensidad_fuego=" << tile->fire_intensity;
        }
        if( tile->dangerous_field ) {
            output << "; campo_peligroso=true";
        }
        if( tile->door != sensory_door_state::not_a_door ) {
            output << "; puerta=" << ( tile->door == sensory_door_state::open ? "abierta" : "cerrada" );
        }
        if( tile->window ) {
            output << "; ventana=true";
        }
        if( tile->goes_up || tile->goes_down || tile->ladder || tile->climbable || tile->ramp ) {
            output << "; transición_vertical={sube:" << ( tile->goes_up ? "sí" : "no" )
                   << ", baja:" << ( tile->goes_down ? "sí" : "no" )
                   << ", escalera:" << ( tile->ladder ? "sí" : "no" )
                   << ", trepable:" << ( tile->climbable ? "sí" : "no" )
                   << ", rampa:" << ( tile->ramp ? "sí" : "no" ) << "}";
        }
        if( tile->trap.knowledge == sensory_knowledge::currently_perceived ) {
            output << "; trampa_visible=" << tile->trap.name << " (" << tile->trap.id << ")";
        }
        if( tile->vehicle.knowledge == sensory_knowledge::currently_perceived ) {
            output << "; vehículo=" << tile->vehicle.name
                   << "; parte_visible=" << tile->vehicle.part_name
                   << "; parte_rota=" << ( tile->vehicle.part_broken ? "sí" : "no" )
                   << "; en_movimiento=" << ( tile->vehicle.moving ? "sí" : "no" );
        }
        if( !tile->fields.empty() ) {
            output << "; campos_visibles={";
            for( std::size_t index = 0; index < tile->fields.size(); ++index ) {
                const sensory_field_observation &field = tile->fields[index];
                if( index > 0 ) {
                    output << ", ";
                }
                output << field.name << "[id=" << field.id << ", intensidad=" << field.intensity
                       << ", peligroso=" << ( field.dangerous ? "sí" : "no" ) << "]";
            }
            output << "}";
        }
        if( !tile->items.empty() ) {
            output << "; objetos_visibles={";
            std::vector<const sensory_item_observation *> visible_items;
            visible_items.reserve( tile->items.size() );
            for( const sensory_item_observation &item : tile->items ) {
                visible_items.push_back( &item );
            }
            std::stable_sort( visible_items.begin(), visible_items.end(), []( const auto * lhs,
            const auto * rhs ) {
                const int lhs_priority = lhs->corpse ? 0 : lhs->casing ? 1 : lhs->weapon ? 2 : 3;
                const int rhs_priority = rhs->corpse ? 0 : rhs->casing ? 1 : rhs->weapon ? 2 : 3;
                return lhs_priority < rhs_priority;
            } );
            const std::size_t limit = detailed_scene ? visible_items.size() :
                                      std::min( visible_items.size(), item_render_limit_per_tile );
            for( std::size_t index = 0; index < limit; ++index ) {
                const sensory_item_observation &item = *visible_items[index];
                if( index > 0 ) {
                    output << ", ";
                }
                output << item.name << "[id=" << item.id << ", tipo=" << item.kind
                       << ", cadaver=" << ( item.corpse ? "true" : "false" )
                       << ", casquillo=" << ( item.casing ? "true" : "false" )
                       << ", arma=" << ( item.weapon ? "true" : "false" ) << "]";
            }
            if( visible_items.size() > limit ) {
                output << ", más objetos no enumerados";
            }
            output << "}";
        }
        output << "\n";
    }
    if( tiles.empty() ) {
        output << "- No hay casillas destacadas que puedas describir ahora.\n";
    }

    output << "\n=== RELACIONES ESPACIALES ENTRE EVIDENCIAS ACTUALES ===\n";
    if( snapshot.relationships.empty() ) {
        output << "- No hay relaciones relevantes que describir.\n";
    } else {
        for( const sensory_spatial_relationship &relationship : snapshot.relationships ) {
            output << "- RELACION ACTUAL: " << relationship.subject_name
                   << " [" << relationship.subject_kind << "; id=" << relationship.subject_id
                   << "; dx=" << relationship.subject_dx << "; dy=" << relationship.subject_dy
                   << "; dz=" << relationship.subject_dz << "] "
                   << relationship.relation << " de " << relationship.object_name
                   << " [" << relationship.object_kind << "; id=" << relationship.object_id
                   << "; dx=" << relationship.object_dx << "; dy=" << relationship.object_dy
                   << "; dz=" << relationship.object_dz << "].\n";
        }
    }

    if( detailed_scene ) {
        output << "\nMODO DE INSPECCION DETALLADA ACTIVO:\n"
               << "- Describe primero amenazas, personas, cadaveres, sangre, restos, casquillos, "
               << "armas, fuego, humo y peligros.\n"
               << "- Distingue HECHO, INFERENCIA, HIPOTESIS y DESCONOCIDO.\n"
               << "- Las relaciones espaciales son hechos; no prueban por si solas una causa.\n";
    }

    output << "\n=== SEMÁNTICA SENSORIAL OBLIGATORIA ===\n"
           << "- ACTUAL significa percibido en el estado vivo del juego en esta evaluación.\n"
           << "- En una casilla ACTUAL, fuego=false significa que no observas fuego activo allí ahora.\n"
           << "- Una casilla o entidad no observada es DESCONOCIDA, no falsa ni ausente.\n"
           << "- Los recuerdos son PASADOS y nunca pueden contradecir un dato ACTUAL.\n"
           << "- Si un recuerdo dice que algo estaba apagado y una casilla ACTUAL dice fuego=true, "
           << "el estado correcto ahora es encendido.\n";
    return output.str();
}

std::string build_sensory_context( const npc &who, const bool detailed_scene )
{
    constexpr int ordinary_prompt_tile_radius = 12;
    // An exhaustive request used to scan to maximum sight range, which costs a
    // line-of-sight test per tile on every loaded z-level.  The renderer can
    // never emit more than `detailed_ordinary_tile_render_limit` ordinary
    // tiles, so the extra range bought nothing.  Creatures are still gathered
    // at full sight range separately, so distant threats stay visible.
    constexpr int detailed_prompt_tile_radius = 20;
    const npc_sensory_snapshot snapshot = build_sensory_snapshot(
                who, detailed_scene ? detailed_prompt_tile_radius : ordinary_prompt_tile_radius );
    return render_sensory_snapshot( snapshot, detailed_scene );
}

std::string build_perception_context( const npc &who, const bool detailed_scene )
{
    const npc_self_snapshot self = build_self_snapshot( who );
    return build_sensory_context( who, detailed_scene ) + render_self_snapshot( self );
}

} // namespace npc_ai
