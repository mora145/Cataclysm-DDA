#include "npc_ai_self.h"

#include <algorithm>
#include <cstdint>
#include <functional>
#include <sstream>
#include <string>
#include <vector>

#include "bodypart.h"
#include "character.h"
#include "effect.h"
#include "field_type.h"
#include "flag.h"
#include "item.h"
#include "item_location.h"
#include "item_pocket.h"
#include "itype.h"
#include "iuse_actor.h"
#include "map.h"
#include "npc.h"
#include "output.h"
#include "units.h"

namespace
{

static const efftype_id effect_bite( "bite" );
static const efftype_id effect_bleed( "bleed" );
static const efftype_id effect_infected( "infected" );
static const std::string comesttype_DRINK( "DRINK" );
static const std::string comesttype_FOOD( "FOOD" );

bool has_sealed_parent( item_location location )
{
    while( location && location.has_parent() ) {
        const item_pocket *pocket = location.parent_pocket();
        if( pocket != nullptr && pocket->sealed() ) {
            return true;
        }
        location = location.parent_item();
    }
    return false;
}

bool is_usable_firestarter( const npc &who, const item &candidate )
{
    item *usable = const_cast<item &>( candidate ).get_usable_item( "firestarter" );
    if( usable == nullptr ) {
        return false;
    }
    const use_function *use = usable->type->get_use( "firestarter" );
    if( use == nullptr || use->get_actor_ptr() == nullptr ) {
        return false;
    }
    const firestarter_actor *actor = dynamic_cast<const firestarter_actor *>( use->get_actor_ptr() );
    return actor != nullptr && actor->can_use( who, *usable, &get_map(),
           who.pos_bub( get_map() ) ).success();
}

void add_resource( npc_ai::self_resource_summary &resource,
                   const npc_ai::self_item_observation &item )
{
    resource.available = true;
    resource.item_count += std::max( item.count, 1 );
    if( std::find( resource.item_ids.begin(), resource.item_ids.end(), item.id ) ==
        resource.item_ids.end() ) {
        resource.item_ids.push_back( item.id );
    }
}

std::string render_resource( const char *name, const npc_ai::self_resource_summary &resource )
{
    std::ostringstream output;
    output << name << "={disponible=" << ( resource.available ? "true" : "false" )
           << "; cantidad=" << resource.item_count << "; ids=[";
    for( std::size_t index = 0; index < resource.item_ids.size(); ++index ) {
        if( index > 0 ) {
            output << ",";
        }
        output << resource.item_ids[index];
    }
    output << "]}\n";
    return output.str();
}

const char *bodypart_damage_severity( const npc_ai::self_bodypart_observation &part )
{
    if( part.hp_max <= 0 || part.hp_current >= part.hp_max ) {
        return "ninguna";
    }
    const int percent = std::max( 0, part.hp_current ) * 100 / part.hp_max;
    if( percent <= 25 ) {
        return "grave";
    }
    if( percent <= 50 ) {
        return "seria";
    }
    return "leve";
}

} // namespace

namespace npc_ai
{

npc_self_snapshot build_self_snapshot( const npc &who, const self_snapshot_scope scope )
{
    npc_self_snapshot snapshot;
    snapshot.hunger = { who.get_hunger(), who.get_hunger() > 100 };
    snapshot.thirst = { who.get_thirst(), who.get_thirst() > 40 };
    snapshot.sleepiness = { who.get_sleepiness(), who.get_sleepiness() >= sleepiness_levels::TIRED };
    snapshot.sleep_deprivation = who.get_sleep_deprivation();
    snapshot.perceived_pain = who.get_perceived_pain();
    snapshot.health_percent = who.hp_percentage();
    snapshot.morale = who.get_morale_level();
    snapshot.stamina = who.get_stamina();
    snapshot.stamina_max = who.get_stamina_max();
    snapshot.carried_weight_gram = units::to_gram( who.weight_carried() );
    snapshot.weight_capacity_gram = units::to_gram( who.weight_capacity() );
    snapshot.carried_volume_ml = units::to_milliliter( who.volume_carried() );
    snapshot.volume_capacity_ml = units::to_milliliter( who.volume_capacity() );

    for( const bodypart_id &part : who.get_all_body_parts( get_body_part_flags::only_main ) ) {
        self_bodypart_observation observation;
        observation.id = part.id().str();
        observation.name = body_part_name( part );
        observation.hp_current = who.get_part_hp_cur( part );
        observation.hp_max = who.get_part_hp_max( part );
        observation.temperature_c = static_cast<int>( units::to_celsius( who.get_part_temp_cur( part ) ) );
        observation.encumbrance = who.encumb( part );
        observation.bleeding_intensity = who.get_effect_int( effect_bleed, part );
        observation.cold = who.get_part_temp_cur( part ) <= BODYTEMP_COLD;
        observation.hot = who.get_part_temp_cur( part ) >= BODYTEMP_HOT;
        observation.bitten = who.has_effect( effect_bite, part.id() );
        observation.infected = who.has_effect( effect_infected, part.id() );
        observation.broken = who.is_limb_broken( part );
        snapshot.cold = snapshot.cold || observation.cold;
        snapshot.hot = snapshot.hot || observation.hot;
        snapshot.bleeding = snapshot.bleeding || observation.bleeding_intensity > 0;
        snapshot.infected = snapshot.infected || observation.infected;
        snapshot.broken_limb = snapshot.broken_limb || observation.broken;
        snapshot.bodyparts.push_back( std::move( observation ) );
    }

    for( const std::reference_wrapper<const effect> &effect_ref : who.get_effects() ) {
        const effect &active_effect = effect_ref.get();
        const effect_type *type = active_effect.get_effect_type();
        if( active_effect.is_null() || type == nullptr || !type->is_show_in_info() ) {
            continue;
        }
        self_effect_observation observation;
        observation.id = active_effect.get_id().str();
        observation.name = active_effect.disp_name();
        observation.bodypart_id = active_effect.get_bp().id().str();
        observation.intensity = active_effect.get_intensity();
        snapshot.effects.push_back( std::move( observation ) );
    }

    if( scope == self_snapshot_scope::physical_state ) {
        return snapshot;
    }
    snapshot.inventory_scanned = true;

    const item_location wielded = who.get_wielded_item();
    const item *wielded_item = wielded ? wielded.get_item() : nullptr;
    for( item_location &location : const_cast<npc &>( who ).all_items_loc() ) {
        if( !location ) {
            continue;
        }
        const item &owned_item = *location;
        self_item_observation observation;
        observation.id = owned_item.typeId().str();
        observation.name = remove_color_tags( owned_item.tname() );
        observation.count = owned_item.count();
        observation.sealed = has_sealed_parent( location );
        observation.accessible = !observation.sealed;
        observation.wielded = &owned_item == wielded_item;
        observation.worn = who.is_worn( owned_item );
        observation.contained = location.has_parent();
        observation.container = !owned_item.get_all_contained_pockets().empty();
        observation.medicine = owned_item.is_medication();
        observation.weapon = owned_item.is_gun() || owned_item.is_maybe_melee_weapon();
        observation.ammunition = owned_item.is_ammo() || owned_item.is_magazine();
        observation.tool = owned_item.is_tool();
        observation.firestarter = observation.accessible && is_usable_firestarter( who, owned_item );

        if( owned_item.is_comestible() && observation.accessible &&
            who.will_eat( owned_item ).success() ) {
            const auto &comestible = owned_item.get_comestible();
            observation.drink = comestible &&
                                comestible->comesttype == comesttype_DRINK && comestible->quench > 0;
            observation.food = comestible &&
                               ( comestible->comesttype == comesttype_FOOD ||
                                 owned_item.has_flag( flag_USE_EAT_VERB ) );
        }

        if( observation.accessible ) {
            if( observation.food ) {
                add_resource( snapshot.usable_food, observation );
            }
            if( observation.drink ) {
                add_resource( snapshot.potable_drink, observation );
            }
            if( observation.medicine ) {
                add_resource( snapshot.medicine, observation );
            }
            if( observation.weapon ) {
                add_resource( snapshot.weapons, observation );
            }
            if( observation.ammunition ) {
                add_resource( snapshot.ammunition, observation );
            }
            if( observation.tool ) {
                add_resource( snapshot.tools, observation );
            }
            if( observation.firestarter ) {
                add_resource( snapshot.firestarters, observation );
            }
        }
        snapshot.items.push_back( std::move( observation ) );
    }

    return snapshot;
}

std::string render_self_snapshot( const npc_self_snapshot &snapshot )
{
    std::ostringstream output;
    output << "\n=== ESTADO PROPIO ACTUAL (HECHOS DE CDDA) ===\n"
           << "hambre={valor=" << snapshot.hunger.value
           << "; necesidad=" << ( snapshot.hunger.active ? "true" : "false" ) << "}\n"
           << "sed={valor=" << snapshot.thirst.value
           << "; necesidad=" << ( snapshot.thirst.active ? "true" : "false" ) << "}\n"
           << "sueno={valor=" << snapshot.sleepiness.value
           << "; necesidad=" << ( snapshot.sleepiness.active ? "true" : "false" ) << "}\n"
           << "privacion_sueno=" << snapshot.sleep_deprivation << "\n"
           << "dolor_percibido=" << snapshot.perceived_pain << "\n"
           << "salud_global_porcentaje=" << snapshot.health_percent << "\n"
           << "moral=" << snapshot.morale << "\n"
           << "resistencia={actual=" << snapshot.stamina << "; maxima=" << snapshot.stamina_max << "}\n"
           << "carga={peso_g=" << snapshot.carried_weight_gram
           << "; capacidad_peso_g=" << snapshot.weight_capacity_gram
           << "; volumen_ml=" << snapshot.carried_volume_ml
           << "; capacidad_volumen_ml=" << snapshot.volume_capacity_ml << "}\n"
           << "estado_corporal={frio=" << ( snapshot.cold ? "true" : "false" )
           << "; calor=" << ( snapshot.hot ? "true" : "false" )
           << "; sangrado=" << ( snapshot.bleeding ? "true" : "false" )
           << "; infeccion=" << ( snapshot.infected ? "true" : "false" )
           << "; miembro_roto=" << ( snapshot.broken_limb ? "true" : "false" ) << "}\n";

    // Mobility is derived from the same vanilla limb state.  A survivor with
    // both legs broken cannot walk; one broken leg still moves, slowly.  This
    // closes backlog finding 1 (mobility never reached the NPC context).
    int broken_legs = 0;
    int broken_arms = 0;
    std::string broken_names;
    for( const self_bodypart_observation &part : snapshot.bodyparts ) {
        if( !part.broken ) {
            continue;
        }
        if( part.id == "leg_l" || part.id == "leg_r" ) {
            ++broken_legs;
        } else if( part.id == "arm_l" || part.id == "arm_r" ) {
            ++broken_arms;
        }
        broken_names += ( broken_names.empty() ? "" : "," ) + part.id;
    }
    output << "movilidad={puede_caminar=" << ( broken_legs >= 2 ? "false" : "true" )
           << "; movilidad_reducida=" << ( broken_legs >= 1 ? "true" : "false" )
           << "; piernas_rotas=" << broken_legs
           << "; brazos_rotos=" << broken_arms
           << "; necesita_ferula_o_medico=" << ( snapshot.broken_limb ? "true" : "false" )
           << "; miembros_rotos=[" << broken_names << "]}\n\n"
           << "PARTES CORPORALES AFECTADAS:\n";

    bool affected_bodypart = false;
    for( const self_bodypart_observation &part : snapshot.bodyparts ) {
        if( part.hp_current >= part.hp_max && part.bleeding_intensity == 0 && !part.cold &&
            !part.hot && !part.bitten && !part.infected && !part.broken ) {
            continue;
        }
        affected_bodypart = true;
        output << "- id=" << part.id << "; nombre=" << part.name
               << "; hp=" << part.hp_current << "/" << part.hp_max
               << "; severidad_dano=" << bodypart_damage_severity( part )
               << "; temperatura_c=" << part.temperature_c
               << "; sangrado=" << part.bleeding_intensity
               << "; mordida=" << ( part.bitten ? "true" : "false" )
               << "; infeccion=" << ( part.infected ? "true" : "false" )
               << "; roto=" << ( part.broken ? "true" : "false" ) << "\n";
    }
    if( !affected_bodypart ) {
        output << "- ninguna\n";
    }

    output << "\nEFECTOS ACTIVOS RELEVANTES:\n";
    if( snapshot.effects.empty() ) {
        output << "- ninguno\n";
    } else {
        for( const self_effect_observation &active_effect : snapshot.effects ) {
            output << "- id=" << active_effect.id << "; nombre=" << active_effect.name
                   << "; intensidad=" << active_effect.intensity;
            if( !active_effect.bodypart_id.empty() && active_effect.bodypart_id != "null" ) {
                output << "; parte=" << active_effect.bodypart_id;
            }
            output << "\n";
        }
    }

    output << "\ninventario_consultado=" << ( snapshot.inventory_scanned ? "true" : "false" ) << "\n";
    if( snapshot.inventory_scanned ) {
        output << "RECURSOS QUE POSEES Y PUEDES USAR AHORA:\n"
               << render_resource( "comida_utilizable", snapshot.usable_food )
               << render_resource( "bebida_potable", snapshot.potable_drink )
               << render_resource( "medicina", snapshot.medicine )
               << render_resource( "armas", snapshot.weapons )
               << render_resource( "municion", snapshot.ammunition )
               << render_resource( "herramientas", snapshot.tools )
               << render_resource( "iniciadores_fuego", snapshot.firestarters );
    }

    if( snapshot.inventory_scanned ) {
        output << "\nEQUIPO E INVENTARIO ESTRUCTURADO:\n";
        for( const self_item_observation &item : snapshot.items ) {
            output << "- id=" << item.id << "; nombre=" << item.name
                   << "; cantidad=" << item.count
                   << "; accesible=" << ( item.accessible ? "true" : "false" )
                   << "; sellado=" << ( item.sealed ? "true" : "false" )
                   << "; empunado=" << ( item.wielded ? "true" : "false" )
                   << "; puesto=" << ( item.worn ? "true" : "false" )
                   << "; contenido=" << ( item.contained ? "true" : "false" ) << "\n";
        }
    }

    output << "\nREGLAS DE INTERPRETACION DEL ESTADO PROPIO:\n"
           << "- Los valores anteriores son lecturas de solo lectura del estado vivo de CDDA.\n";
    if( snapshot.inventory_scanned ) {
        output << "- Una necesidad y la disponibilidad de recursos son hechos separados.\n"
               << "- sed=true y bebida_potable.disponible=true significa que tienes sed Y llevas bebida.\n"
               << "- hambre=true y comida_utilizable.disponible=true significa que tienes hambre Y llevas comida.\n"
               << "- Nunca conviertas una necesidad en la afirmacion de que falta el recurso.\n";
    } else {
        output << "- El inventario no fue consultado: no afirmes si posees o te falta un recurso.\n";
    }
    output
           << "- Este estado ACTUAL reemplaza cualquier recuerdo conversacional contradictorio.\n"
           // Live scenario run 2026-09-03: with arm_r at 10/100 (grave) the
           // model still answered "estoy bien" because the aggregates read 94 %
           // health and pain 0.  Severity is an obligation, not one more field.
           << "- Si alguna parte listada tiene severidad_dano=grave o critica, roto=true, "
           "sangrado>0, mordida=true o infeccion=true, NO puedes decir que estas bien: "
           "nombra esa parte y su estado.\n"
           << "- En ese caso estan prohibidas las frases \"estoy bien\", \"no es nada\", "
           "\"puedo cuidarme solo\" y equivalentes, tambien al final de la respuesta. "
           "Di lo que te pasa y lo que necesitas.\n"
           << "- salud_global_porcentaje y dolor_percibido NO anulan una parte grave; "
           "una parte grave con dolor bajo sigue siendo grave.\n"
           << "- puede_caminar=false significa que no puedes andar por ti mismo y necesitas "
           "que te ayuden o te lleven.\n"
           << "- dolor_percibido es la unica medida de dolor: con 0 no digas que te duele; "
           "con menos de 15 es leve; solo por encima de 40 es fuerte.\n"
           << "- No inventes el origen, la historia, la causa ni la localizacion de nada que "
           "no aparezca aqui. Si un dato no trae parte del cuerpo, no digas donde.\n";
    return output.str();
}

} // namespace npc_ai
