#include "npc_ai_wield.h"

#include <cstddef>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "cata_utility.h"
#include "item.h"
#include "item_location.h"
#include "map.h"
#include "map_selector.h"
#include "messages.h"
#include "npc.h"
#include "npc_ai_async.h"
#include "npc_ai_context.h"
#include "npc_ai_debug.h"
#include "npc_ai_memory.h"
#include "output.h"
#include "ret_val.h"
#include "rng.h"
#include "string_formatter.h"
#include "translations.h"
#include "units.h"

namespace
{

struct wield_candidate {
    item_location location;
    std::string item_id;
    std::string name;
    std::string kind;
    std::string location_description;
    bool ground = false;
};


bool looks_like_wield_command(
    const std::string &text
)
{
    static const std::vector<std::string> verbs = {
        "equipate",
        "equipa",
        "equipar",
        "empuna",
        "empunar",
        "empunala",
        "empunalo",
        "wield"
    };


    for(
        const std::string &verb :
        verbs
    ) {

        if(
            lcmatch(
                text,
                verb
            )
        ) {

            return true;
        }
    }


    return false;
}


std::string candidate_kind(
    const item &it
)
{
    if(
        it.is_gun()
    ) {

        return "arma de fuego";
    }


    if(
        it.is_maybe_melee_weapon()
    ) {

        return "posible arma cuerpo a cuerpo";
    }


    return "objeto";
}


std::string build_wield_prompt(
    const std::string &player_line,
    const std::vector<wield_candidate> &candidates
)
{
    std::ostringstream prompt;


    prompt
        << "ORDEN DEL JUGADOR:\n"
        << player_line
        << "\n\n"

        << "ARMAS CANDIDATAS REALES:\n";


    for(
        std::size_t i = 0;
        i < candidates.size();
        ++i
    ) {

        const wield_candidate &candidate =
            candidates[i];


        prompt
            << ( i + 1 )
            << " | id="
            << candidate.item_id
            << " | nombre="
            << candidate.name
            << " | tipo="
            << candidate.kind
            << " | ubicacion="
            << candidate.location_description
            << "\n";
    }


    return prompt.str();
}


int parse_wield_index(
    const std::string &response
)
{
    const std::string marker =
        "WIELD_INDEX=";


    const std::size_t marker_position =
        response.find(
            marker
        );


    if(
        marker_position ==
        std::string::npos
    ) {

        return -1;
    }


    std::size_t position =
        marker_position +
        marker.size();


    if(
        position >=
        response.size()
    ) {

        return -1;
    }


    bool found_digit =
        false;


    int value =
        0;


    while(
        position <
        response.size()
    ) {

        const char c =
            response[position];


        if(
            c < '0' ||
            c > '9'
        ) {

            break;
        }


        found_digit =
            true;


        value =
            value * 10 +
            static_cast<int>(
                c - '0'
            );


        ++position;
    }


    if(
        !found_digit
    ) {

        return -1;
    }


    return value;
}

} // namespace


namespace npc_ai
{

wield_target_result validate_wield_target( npc &who, const item_location &target,
        const bool allow_drop_previous )
{
    wield_target_result result;
    if( !target ) {
        result.message = npc_ai::localized_ai_message(
                             _( "That item is no longer available." ),
                             "Ese objeto ya no está disponible." );
        return result;
    }

    const item_location current = who.get_wielded_item();
    if( current && current.get_item() == target.get_item() ) {
        result.success = true;
        return result;
    }

    if( current && current->has_item( *target ) ) {
        result.message = string_format( npc_ai::localized_ai_message(
                             _( "I need to put away %s before wielding something from it." ),
                             "Necesito guardar %s antes de empuñar algo que contiene." ),
                             remove_color_tags( current->tname() ) );
        return result;
    }

    const ret_val<void> can_wield = who.can_wield( *target );
    if( !can_wield.success() ) {
        result.message = can_wield.str();
        return result;
    }

    const bool target_already_carried =
        target.where_recursive() == item_location::type::character;
    units::mass carried_after = who.weight_carried();
    if( !target_already_carried ) {
        carried_after += target->weight();
    }

    if( current ) {
        result.previous_name = remove_color_tags( current->tname() );
        const bool can_keep_previous = who.can_wear( *current ).success() ||
                                       who.can_stash( *current );
        const bool must_drop_for_storage = !can_keep_previous;
        const bool must_drop_for_weight = carried_after > who.weight_capacity();
        result.drops_previous = must_drop_for_storage || must_drop_for_weight;
        // Never let npc::wield() implicitly drop the previous weapon while it
        // still holds a reference to the new target.  A drop can restack the
        // target's map tile, so perform it explicitly and revalidate the safe
        // item_location before passing it to the vanilla wield operation.
        result.drop_previous_before_wield = result.drops_previous;

        if( result.drops_previous && !allow_drop_previous ) {
            result.message = string_format( npc_ai::localized_ai_message(
                                 _( "I can't put away %s, so I need to keep it in my hands." ),
                                 "No puedo guardar %s, así que necesito conservarlo en las manos." ),
                                 result.previous_name );
            return result;
        }

        if( result.drops_previous ) {
            carried_after -= current->weight();
            if( carried_after > who.weight_capacity() ) {
                result.message = npc_ai::localized_ai_message(
                                     _( "Carrying that item would exceed my weight capacity." ),
                                     "Llevar ese objeto superaría mi capacidad de peso." );
                return result;
            }
            const ret_val<void> can_drop = who.can_drop( *current );
            if( !can_drop.success() ) {
                result.message = string_format( npc_ai::localized_ai_message(
                                                    _( "I can't set down %1$s: %2$s" ),
                                                    "No puedo dejar %1$s en el suelo: %2$s" ),
                                                result.previous_name, can_drop.str() );
                return result;
            }
        }
    } else if( carried_after > who.weight_capacity() ) {
        result.message = npc_ai::localized_ai_message(
                             _( "Carrying that item would exceed my weight capacity." ),
                             "Llevar ese objeto superaría mi capacidad de peso." );
        return result;
    }

    result.success = true;
    return result;
}

wield_target_result wield_target( npc &who, item_location target,
                                  const bool allow_drop_previous )
{
    wield_target_result result = validate_wield_target( who, target, allow_drop_previous );
    if( !result.success || !target ) {
        return result;
    }

    const std::string target_name = remove_color_tags( target->tname() );
    const itype_id target_id = target->typeId();
    const item_location current = who.get_wielded_item();
    if( current && current.get_item() == target.get_item() ) {
        result.message = string_format( npc_ai::localized_ai_message(
                                            _( "I already have %s in my hands." ),
                                            "Ya tengo %s en las manos." ), target_name );
        return result;
    }

    if( result.drop_previous_before_wield && current ) {
        const drop_locations previous = { { current, current->count() } };
        who.drop( previous, who.pos_bub( get_map() ), false );
        const item_location after_drop = who.get_wielded_item();
        if( after_drop ) {
            result.success = false;
            result.message = string_format( npc_ai::localized_ai_message(
                                                _( "I couldn't set down %s." ),
                                                "No pude dejar %s en el suelo." ),
                                            result.previous_name );
            return result;
        }
        if( !target ) {
            result.success = false;
            result.message = string_format( npc_ai::localized_ai_message(
                                 _( "I set down %1$s, but %2$s moved or was combined with another item." ),
                                 "Dejé %1$s en el suelo, pero %2$s se movió o se combinó con otro objeto." ),
                                 result.previous_name, target_name );
            return result;
        }
    }

    if( !who.wield( target ) ) {
        result.success = false;
        result.message = string_format( npc_ai::localized_ai_message(
                                            _( "I couldn't wield %s." ),
                                            "No pude empuñar %s." ), target_name );
        return result;
    }

    const item_location wielded = who.get_wielded_item();
    if( !wielded || wielded->typeId() != target_id ) {
        result.success = false;
        result.message = string_format( npc_ai::localized_ai_message(
                                            _( "I tried to wield %s, but it isn't in my hands." ),
                                            "Intenté empuñar %s, pero no está en mis manos." ),
                                        target_name );
        return result;
    }

    result.message = result.drops_previous ?
                     string_format( npc_ai::localized_ai_message(
                                        _( "I couldn't put away %1$s, so I set it down and wielded %2$s." ),
                                        "No pude guardar %1$s, así que lo dejé en el suelo y empuñé %2$s." ),
                                    result.previous_name, remove_color_tags( wielded->tname() ) ) :
                     string_format( npc_ai::localized_ai_message(
                                        _( "I'm wielding %s." ),
                                        "Estoy empuñando %s." ),
                                    remove_color_tags( wielded->tname() ) );
    return result;
}

wield_command_result try_handle_wield_command_impl(
    npc &who,
    const std::string &player_line,
    const ai_request_completion *completion
)
{
    wield_command_result result;


    if(
        !looks_like_wield_command(
            player_line
        )
    ) {

        return result;
    }


    result.handled =
        true;


    npc_ai::debug_stream debug( "npc_ai_wield_v1_runtime.txt", true );


    if( debug ) {

        debug
            << "CDDA-AI WIELD V1 RUNTIME\n"
            << "NPC="
            << who.get_name()
            << "\n"
            << "REQUEST="
            << player_line
            << "\n\n";
    }


    // ============================================================
    // Estado actual de las manos
    // ============================================================

    const item_location currently_wielded =
        who.get_wielded_item();


    if( debug ) {

        if(
            currently_wielded
        ) {

            debug
                << "CURRENTLY_WIELDED="
                << remove_color_tags( currently_wielded->tname() )
                << " | id="
                << currently_wielded->typeId().str()
                << "\n\n";
        }
        else {

            debug
                << "CURRENTLY_WIELDED=NONE\n\n";
        }
    }


    // ============================================================
    // Candidatos del inventario REAL
    //
    // all_items_loc() incluye objetos contenidos dentro de otros
    // objetos, por ejemplo una escopeta dentro de una mochila.
    // ============================================================

    std::vector<wield_candidate> candidates;


    std::vector<item_location> inventory_items =
        who.all_items_loc();


    for(
        item_location &location :
        inventory_items
    ) {

        if(
            !location
        ) {

            continue;
        }


        const item &it =
            *location;


        // WIELD V1 solo trabaja con armas.
        //
        // Ropa, cascos, chalecos, zapatos, etc. se manejaran
        // posteriormente mediante WEAR/AUTO-EQUIP.
        if(
            !it.is_gun() &&
            !it.is_maybe_melee_weapon()
        ) {

            continue;
        }


        wield_candidate candidate;


        candidate.location =
            location;


        candidate.item_id =
            it.typeId().str();


        candidate.name =
            remove_color_tags( it.tname() );


        candidate.kind =
            candidate_kind(
                it
            );


        candidate.location_description =
            location.describe(
                &who
            );


        candidate.ground = false;


        candidates.push_back(
            candidate
        );


        if(
            candidates.size() >= 20
        ) {

            break;
        }
    }


    // Immediate ground weapons use the directed acquisition engine below.
    // Farther ground targets are discovered first by the pickup dispatcher,
    // which owns the same movement and transfer pipeline.
    map &here = get_map();
    const tripoint_bub_ms origin = who.pos_bub( here );
    for( const tripoint_bub_ms &position : here.points_in_radius( origin, 1 ) ) {
        if( position.z() != origin.z() || !who.sees( here, position ) ||
            !here.could_see_items( position, who ) ||
            !here.clear_path( origin, position, 1, 1, 100 ) ) {
            continue;
        }
        for( item &it : here.i_at( position ) ) {
            if( !it.is_gun() && !it.is_maybe_melee_weapon() ) {
                continue;
            }
            wield_candidate candidate;
            candidate.location = item_location( map_cursor( &here, position ), &it );
            candidate.item_id = it.typeId().str();
            candidate.name = remove_color_tags( it.tname() );
            candidate.kind = candidate_kind( it );
            candidate.location_description = "suelo " + position.to_string_writable();
            candidate.ground = true;
            candidates.push_back( std::move( candidate ) );
            if( candidates.size() >= 20 ) {
                break;
            }
        }
        if( candidates.size() >= 20 ) {
            break;
        }
    }


    if( debug ) {

        debug
            << "CANDIDATES="
            << candidates.size()
            << "\n";


        for(
            std::size_t i = 0;
            i < candidates.size();
            ++i
        ) {

            debug
                << ( i + 1 )
                << " | id="
                << candidates[i].item_id
                << " | name="
                << candidates[i].name
                << " | kind="
                << candidates[i].kind
                << " | location="
                << candidates[i].location_description
                << "\n";
        }


        debug
            << "\n";
    }


    if(
        candidates.empty()
    ) {

        result.message =
            "No tengo ningun arma disponible para empunar.";


        if( debug ) {

            debug
                << "RESULT=NO_WEAPON_CANDIDATES\n";
        }


        return result;
    }


    // ============================================================
    // Qwen resuelve SOLO la semantica.
    // ============================================================

    const std::string resolver_prompt =
        build_wield_prompt(
            player_line,
            candidates
        );


    if( debug ) {

        debug
            << "=== RESOLVER PROMPT ===\n"
            << resolver_prompt
            << "\n";
    }


    ai_response ai;
    if( completion == nullptr ) {
        // A single real candidate needs no semantic model call.  This also
        // ensures a direct physical order remains deterministic without Ollama.
        if( candidates.size() == 1 ) {
            ai = { true, "WIELD_INDEX=1", "" };
        } else {
            std::vector<ai_target_snapshot> targets;
            targets.reserve( candidates.size() );
            for( wield_candidate &candidate : candidates ) {
                std::string uid = candidate.location->get_var( "npc_ai_async_target_uid" );
                if( uid.empty() ) {
                    uid = "wield-" + random_string( 16 );
                    candidate.location->set_var( "npc_ai_async_target_uid", uid );
                }
                targets.push_back( { uid, candidate.item_id, candidate.name,
                                     who.pos_abs().x(), who.pos_abs().y(), who.pos_abs().z() } );
            }
            const ai_enqueue_result queued = enqueue_command_resolution(
                                                 who, ai_request_type::wield_resolution, player_line,
                                                 resolver_prompt, std::move( targets ) );
            result.pending = queued.accepted;
            if( !queued.accepted ) {
                result.message = "Todavia estoy considerando tu solicitud anterior.";
            }
            return result;
        }
    }

    if( completion != nullptr ) {
        ai = completion->response;
    }
    if( completion != nullptr && ai.success ) {
        const int original_index = parse_wield_index( ai.text );
        if( original_index <= 0 ||
            original_index > static_cast<int>( completion->request.targets.size() ) ) {
            ai = { false, "", "invalid target index" };
        } else {
            const std::string &uid = completion->request.targets[
                                         static_cast<std::size_t>( original_index - 1 )].uid;
            int current_index = 0;
            for( std::size_t i = 0; i < candidates.size(); ++i ) {
                if( candidates[i].location->get_var( "npc_ai_async_target_uid" ) == uid ) {
                    current_index = static_cast<int>( i + 1 );
                    break;
                }
            }
            ai.text = "WIELD_INDEX=" + std::to_string( current_index );
        }
    }


    if(
        !ai.success
    ) {

        result.message =
            "No pude determinar con seguridad que arma quieres que equipe.";


        if( debug ) {

            debug
                << "OLLAMA_SUCCESS=no\n"
                << "OLLAMA_ERROR="
                << ai.error
                << "\n";
        }


        return result;
    }


    if( debug ) {

        debug
            << "OLLAMA_SUCCESS=yes\n"
            << "OLLAMA_RESPONSE="
            << ai.text
            << "\n";
    }


    const int selected_index =
        parse_wield_index(
            ai.text
        );


    if( debug ) {

        debug
            << "PARSED_INDEX="
            << selected_index
            << "\n";
    }


    if(
        selected_index <= 0 ||
        selected_index >
        static_cast<int>(
            candidates.size()
        )
    ) {

        result.message =
            "No encuentro con seguridad el arma a la que te refieres.";


        if( debug ) {

            debug
                << "RESULT=NO_SAFE_MATCH\n";
        }


        return result;
    }


    wield_candidate &selected =
        candidates[
            static_cast<std::size_t>(
                selected_index - 1
            )
        ];


    // Guardar datos ANTES de wield(), porque wield()
    // puede invalidar el item_location original.
    const std::string selected_name =
        selected.name;


    const std::string selected_id =
        selected.item_id;


    if(
        !selected.location
    ) {

        result.message =
            "Esa arma ya no esta disponible.";


        if( debug ) {

            debug
                << "RESULT=TARGET_INVALID\n";
        }


        return result;
    }


    if( debug ) {

        debug
            << "SELECTED_ID="
            << selected_id
            << "\n"
            << "SELECTED_NAME="
            << selected_name
            << "\n"
            << "SELECTED_LOCATION="
            << selected.location_description
            << "\nSOURCE="
            << ( selected.ground ? "ground" : "owned" )
            << "\n"

            << "WIELD_START\n";
    }


    // ============================================================
    // ACCION REAL DEL MOTOR
    //
    // Character::wield(item_location) mueve el objeto desde su
    // ubicacion actual a las manos y cobra los movimientos reales.
    //
    // Si la escopeta esta dentro de la mochila, CDDA la saca.
    // ============================================================

    const item_location current_weapon = who.get_wielded_item();
    if( current_weapon && current_weapon.get_item() == selected.location.get_item() ) {
        result.success = true;
        result.message = std::string( "Ya tengo " ) + selected_name + " en las manos.";
        add_msg_debug( debugmode::DF_NPC_ITEMAI,
                       "%s WIELD_RESULT=already_wielded TARGET_ITEM=%s", who.get_name(),
                       selected_name );
        return result;
    }

    if( selected.ground ) {
        const tripoint_bub_ms target_position = selected.location.pos_bub( here );
        std::string error;
        const acquisition_intent_classification classification =
            classify_acquisition_intent( player_line );
        if( !who.ai_request_pickup( selected.location, target_position, error, true,
                                    player_line, acquisition_intent::wield,
                                    classification.source.empty() ?
                                    "explicit_wield:wield_router" : classification.source ) ) {
            result.message = string_format( npc_ai::localized_ai_message(
                                                _( "I can't pick up %1$s to wield it: %2$s" ),
                                                "No puedo recoger %1$s para empuñarlo: %2$s" ),
                                            selected_name, error );
            return result;
        }
        result.success = true;
        result.message = string_format( npc_ai::localized_ai_message(
                                            _( "I'm going to pick up and wield %s." ),
                                            "Voy a recoger y empuñar %s." ), selected_name );
        return result;
    }

    const wield_target_result wielded_result = wield_target( who, selected.location, true );
    add_msg_debug( debugmode::DF_NPC_ITEMAI,
                   "%s EQUIP_INTENT=WIELD TARGET_ITEM=%s TARGET_LOCATION=%s SOURCE=%s "
                   "WIELD_VALIDATION=%s",
                   who.get_name(), selected_name, selected.location_description,
                   selected.ground ? "ground" : "owned",
                   wielded_result.success ? "success" : wielded_result.message );
    if( !wielded_result.success ) {
        result.message = string_format( npc_ai::localized_ai_message(
                                            _( "I can't wield %1$s: %2$s" ),
                                            "No puedo empuñar %1$s: %2$s" ), selected_name,
                                        wielded_result.message );
        if( debug ) {
            debug << "RESULT=WIELD_FAILED\n";
        }
        return result;
    }


    const item_location after_wield =
        who.get_wielded_item();


    if(
        !after_wield
    ) {

        result.message =
            std::string( "Intente equipar " ) +
            selected_name +
            ", pero no quedo en mis manos.";


        if( debug ) {

            debug
                << "RESULT=NO_ITEM_IN_HANDS_AFTER_WIELD\n";
        }


        return result;
    }


    result.success =
        true;

    add_msg_debug( debugmode::DF_NPC_ITEMAI, "%s WIELD_RESULT=success TARGET_ITEM=%s",
                   who.get_name(), selected_name );


    result.message = wielded_result.message;


    if( debug ) {

        debug
            << "RESULT=WIELD_SUCCESS\n"
            << "NOW_WIELDED_ID="
            << after_wield->typeId().str()
            << "\n"
            << "NOW_WIELDED_NAME="
            << remove_color_tags( after_wield->tname() )
            << "\n";
    }


    return result;
}

wield_command_result try_handle_wield_command( npc &who, const std::string &player_line )
{
    return try_handle_wield_command_impl( who, player_line, nullptr );
}

void apply_wield_ai_completion( npc &who, const ai_request_completion &completion )
{
    wield_command_result result = try_handle_wield_command_impl(
                                      who, completion.request.player_line, &completion );
    if( result.message.empty() ) {
        result.message = "No pude determinar con seguridad que arma quieres que equipe.";
    }
    say_command_reply( who, result.message );
    remember_exchange( who, completion.request.player_line, result.message );
}

} // namespace npc_ai
