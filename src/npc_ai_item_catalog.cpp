#include "npc_ai_item_catalog.h"

#include <algorithm>
#include <cctype>
#include <sstream>
#include <string>
#include <vector>

#include "item_factory.h"
#include "itype.h"

namespace
{

std::string lower_ascii( std::string text )
{
    std::transform(
        text.begin(),
        text.end(),
        text.begin(),
        []( unsigned char c ) {
            return static_cast<char>(
                       std::tolower( c )
                   );
        }
    );

    return text;
}

// Convierte:
//
// combat_boots
// combat-boots
// combat boots
//
// en una representacion comparable:
//
// combat boots
//
// Tambien elimina signos que provocaban falsos positivos raros.
std::string normalize_search_text(
    const std::string &text
)
{
    const std::string lowered =
        lower_ascii( text );

    std::string normalized;
    normalized.reserve( lowered.size() );

    bool previous_space = true;

    for( unsigned char c : lowered ) {

        if( std::isalnum( c ) ) {

            normalized.push_back(
                static_cast<char>( c )
            );

            previous_space = false;

        } else {

            if( !previous_space ) {
                normalized.push_back( ' ' );
                previous_space = true;
            }
        }
    }

    while( !normalized.empty() &&
           normalized.back() == ' ' ) {

        normalized.pop_back();
    }

    return normalized;
}

std::string remove_spaces(
    const std::string &text
)
{
    std::string result;

    for( char c : text ) {
        if( c != ' ' ) {
            result.push_back( c );
        }
    }

    return result;
}

// Busca terminos completos.
//
// Evita, por ejemplo:
//
// boot
//
// coincidiendo accidentalmente con:
//
// rebooter
//
// Pero permite:
//
// combat_boots -> "boots"
// t-shirt      -> "tshirt"
bool contains_search_term(
    const std::string &text,
    const std::string &term
)
{
    const std::string normalized_text =
        normalize_search_text( text );

    const std::string normalized_term =
        normalize_search_text( term );

    if( normalized_text.empty() ||
        normalized_term.empty() ) {

        return false;
    }

    // Coincidencia como palabra o frase completa.
    const std::string padded_text =
        " " + normalized_text + " ";

    const std::string padded_term =
        " " + normalized_term + " ";

    if( padded_text.find( padded_term ) !=
        std::string::npos ) {

        return true;
    }

    // Caso especial para IDs como:
    //
    // tshirt
    //
    // frente a:
    //
    // t-shirt
    const std::string compact_term =
        remove_spaces( normalized_term );

    std::stringstream words(
        normalized_text
    );

    std::string word;

    while( words >> word ) {

        if( word == compact_term ) {
            return true;
        }
    }

    return false;
}

bool contains_any(
    const std::string &text,
    const std::vector<std::string> &terms
)
{
    for( const std::string &term : terms ) {

        if( contains_search_term(
                text,
                term
            ) ) {

            return true;
        }
    }

    return false;
}

bool matches_kind(
    const itype &def,
    const std::string &kind
)
{
    const std::string lowered =
        lower_ascii( kind );

    if( lowered == "magazine" ) {
        return static_cast<bool>(
                   def.magazine
               );
    }

    if( lowered == "gun" ) {
        return static_cast<bool>(
                   def.gun
               );
    }

    if( lowered == "ammo" ) {
        return static_cast<bool>(
                   def.ammo
               );
    }

    if( lowered == "tool" ) {
        return static_cast<bool>(
                   def.tool
               );
    }

    if( lowered == "book" ) {
        return static_cast<bool>(
                   def.book
               );
    }

    if( lowered == "food" ) {
        return static_cast<bool>(
                   def.comestible
               );
    }

    if( lowered == "armor" ) {
        return static_cast<bool>(
                   def.armor
               );
    }

    // SPECIFIC:
    // puede ser cualquier clase de objeto.
    return true;
}

} // namespace

namespace npc_ai
{

item_catalog_result resolve_item_candidates(
    const std::string &kind,
    const std::vector<std::string> &terms,
    std::size_t max_candidates
)
{
    item_catalog_result result;

    if( !item_controller ) {
        return result;
    }

    const std::vector<itype> &all =
        item_controller
        ->get_generic_factory()
        .get_all();

    result.total_types = all.size();

    const std::string lowered_kind =
        lower_ascii( kind );

    for( const itype &def : all ) {

        if( !matches_kind(
                def,
                lowered_kind
            ) ) {

            continue;
        }

        const std::string id =
            def.get_id().str();

        const std::string name =
            def.nname( 1 );

        if( lowered_kind == "specific" ) {

            // Primero intentamos contra el ID interno.
            //
            // Esto es especialmente importante cuando
            // el juego esta en español pero Qwen entrega
            // terminos ingleses.
            const bool id_match =
                contains_any(
                    id,
                    terms
                );

            // Dejamos tambien el nombre visible como
            // segunda oportunidad.
            const bool name_match =
                contains_any(
                    name,
                    terms
                );

            if( !id_match &&
                !name_match ) {

                continue;
            }
        }

        result.candidate_ids.push_back(
            id
        );

        // Para debug mostramos nombre + ID.
        result.candidates.push_back(
            name +
            " [id=" +
            id +
            "]"
        );

        if( result.candidate_ids.size() >=
            max_candidates ) {

            break;
        }
    }

    return result;
}

} // namespace npc_ai