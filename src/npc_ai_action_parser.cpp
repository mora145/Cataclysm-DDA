#include "npc_ai_action_parser.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "npc.h"
#include "npc_ai_async.h"
#include "npc_ai_context.h"
#include "npc_ai_item_catalog.h"
#include "npc_ai_debug.h"
#include "npc_ai_memory.h"
#include "npc_ai_watchlist.h"
#include "translations.h"

namespace
{

std::string trim_copy(
    const std::string &text
)
{
    const std::size_t first =
        text.find_first_not_of(
            " \t\r\n"
        );

    if( first == std::string::npos ) {
        return "";
    }

    const std::size_t last =
        text.find_last_not_of(
            " \t\r\n"
        );

    return text.substr(
               first,
               last - first + 1
           );
}

std::string lower_ascii(
    std::string text
)
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

std::string upper_ascii(
    std::string text
)
{
    std::transform(
        text.begin(),
        text.end(),
        text.begin(),
        []( unsigned char c ) {
            return static_cast<char>(
                       std::toupper( c )
                   );
        }
    );

    return text;
}

std::vector<std::string> split(
    const std::string &text,
    char separator
)
{
    std::vector<std::string> result;

    std::stringstream stream( text );
    std::string token;

    while( std::getline(
               stream,
               token,
               separator
           ) ) {

        token = trim_copy( token );

        if( !token.empty() ) {
            result.push_back(
                token
            );
        }
    }

    return result;
}

bool looks_like_watch_request(
    const std::string &line
)
{
    const std::string text =
        lower_ascii( line );

    return
        text.find( "avisa" ) != std::string::npos ||
        text.find( "avís" ) != std::string::npos ||
        text.find( "dime si" ) != std::string::npos ||
        text.find( "cuando ve" ) != std::string::npos ||
        text.find( "si ves" ) != std::string::npos ||
        text.find( "si veas" ) != std::string::npos ||
        text.find( "si encuent" ) != std::string::npos ||
        text.find( "cuando encuent" ) != std::string::npos ||
        text.find( "let me know" ) != std::string::npos ||
        text.find( "tell me if" ) != std::string::npos ||
        text.find( "notify me" ) != std::string::npos;
}

std::string sanitize_control_term(
    std::string text
)
{
    for( char &c : text ) {

        if( c == '|' ||
            c == '[' ||
            c == ']' ||
            c == '\r' ||
            c == '\n' ) {

            c = ' ';
        }
    }

    return trim_copy( text );
}

void debug_log(
    const npc_ai::watch_action_result &result,
    const std::string &player_line
)
{
    npc_ai::debug_stream output( "npc_ai_watch_debug.log" );

    if( !output ) {
        return;
    }

    output
        << "\n========================================\n"
        << "PLAYER: "
        << player_line
        << "\n"
        << "PARSER_RAW: "
        << result.raw_output
        << "\n"
        << "ATTEMPTED: "
        << ( result.attempted ? "YES" : "NO" )
        << "\n"
        << "SUCCESS: "
        << ( result.success ? "YES" : "NO" )
        << "\n"
        << "WATCH: "
        << ( result.is_watch ? "YES" : "NO" )
        << "\n"
        << "KIND: "
        << result.kind
        << "\n"
        << "CATALOG_TYPES: "
        << result.catalog_size
        << "\n";

    output << "TERMS:";

    for( const std::string &term :
         result.terms ) {

        output
            << " ["
            << term
            << "]";
    }

    output << "\nCANDIDATES:";

    for( const std::string &candidate :
         result.candidates ) {

        output
            << "\n  - "
            << candidate;
    }

    output
        << "\nCONTROL: "
        << result.control_marker
        << "\n";
}

} // namespace

namespace npc_ai
{

static watch_action_result parse_watch_action_impl( npc *who, const std::string &player_line,
        const std::string *model_output )
{
    watch_action_result result;

    if( !looks_like_watch_request(
            player_line
        ) ) {

        return result;
    }

    result.attempted = true;

    std::ostringstream prompt;

    prompt
        << "FRASE DEL JUGADOR:\n"
        << player_line;

    npc_ai::ai_response ai;
    if( model_output != nullptr ) {
        ai = { true, *model_output, "" };
    } else {
        const ai_enqueue_result queued = enqueue_command_resolution(
                *who, ai_request_type::watch_resolution, player_line, prompt.str() );
        result.pending = queued.accepted;
        result.raw_output = queued.accepted ? "PENDING" : "ERROR: " + queued.error;
        return result;
    }

    if( !ai.success ) {

        result.raw_output =
            "ERROR: " + ai.error;

        debug_log(
            result,
            player_line
        );

        return result;
    }

    result.raw_output =
        trim_copy( ai.text );

    std::string machine_line;

    std::stringstream lines(
        result.raw_output
    );

    std::string line;

    while( std::getline(
               lines,
               line
           ) ) {

        const std::string trimmed =
            trim_copy( line );

        if( trimmed == "NONE" ||
            trimmed.find( "WATCH|" ) == 0 ||
            trimmed.find( "WATCH;" ) == 0 ) {

            machine_line = trimmed;
            break;
        }
    }

    if( machine_line.empty() ) {

        debug_log(
            result,
            player_line
        );

        return result;
    }

    if( machine_line == "NONE" ) {

        // CDDA-AI:
        // El detector local ya decidio que esta frase parece una
        // solicitud de vigilancia. Si contiene una orden explicita
        // de "avisame", damos al parser un segundo intento.
        //
        // Esto evita que una variacion aleatoria de Qwen convierta:
        //
        //   "avisame si ves una camiseta"
        //
        // unas veces en WATCH y otras en NONE.

        const std::string lower_player =
            lower_ascii( player_line );

        const bool explicit_watch_order =
            lower_player.find( "avisame" ) != std::string::npos ||
            lower_player.find( "avísame" ) != std::string::npos ||
            lower_player.find( "avisa si" ) != std::string::npos ||
            lower_player.find( "avisa cuando" ) != std::string::npos;

        if( explicit_watch_order ) {

            std::ostringstream retry_prompt;

            retry_prompt
                << "Eres un parser de ordenes para "
                << "Cataclysm: Dark Days Ahead.\n"
                << "NO converses.\n"
                << "NO expliques nada.\n"
                << "La frase siguiente YA fue identificada como "
                << "una orden explicita de vigilancia futura.\n"
                << "NO puedes responder NONE.\n\n"

                << "Devuelve EXACTAMENTE uno de estos formatos:\n"
                << "WATCH|MAGAZINE|magazine\n"
                << "WATCH|GUN|gun\n"
                << "WATCH|AMMO|ammo\n"
                << "WATCH|SPECIFIC|term1;term2;term3\n\n"

                << "MAGAZINE = cargador fisico de un arma.\n"
                << "GUN = arma de fuego.\n"
                << "AMMO = municion, balas o cartuchos.\n"
                << "SPECIFIC = cualquier otro objeto concreto.\n"
                << "Para SPECIFIC usa de 1 a 5 terminos utiles "
                << "en ingles.\n\n"

                << "FRASE:\n"
                << player_line;

            // A retry would be a second background request and could reorder
            // direct conversation turns.  Treat NONE as a valid no-action
            // result; the main-thread completion handler falls back to normal
            // asynchronous conversation instead.
            const npc_ai::ai_response retry = { false, "", "" };

            if( retry.success ) {

                const std::string retry_raw =
                    trim_copy( retry.text );

                result.raw_output +=
                    "\nRETRY: " + retry_raw;

                std::stringstream retry_lines(
                    retry_raw
                );

                std::string retry_line;

                while( std::getline(
                           retry_lines,
                           retry_line
                       ) ) {

                    const std::string trimmed =
                        trim_copy( retry_line );

                    if( trimmed.find( "WATCH|" ) == 0 ||
                        trimmed.find( "WATCH;" ) == 0 ) {

                        machine_line = trimmed;
                        break;
                    }
                }
            }
        }

        // Si incluso el segundo intento no pudo producir una
        // orden WATCH valida, mantenemos NONE.
        if( machine_line == "NONE" ) {

            result.success = true;
            result.is_watch = false;

            debug_log(
                result,
                player_line
            );

            return result;
        }
    }

    // CDDA-AI: normaliza errores comunes del formato WATCH.
    //
    // Correcto:
    // WATCH|SPECIFIC|shirt;t-shirt
    //
    // Variantes toleradas:
    // WATCH|SPECIFIC;shirt;t-shirt
    // WATCH;SPECIFIC|shirt;t-shirt

    if( machine_line.rfind( "WATCH;", 0 ) == 0 ) {
        machine_line[5] = '|';
    }

    if( machine_line.rfind( "WATCH|", 0 ) == 0 ) {

        const std::size_t first_separator =
            machine_line.find( '|' );

        const std::size_t second_separator =
            machine_line.find(
                '|',
                first_separator + 1
            );

        // Si falta el segundo |, Qwen probablemente escribio ;
        // entre KIND y la lista de terminos.
        if( second_separator == std::string::npos ) {

            const std::size_t wrong_separator =
                machine_line.find(
                    ';',
                    first_separator + 1
                );

            if( wrong_separator != std::string::npos ) {
                machine_line[wrong_separator] = '|';
            }
        }
    }
    const std::vector<std::string> fields =
        split(
            machine_line,
            '|'
        );

    if( fields.size() < 3 ) {

        debug_log(
            result,
            player_line
        );

        return result;
    }

    if( upper_ascii(
            fields[0]
        ) != "WATCH" ) {

        debug_log(
            result,
            player_line
        );

        return result;
    }

    result.kind =
        upper_ascii(
            fields[1]
        );

    const std::string allowed_kind =
        result.kind;

    if( allowed_kind != "MAGAZINE" &&
        allowed_kind != "GUN" &&
        allowed_kind != "AMMO" &&
        allowed_kind != "SPECIFIC" ) {

        debug_log(
            result,
            player_line
        );

        return result;
    }

    result.terms =
        split(
            fields[2],
            ';'
        );

    const npc_ai::item_catalog_result catalog =
        npc_ai::resolve_item_candidates(
            result.kind,
            result.terms,
            40
        );

    result.catalog_size =
        catalog.total_types;

    result.candidates =
        catalog.candidates;

    if( result.kind == "MAGAZINE" ) {

        result.control_marker =
            "[[WATCH_ITEM:@MAGAZINE]]";

    } else if( result.kind == "GUN" ) {

        result.control_marker =
            "[[WATCH_ITEM:@GUN]]";

    } else if( result.kind == "AMMO" ) {

        result.control_marker =
            "[[WATCH_ITEM:@AMMO]]";

    } else {

        // Resolver V2:
        //
        // SPECIFIC ya no almacena nombres traducidos.
        // Guarda IDs internos reales de CDDA.
        //
        // Ejemplo:
        //
        // @id:tshirt|tshirt_red|longshirt
        //
        // El nombre visible puede cambiar de idioma;
        // el ID interno permanece estable.

        std::ostringstream target;

        bool first = true;
        std::size_t added_ids = 0;

        for( std::string id :
             catalog.candidate_ids ) {

            // Evitamos selectores gigantes.
            if( added_ids >= 24 ) {
                break;
            }

            id =
                sanitize_control_term(
                    id
                );

            if( id.empty() ) {
                continue;
            }

            if( first ) {
                target << "@id:";
            } else {
                target << "|";
            }

            target << id;

            first = false;
            ++added_ids;
        }

        if( first ) {

            // Fallback temporal.
            //
            // Si el catalogo no pudo resolver ningun ID,
            // conservamos los terminos del parser para no
            // perder completamente la funcionalidad.
            //
            // Mas adelante podremos eliminar este fallback
            // cuando el resolver semantico este completo.

            for( std::string term :
                 result.terms ) {

                term =
                    sanitize_control_term(
                        term
                    );

                if( term.empty() ) {
                    continue;
                }

                if( !first ) {
                    target << "|";
                }

                target << term;
                first = false;
            }
        }

        if( first ) {

            debug_log(
                result,
                player_line
            );

            return result;
        }

        result.control_marker =
            "[[WATCH_ITEM:" +
            target.str() +
            "]]";
    }

    result.success = true;
    result.is_watch = true;

    debug_log(
        result,
        player_line
    );

    return result;
}

watch_action_result parse_watch_action( npc &who, const std::string &player_line )
{
    return parse_watch_action_impl( &who, player_line, nullptr );
}

watch_action_result parse_watch_action_response( const std::string &player_line,
        const std::string &model_output )
{
    return parse_watch_action_impl( nullptr, player_line, &model_output );
}

void apply_watch_ai_completion( npc &who, const ai_request_completion &completion )
{
    if( !completion.response.success ) {
        who.say( _( "I couldn't understand what you wanted me to watch for." ) );
        return;
    }

    const watch_action_result result = parse_watch_action_response(
                                           completion.request.player_line, completion.response.text );
    if( result.success && result.is_watch && !result.control_marker.empty() ) {
        std::string control = result.control_marker;
        apply_watch_control( who, control );
        const std::string reply = _( "I'll let you know if I see it." );
        who.say( reply );
        remember_exchange( who, completion.request.player_line, reply );
        return;
    }

    enqueue_direct_dialogue( who, completion.request.player_line,
                             build_npc_prompt( who, completion.request.player_line ),
                             completion.request.conversation_id );
}

void strip_watch_markers(
    std::string &text
)
{
    const std::string prefix =
        "[[WATCH_ITEM:";

    while( true ) {

        const std::size_t begin =
            text.find( prefix );

        if( begin == std::string::npos ) {
            break;
        }

        const std::size_t end =
            text.find(
                "]]",
                begin
            );

        if( end == std::string::npos ) {
            text.erase( begin );
            break;
        }

        text.erase(
            begin,
            end - begin + 2
        );
    }

    text = trim_copy( text );
}

} // namespace npc_ai
