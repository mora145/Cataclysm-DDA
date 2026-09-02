#include "npc_ai_watchlist.h"
#include "npc_ai_world_memory.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <string>
#include <unordered_map>
#include <vector>

#include "cata_path.h"
#include "filesystem.h"
#include "item.h"
#include "map.h"
#include "npc.h"
#include "output.h"
#include "npc_ai_profiler.h"
#include "path_info.h"
#include "worldfactory.h"

namespace
{

using watch_targets = std::vector<std::string>;

std::unordered_map<std::string, watch_targets> watch_cache;

std::string trim_copy( std::string text )
{
    const auto not_space = []( const unsigned char c ) {
        return !std::isspace( c );
    };

    text.erase(
        text.begin(),
        std::find_if( text.begin(), text.end(), not_space )
    );

    text.erase(
        std::find_if( text.rbegin(), text.rend(), not_space ).base(),
        text.end()
    );

    return text;
}

std::string lower_ascii( std::string text )
{
    std::transform(
        text.begin(),
        text.end(),
        text.begin(),
        []( const unsigned char c ) {
            return static_cast<char>( std::tolower( c ) );
        }
    );

    return text;
}

std::filesystem::path watch_directory()
{
    if( world_generator &&
        world_generator->active_world != nullptr ) {

        const cata_path dir =
            world_generator->active_world->folder_path() /
            "npc_ai_memory";

        return dir.get_unrelative_path();
    }

    return std::filesystem::u8path(
               PATH_INFO::user_dir()
           ) / "npc_ai_memory";
}

std::filesystem::path watch_file( const npc &who )
{
    const int npc_id = who.getID().get_value();

    std::string filename;

    if( npc_id > 0 ) {
        filename =
            "npc_" +
            std::to_string( npc_id ) +
            ".watch";
    } else {
        filename =
            "npc_" +
            ensure_valid_file_name( who.get_name() ) +
            ".watch";
    }

    return watch_directory() / filename;
}

watch_targets load_targets( const npc &who )
{
    watch_targets result;

    const std::filesystem::path path =
        watch_file( who );

    if( !file_exist( path ) ) {
        return result;
    }

    std::ifstream input(
        path,
        std::ios::binary
    );

    if( !input.is_open() ) {
        return result;
    }

    std::string line;

    while( std::getline( input, line ) ) {
        line = lower_ascii(
                   trim_copy( line )
               );

        if( !line.empty() ) {
            result.push_back( line );
        }
    }

    return result;
}

watch_targets &targets_for( const npc &who )
{
    const std::string key =
        watch_file( who ).generic_u8string();

    const auto found =
        watch_cache.find( key );

    if( found != watch_cache.end() ) {
        return found->second;
    }

    watch_cache.emplace(
        key,
        load_targets( who )
    );

    return watch_cache.at( key );
}

void save_targets(
    const npc &who,
    const watch_targets &targets
)
{
    const std::filesystem::path directory =
        watch_directory();

    if( !assure_dir_exist( directory ) ) {
        return;
    }

    std::ofstream output(
        watch_file( who ),
        std::ios::binary |
        std::ios::trunc
    );

    if( !output.is_open() ) {
        return;
    }

    for( const std::string &target : targets ) {
        output << target << '\n';
    }
}

bool add_target(
    const npc &who,
    std::string target
)
{
    target = lower_ascii(
                 trim_copy( target )
             );

    // Temporalmente permitimos selectores largos.
    // El Item Resolver V2 los reemplazara por IDs compactos.
    if( target.empty() ||
        target.size() > 4096 ) {
        return false;
    }

    std::replace(
        target.begin(),
        target.end(),
        '\n',
        ' '
    );

    std::replace(
        target.begin(),
        target.end(),
        '\r',
        ' '
    );

    watch_targets &targets =
        targets_for( who );

    if( std::find(
            targets.begin(),
            targets.end(),
            target
        ) != targets.end() ) {

        return true;
    }

    targets.push_back( target );

    save_targets(
        who,
        targets
    );

    return true;
}

bool matches_target( const item &it, const std::string &target )
{
    const std::string normalized = lower_ascii( target );

    // ----------------------------------------------------
    // Categorias semanticas reales de CDDA
    // ----------------------------------------------------

    if( normalized == "@magazine" ) {
        return it.is_magazine();
    }

    if( normalized == "@gun" ) {
        return it.is_gun();
    }

    if( normalized == "@ammo" ) {
        return it.is_ammo();
    }

    // ----------------------------------------------------
    // IDs internos reales de CDDA
    //
    // Formatos aceptados:
    //
    // @id:radio
    //
    // @id:combat_boots|winter_boots|rubber_boots
    //
    // Tambien tolera:
    //
    // @id:combat_boots|@id:winter_boots
    // ----------------------------------------------------

    if( normalized.rfind( "@id:", 0 ) == 0 ) {

        const std::string actual_id =
            lower_ascii( it.typeId().str() );

        std::size_t begin = 4;

        while( begin <= normalized.size() ) {

            const std::size_t end =
                normalized.find( '|', begin );

            std::string candidate;

            if( end == std::string::npos ) {
                candidate = normalized.substr( begin );
            } else {
                candidate =
                    normalized.substr( begin, end - begin );
            }

            // Permitir:
            // @id:a|@id:b
            if( candidate.rfind( "@id:", 0 ) == 0 ) {
                candidate = candidate.substr( 4 );
            }

            const std::size_t first =
                candidate.find_first_not_of( " \t\r\n" );

            if( first != std::string::npos ) {

                const std::size_t last =
                    candidate.find_last_not_of( " \t\r\n" );

                candidate =
                    candidate.substr(
                        first,
                        last - first + 1
                    );

            } else {
                candidate.clear();
            }

            if( !candidate.empty() &&
                actual_id == candidate ) {

                return true;
            }

            if( end == std::string::npos ) {
                break;
            }

            begin = end + 1;
        }

        return false;
    }

    // ----------------------------------------------------
    // Compatibilidad con el sistema antiguo.
    // Sigue permitiendo terminos de texto.
    // ----------------------------------------------------

    const std::string item_name =
        lower_ascii( remove_color_tags( it.tname() ) );

    std::size_t begin = 0;

    while( begin <= normalized.size() ) {

        const std::size_t end =
            normalized.find( '|', begin );

        std::string term;

        if( end == std::string::npos ) {
            term = normalized.substr( begin );
        } else {
            term =
                normalized.substr( begin, end - begin );
        }

        const std::size_t first =
            term.find_first_not_of( " \t\r\n" );

        if( first != std::string::npos ) {

            const std::size_t last =
                term.find_last_not_of( " \t\r\n" );

            term =
                term.substr(
                    first,
                    last - first + 1
                );

        } else {
            term.clear();
        }

        if( !term.empty() &&
            item_name.find( term ) != std::string::npos ) {

            return true;
        }

        if( end == std::string::npos ) {
            break;
        }

        begin = end + 1;
    }

    return false;
}

} // namespace

namespace npc_ai
{

bool apply_watch_control(
    const npc &who,
    std::string &speech
)
{
    const std::string marker =
        "[[WATCH_ITEM:";

    const std::size_t start =
        speech.find( marker );

    if( start == std::string::npos ) {
        return false;
    }

    const std::size_t end =
        speech.find(
            "]]",
            start + marker.size()
        );

    if( end == std::string::npos ) {
        return false;
    }

    std::string target =
        speech.substr(
            start + marker.size(),
            end - ( start + marker.size() )
        );

    target = trim_copy( target );

    const bool added =
        add_target(
            who,
            target
        );

    // El jugador nunca debe ver la instruccion interna.
    speech.erase(
        start,
        end + 2 - start
    );

    speech = trim_copy( speech );

    if( speech.empty() && added ) {
        speech =
            "Vale. Te aviso si lo veo.";
    }

    return added;
}

std::string build_watchlist_context(
    const npc &who
)
{
    const watch_targets &targets =
        targets_for( who );

    std::string result;

    result +=
        "=== ENCARGOS ACTIVOS DE VIGILANCIA ===\n";

    if( targets.empty() ) {
        result +=
            "No tienes busquedas de objetos activas.\n";

        return result;
    }

    result +=
        "El jugador te pidio que le avises si ves:\n";

    for( const std::string &target : targets ) {
        result += "- " + target + "\n";
    }

    return result;
}

void check_item_watchlist( npc &who )
{
    scoped_profile profile( profile_subsystem::watchlist );
    watch_targets &targets =
        targets_for( who );

    // Esta es la ruta normal: sin busquedas activas
    // no se escanea absolutamente nada.
    if( targets.empty() ) {
        return;
    }

    map &here = get_map();

    for( const tripoint_bub_ms &p :
         here.points_in_radius(
             who.pos_bub( here ),
             6
         ) ) {

        if( !who.sees( here, p ) ) {
            continue;
        }

        if( !here.could_see_items( p, who ) ) {
            continue;
        }

        for( const item &it : here.i_at( p ) ) {

            for( std::size_t i = 0;
                 i < targets.size();
                 ++i ) {

                if( !matches_target(
                        it,
                        targets[i]
                    ) ) {

                    continue;
                }

                const std::string alert =
                    "Oye, encontre " +
                    remove_color_tags( it.tname() ) +
                    ". Me pediste que te avisara.";

                // Convertimos la posicion local del mapa
                // a coordenadas absolutas persistentes.
                const tripoint_abs_ms abs_pos =
                    here.get_abs( p );

                npc_ai::remember_seen_item(
                    who,
                    it.typeId().str(),
                    remove_color_tags( it.tname() ),
                    abs_pos.x(),
                    abs_pos.y(),
                    abs_pos.z()
                );

                who.say( alert );

                // Una sola notificacion por defecto.
                // Una vez encontrado, el encargo queda cumplido.
                targets.erase(
                    targets.begin() + i
                );

                save_targets(
                    who,
                    targets
                );

                return;
            }
        }
    }
}

void reset_watch_cache()
{
    // The watchlists themselves stay on disk; only the in-memory mirror keyed
    // by the previous world's file paths is dropped.
    watch_cache.clear();
}

} // namespace npc_ai
