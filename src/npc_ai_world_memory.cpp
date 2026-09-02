#include "npc_ai_world_memory.h"

#include "npc_ai_database.h"

#include <deque>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#include "cata_path.h"
#include "filesystem.h"
#include "npc.h"
#include "path_info.h"
#include "worldfactory.h"

namespace
{

std::filesystem::path memory_directory()
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

std::filesystem::path world_memory_file(
    const npc &who
)
{
    const int npc_id =
        who.getID().get_value();

    std::string filename;

    if( npc_id > 0 ) {
        filename =
            "npc_" +
            std::to_string( npc_id ) +
            ".world";
    } else {
        filename =
            "npc_" +
            ensure_valid_file_name(
                who.get_name()
            ) +
            ".world";
    }

    return memory_directory() / filename;
}

std::string sanitize_field(
    std::string text
)
{
    for( char &c : text ) {
        if( c == '\t' ||
            c == '\n' ||
            c == '\r' ) {

            c = ' ';
        }
    }

    return text;
}

std::string clean_world_memory_item_name(
    const std::string &raw
)
{
    std::string cleaned;

    bool inside_tag = false;

    for( const char c : raw ) {

        if( c == '<' ) {
            inside_tag = true;
            continue;
        }

        if( c == '>' && inside_tag ) {
            inside_tag = false;
            continue;
        }

        if( !inside_tag ) {
            cleaned.push_back( c );
        }
    }


    bool changed = true;

    while( changed && !cleaned.empty() ) {

        changed = false;

        while(
            !cleaned.empty() &&
            (
                cleaned.front() == '+' ||
                cleaned.front() == ' ' ||
                cleaned.front() == '\t'
            )
        ) {

            cleaned.erase(
                cleaned.begin()
            );

            changed = true;
        }


        // UTF-8 non-breaking space:
        // C2 A0
        if(
            cleaned.size() >= 2 &&
            static_cast<unsigned char>( cleaned[0] ) == 0xC2 &&
            static_cast<unsigned char>( cleaned[1] ) == 0xA0
        ) {

            cleaned.erase(
                0,
                2
            );

            changed = true;
        }
    }


    return cleaned;
}

} // namespace

namespace npc_ai
{

void remember_seen_item(
    const npc &who,
    const std::string &item_id,
    const std::string &item_name,
    int abs_x,
    int abs_y,
    int abs_z
)
{
    const std::string stored_item_name =
        sanitize_field(
            item_name
        );


    // ============================================================
    // SQLite es la fuente principal.
    //
    // Si el recuerdo ya existe exactamente en la misma posicion,
    // store_seen_item_memory() devuelve inserted = false.
    // En ese caso tampoco repetimos la entrada legacy .world.
    //
    // Si SQLite falla, conservamos .world como fallback.
    // ============================================================

    std::string sqlite_error;
    bool sqlite_inserted = false;

    const bool sqlite_success =
        npc_ai::store_seen_item_memory(
            who,
            item_id,
            stored_item_name,
            abs_x,
            abs_y,
            abs_z,
            sqlite_inserted,
            sqlite_error
        );


    if(
        sqlite_success &&
        !sqlite_inserted
    ) {

        return;
    }


    // ============================================================
    // Legacy / fallback .world
    // ============================================================

    const std::filesystem::path directory =
        memory_directory();


    if(
        !assure_dir_exist(
            directory
        )
    ) {

        return;
    }


    std::ofstream output(
        world_memory_file(
            who
        ),
        std::ios::binary |
        std::ios::app
    );


    if(
        !output.is_open()
    ) {

        return;
    }


    output
        << "SEEN_ITEM"
        << '\t'
        << stored_item_name
        << '\t'
        << abs_x
        << '\t'
        << abs_y
        << '\t'
        << abs_z
        << '\n';
}

std::string build_world_memory_context(
    const npc &who,
    std::size_t max_memories
)
{
    std::ifstream input(
        world_memory_file( who ),
        std::ios::binary
    );

    std::ostringstream result;

    result
        << "=== MEMORIA DEL MUNDO ===\n"
        << "Estos son hechos que observaste directamente en el pasado.\n"
        << "IMPORTANTE: son recuerdos historicos. "
        << "No garantizan que el objeto siga alli ahora.\n";

    // SQLITE WORLD MEMORY PRIMARY READ
    std::string sqlite_error;

    const std::vector<npc_ai::world_memory_record> sqlite_memories =
        npc_ai::load_recent_world_memories(
            who,
            static_cast<int>(
                max_memories
            ),
            sqlite_error
        );


    if(
        sqlite_error.empty() &&
        !sqlite_memories.empty()
    ) {

        for(
            const npc_ai::world_memory_record &memory :
            sqlite_memories
        ) {

            if(
                memory.memory_type != "SEEN_ITEM" ||
                memory.item_name.empty()
            ) {

                continue;
            }


            result
                << "- Viste directamente: "
                << clean_world_memory_item_name(
                    memory.item_name
                )
                << " | posicion absoluta: X="
                << memory.abs_x
                << ", Y="
                << memory.abs_y
                << ", Z="
                << memory.abs_z
                << "\n";
        }


        return result.str();
    }

    if( !input.is_open() ) {
        result
            << "No tienes recuerdos del mundo registrados.\n";

        return result.str();
    }

    std::deque<std::string> recent;
    std::string line;

    while( std::getline( input, line ) ) {

        if( line.empty() ) {
            continue;
        }

        recent.push_back( line );

        while( recent.size() > max_memories ) {
            recent.pop_front();
        }
    }

    if( recent.empty() ) {
        result
            << "No tienes recuerdos del mundo registrados.\n";

        return result.str();
    }

    for( const std::string &entry : recent ) {

        std::istringstream parser( entry );

        std::string type;
        std::string item_name;
        std::string x;
        std::string y;
        std::string z;

        std::getline( parser, type, '\t' );
        std::getline( parser, item_name, '\t' );
        std::getline( parser, x, '\t' );
        std::getline( parser, y, '\t' );
        std::getline( parser, z, '\t' );

        if( type != "SEEN_ITEM" ||
            item_name.empty() ) {

            continue;
        }

        result
            << "- Viste directamente: "
            << item_name
            << " | posicion absoluta: X="
            << x
            << ", Y="
            << y
            << ", Z="
            << z
            << "\n";
    }

    return result.str();
}

} // namespace npc_ai
