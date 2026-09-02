#include "npc_ai_database.h"

#include <filesystem>
#include <string>
#include <system_error>

#include <sqlite3.h>

#include "cata_path.h"
#include "npc.h"
#include "worldfactory.h"

#if defined(_MSC_VER)
#pragma comment(lib, "sqlite3.lib")
#endif

namespace
{

constexpr int npc_ai_schema_version = 4;


bool execute_sql(
    sqlite3 *db,
    const char *sql,
    std::string &error
)
{
    char *sqlite_error = nullptr;

    const int rc =
        sqlite3_exec(
            db,
            sql,
            nullptr,
            nullptr,
            &sqlite_error
        );

    if( rc == SQLITE_OK ) {
        return true;
    }

    if( sqlite_error != nullptr ) {

        error = sqlite_error;

        sqlite3_free(
            sqlite_error
        );

    } else {

        error =
            sqlite3_errmsg( db );
    }

    return false;
}


std::string database_path()
{
    if( !world_generator ||
        world_generator->active_world == nullptr ) {

        return "";
    }

    const cata_path database_file =
        world_generator
        ->active_world
        ->folder_path() /
        "npc_ai_memory" /
        "npc_ai.db";

    return database_file.generic_u8string();
}


bool open_database(
    sqlite3 **db,
    std::string &path,
    std::string &error
)
{
    if( !world_generator ||
        world_generator->active_world == nullptr ) {

        error =
            "No hay un mundo activo.";

        return false;
    }

    const cata_path directory =
        world_generator
        ->active_world
        ->folder_path() /
        "npc_ai_memory";

    const cata_path database_file =
        directory /
        "npc_ai.db";

    path =
        database_file.generic_u8string();

    std::error_code filesystem_error;

    std::filesystem::create_directories(
        directory.get_unrelative_path(),
        filesystem_error
    );

    if( filesystem_error ) {

        error =
            "No se pudo crear el directorio SQLite: " +
            filesystem_error.message();

        return false;
    }

    const int rc =
        sqlite3_open_v2(
            path.c_str(),
            db,
            SQLITE_OPEN_READWRITE |
            SQLITE_OPEN_CREATE,
            nullptr
        );

    if( rc != SQLITE_OK ) {

        if( *db != nullptr ) {

            error =
                sqlite3_errmsg( *db );

            sqlite3_close(
                *db
            );

            *db = nullptr;

        } else {

            error =
                "sqlite3_open_v2 fallo.";
        }

        return false;
    }

    sqlite3_busy_timeout(
        *db,
        500
    );

    return true;
}


bool world_memories_has_item_id(
    sqlite3 *db,
    bool &exists,
    std::string &error
)
{
    exists = false;
    error.clear();

    sqlite3_stmt *statement = nullptr;

    const int prepare_result =
        sqlite3_prepare_v2(
            db,
            "PRAGMA table_info(world_memories);",
            -1,
            &statement,
            nullptr
        );


    if( prepare_result != SQLITE_OK ) {

        error =
            sqlite3_errmsg(
                db
            );

        return false;
    }


    int step_result = SQLITE_ROW;


    while(
        (
            step_result =
                sqlite3_step(
                    statement
                )
        ) == SQLITE_ROW
    ) {

        const unsigned char *column_name =
            sqlite3_column_text(
                statement,
                1
            );


        if(
            column_name != nullptr &&
            std::string(
                reinterpret_cast<const char *>(
                    column_name
                )
            ) == "item_id"
        ) {

            exists = true;
            break;
        }
    }


    if(
        step_result != SQLITE_ROW &&
        step_result != SQLITE_DONE
    ) {

        error =
            sqlite3_errmsg(
                db
            );
    }


    sqlite3_finalize(
        statement
    );


    return error.empty();
}

bool create_schema(
    sqlite3 *db,
    std::string &error
)
{
    // ------------------------------------------------------------
    // Tabla de version.
    // ------------------------------------------------------------

    if( !execute_sql(
            db,
            "CREATE TABLE IF NOT EXISTS schema_version ("
            "singleton INTEGER PRIMARY KEY CHECK(singleton = 1),"
            "version INTEGER NOT NULL"
            ");",
            error
        ) ) {

        return false;
    }


    if( !execute_sql(
            db,
            "INSERT OR IGNORE INTO schema_version "
            "(singleton, version) "
            "VALUES (1, 1);",
            error
        ) ) {

        return false;
    }


    // ------------------------------------------------------------
    // V2: conversaciones persistentes.
    //
    // id       = orden cronologico real de insercion
    // npc_id   = identidad interna de CDDA
    // player   = frase del jugador
    // npc      = respuesta real generada por el NPC
    // ------------------------------------------------------------

    if( !execute_sql(
            db,
            "CREATE TABLE IF NOT EXISTS conversations ("
            "id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "npc_id INTEGER NOT NULL,"
            "player_line TEXT NOT NULL,"
            "npc_line TEXT NOT NULL,"
            "created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP"
            ");",
            error
        ) ) {

        return false;
    }


    if( !execute_sql(
            db,
            "CREATE INDEX IF NOT EXISTS "
            "idx_conversations_npc_id_id "
            "ON conversations(npc_id, id);",
            error
        ) ) {

        return false;
    }


    if( !execute_sql(
            db,
            "CREATE TABLE IF NOT EXISTS world_memories ("
            "id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "npc_id INTEGER NOT NULL,"
            "memory_type TEXT NOT NULL,"
            "item_id TEXT NOT NULL DEFAULT '',"
            "item_name TEXT NOT NULL,"
            "abs_x INTEGER NOT NULL,"
            "abs_y INTEGER NOT NULL,"
            "abs_z INTEGER NOT NULL,"
            "created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP"
            ");",
            error
        ) ) {

        return false;
    }


    // ------------------------------------------------------------
    // CDDA-AI V4 item_id migration
    //
    // Las bases V3 existentes no tienen item_id.
    // Los recuerdos historicos reciben "" como valor.
    // ------------------------------------------------------------

    bool has_item_id = false;

    if(
        !world_memories_has_item_id(
            db,
            has_item_id,
            error
        )
    ) {

        return false;
    }


    if( !has_item_id ) {

        if( !execute_sql(
                db,
                "ALTER TABLE world_memories "
                "ADD COLUMN item_id TEXT NOT NULL DEFAULT '';",
                error
            ) ) {

            return false;
        }
    }

    if( !execute_sql(
            db,
            "CREATE INDEX IF NOT EXISTS "
            "idx_world_memories_npc_id_id "
            "ON world_memories(npc_id, id);",
            error
        ) ) {

        return false;
    }


    if( !execute_sql(
            db,
            "CREATE UNIQUE INDEX IF NOT EXISTS "
            "idx_world_memories_item_identity "
            "ON world_memories("
                "npc_id,"
                "memory_type,"
                "item_id,"
                "abs_x,"
                "abs_y,"
                "abs_z"
            ") "
            "WHERE item_id <> '';",
            error
        ) ) {

        return false;
    }

    if( !execute_sql(
            db,
            "UPDATE schema_version "
            "SET version = 4 "
            "WHERE singleton = 1 "
            "AND version < 4;",
            error
        ) ) {

        return false;
    }

    return true;
}

} // namespace


namespace npc_ai
{

database_status ensure_database_ready()
{
    database_status result;

    sqlite3 *db = nullptr;

    if( !open_database(
            &db,
            result.path,
            result.error
        ) ) {

        return result;
    }


    std::string schema_error;

    if( !create_schema(
            db,
            schema_error
        ) ) {

        result.error =
            "Schema SQLite: " +
            schema_error;

        sqlite3_close(
            db
        );

        return result;
    }


    sqlite3_stmt *statement = nullptr;

    const int prepare_result =
        sqlite3_prepare_v2(
            db,
            "SELECT version "
            "FROM schema_version "
            "WHERE singleton = 1;",
            -1,
            &statement,
            nullptr
        );


    if( prepare_result != SQLITE_OK ) {

        result.error =
            "SELECT schema_version: " +
            std::string(
                sqlite3_errmsg( db )
            );

        sqlite3_close(
            db
        );

        return result;
    }


    const int step_result =
        sqlite3_step(
            statement
        );


    if( step_result == SQLITE_ROW ) {

        result.schema_version =
            sqlite3_column_int(
                statement,
                0
            );

    } else {

        result.error =
            "No se pudo leer schema_version.";

        sqlite3_finalize(
            statement
        );

        sqlite3_close(
            db
        );

        return result;
    }


    sqlite3_finalize(
        statement
    );

    sqlite3_close(
        db
    );

    result.success = true;

    return result;
}


bool store_conversation(
    const npc &who,
    const std::string &player_line,
    const std::string &npc_line,
    std::string &error
)
{
    // Aseguramos primero que V2 exista.
    const database_status status =
        ensure_database_ready();

    if( !status.success ) {

        error =
            status.error;

        return false;
    }


    sqlite3 *db = nullptr;

    std::string path;

    if( !open_database(
            &db,
            path,
            error
        ) ) {

        return false;
    }


    sqlite3_stmt *statement = nullptr;

    const char *sql =
        "INSERT INTO conversations "
        "(npc_id, player_line, npc_line) "
        "VALUES (?, ?, ?);";


    if( sqlite3_prepare_v2(
            db,
            sql,
            -1,
            &statement,
            nullptr
        ) != SQLITE_OK ) {

        error =
            sqlite3_errmsg( db );

        sqlite3_close(
            db
        );

        return false;
    }


    // ID persistente real del NPC de CDDA.
    const int npc_id =
        who.getID().get_value();


    sqlite3_bind_int(
        statement,
        1,
        npc_id
    );


    sqlite3_bind_text(
        statement,
        2,
        player_line.c_str(),
        static_cast<int>(
            player_line.size()
        ),
        SQLITE_TRANSIENT
    );


    sqlite3_bind_text(
        statement,
        3,
        npc_line.c_str(),
        static_cast<int>(
            npc_line.size()
        ),
        SQLITE_TRANSIENT
    );


    const int step_result =
        sqlite3_step(
            statement
        );


    if( step_result != SQLITE_DONE ) {

        error =
            sqlite3_errmsg( db );

        sqlite3_finalize(
            statement
        );

        sqlite3_close(
            db
        );

        return false;
    }


    sqlite3_finalize(
        statement
    );

    sqlite3_close(
        db
    );

    return true;
}


std::vector<conversation_record> load_recent_conversations(
    const npc &who,
    const int max_records,
    std::string &error
)
{
    std::vector<conversation_record> result;

    error.clear();

    if( max_records <= 0 ) {
        return result;
    }


    // ------------------------------------------------------------
    // Asegurar que el esquema SQLite exista.
    // ------------------------------------------------------------

    const database_status status =
        ensure_database_ready();

    if( !status.success ) {

        error =
            status.error;

        return result;
    }


    sqlite3 *db = nullptr;

    std::string path;

    if( !open_database(
            &db,
            path,
            error
        ) ) {

        return result;
    }


    // ------------------------------------------------------------
    // Seleccionamos los N registros mas recientes,
    // pero los devolvemos de antiguo -> nuevo.
    // ------------------------------------------------------------

    const char *sql =
        "SELECT id, npc_id, player_line, npc_line, created_at "
        "FROM ("
        "    SELECT id, npc_id, player_line, npc_line, created_at "
        "    FROM conversations "
        "    WHERE npc_id = ? "
        "    ORDER BY id DESC "
        "    LIMIT ?"
        ") "
        "ORDER BY id ASC;";


    sqlite3_stmt *statement = nullptr;


    if( sqlite3_prepare_v2(
            db,
            sql,
            -1,
            &statement,
            nullptr
        ) != SQLITE_OK ) {

        error =
            sqlite3_errmsg( db );

        sqlite3_close(
            db
        );

        return result;
    }


    const int npc_id =
        who.getID().get_value();


    sqlite3_bind_int(
        statement,
        1,
        npc_id
    );


    sqlite3_bind_int(
        statement,
        2,
        max_records
    );


    int step_result =
        SQLITE_ROW;


    while(
        ( step_result =
              sqlite3_step(
                  statement
              ) ) == SQLITE_ROW
    ) {

        conversation_record record;

        record.id =
            static_cast<long long>(
                sqlite3_column_int64(
                    statement,
                    0
                )
            );


        record.npc_id =
            sqlite3_column_int(
                statement,
                1
            );


        const unsigned char *player_text =
            sqlite3_column_text(
                statement,
                2
            );

        const unsigned char *npc_text =
            sqlite3_column_text(
                statement,
                3
            );

        const unsigned char *created_text =
            sqlite3_column_text(
                statement,
                4
            );


        if( player_text != nullptr ) {

            record.player_line =
                reinterpret_cast<const char *>(
                    player_text
                );
        }


        if( npc_text != nullptr ) {

            record.npc_line =
                reinterpret_cast<const char *>(
                    npc_text
                );
        }


        if( created_text != nullptr ) {

            record.created_at =
                reinterpret_cast<const char *>(
                    created_text
                );
        }


        result.push_back(
            record
        );
    }


    if( step_result != SQLITE_DONE ) {

        error =
            sqlite3_errmsg( db );

        result.clear();
    }


    sqlite3_finalize(
        statement
    );


    sqlite3_close(
        db
    );


    return result;
}

bool store_seen_item_memory(
    const npc &who,
    const std::string &item_id,
    const std::string &item_name,
    const int abs_x,
    const int abs_y,
    const int abs_z,
    bool &inserted,
    std::string &error
)
{
    error.clear();
    inserted = false;


    const database_status status =
        ensure_database_ready();


    if( !status.success ) {

        error =
            status.error;

        return false;
    }


    sqlite3 *db = nullptr;

    std::string path;


    if( !open_database(
            &db,
            path,
            error
        ) ) {

        return false;
    }


    const char *sql =
        "INSERT OR IGNORE INTO world_memories ("
        "npc_id,"
        "memory_type,"
        "item_id,"
        "item_name,"
        "abs_x,"
        "abs_y,"
        "abs_z"
        ") "
        "VALUES (?, 'SEEN_ITEM', ?, ?, ?, ?, ?);";


    sqlite3_stmt *statement = nullptr;


    if(
        sqlite3_prepare_v2(
            db,
            sql,
            -1,
            &statement,
            nullptr
        ) != SQLITE_OK
    ) {

        error =
            sqlite3_errmsg(
                db
            );

        sqlite3_close(
            db
        );

        return false;
    }


    bool success = true;


    if(
        sqlite3_bind_int(
            statement,
            1,
            who.getID().get_value()
        ) != SQLITE_OK
    ) {

        success = false;
    }


    if(
        success &&
        sqlite3_bind_text(
            statement,
            2,
            item_id.c_str(),
            -1,
            SQLITE_TRANSIENT
        ) != SQLITE_OK
    ) {

        success = false;
    }


    if(
        success &&
        sqlite3_bind_text(
            statement,
            3,
            item_name.c_str(),
            -1,
            SQLITE_TRANSIENT
        ) != SQLITE_OK
    ) {

        success = false;
    }


    if(
        success &&
        sqlite3_bind_int(
            statement,
            4,
            abs_x
        ) != SQLITE_OK
    ) {

        success = false;
    }


    if(
        success &&
        sqlite3_bind_int(
            statement,
            5,
            abs_y
        ) != SQLITE_OK
    ) {

        success = false;
    }


    if(
        success &&
        sqlite3_bind_int(
            statement,
            6,
            abs_z
        ) != SQLITE_OK
    ) {

        success = false;
    }


    if(
        success &&
        sqlite3_step(
            statement
        ) != SQLITE_DONE
    ) {

        success = false;
    }


    if( success ) {

        inserted =
            sqlite3_changes(
                db
            ) > 0;

    } else {

        error =
            sqlite3_errmsg(
                db
            );
    }


    sqlite3_finalize(
        statement
    );

    sqlite3_close(
        db
    );


    return success;
}

std::vector<world_memory_record> load_recent_world_memories(
    const npc &who,
    const int max_records,
    std::string &error
)
{
    std::vector<world_memory_record> result;

    error.clear();

    if( max_records <= 0 ) {
        return result;
    }


    const database_status status =
        ensure_database_ready();

    if( !status.success ) {
        error = status.error;
        return result;
    }


    sqlite3 *db = nullptr;

    std::string path;

    if( !open_database(
            &db,
            path,
            error
        ) ) {

        return result;
    }


    const char *sql =
        "SELECT "
        "id, npc_id, memory_type, item_name, "
        "abs_x, abs_y, abs_z, created_at "
        "FROM ("
            "SELECT "
            "id, npc_id, memory_type, item_name, "
            "abs_x, abs_y, abs_z, created_at "
            "FROM world_memories "
            "WHERE npc_id = ? "
            "ORDER BY id DESC "
            "LIMIT ?"
        ") "
        "ORDER BY id ASC;";


    sqlite3_stmt *statement = nullptr;


    if(
        sqlite3_prepare_v2(
            db,
            sql,
            -1,
            &statement,
            nullptr
        ) != SQLITE_OK
    ) {

        error = sqlite3_errmsg( db );

        sqlite3_close( db );

        return result;
    }


    if(
        sqlite3_bind_int(
            statement,
            1,
            who.getID().get_value()
        ) != SQLITE_OK
    ) {

        error = sqlite3_errmsg( db );

        sqlite3_finalize( statement );
        sqlite3_close( db );

        return result;
    }


    if(
        sqlite3_bind_int(
            statement,
            2,
            max_records
        ) != SQLITE_OK
    ) {

        error = sqlite3_errmsg( db );

        sqlite3_finalize( statement );
        sqlite3_close( db );

        return result;
    }


    int step_result = SQLITE_ROW;


    while(
        (
            step_result =
                sqlite3_step( statement )
        ) == SQLITE_ROW
    ) {

        world_memory_record record;

        record.id =
            sqlite3_column_int64(
                statement,
                0
            );

        record.npc_id =
            sqlite3_column_int(
                statement,
                1
            );


        const unsigned char *memory_type =
            sqlite3_column_text(
                statement,
                2
            );

        const unsigned char *item_name =
            sqlite3_column_text(
                statement,
                3
            );

        const unsigned char *created_at =
            sqlite3_column_text(
                statement,
                7
            );


        if( memory_type != nullptr ) {
            record.memory_type =
                reinterpret_cast<const char *>(
                    memory_type
                );
        }

        if( item_name != nullptr ) {
            record.item_name =
                reinterpret_cast<const char *>(
                    item_name
                );
        }


        record.abs_x =
            sqlite3_column_int(
                statement,
                4
            );

        record.abs_y =
            sqlite3_column_int(
                statement,
                5
            );

        record.abs_z =
            sqlite3_column_int(
                statement,
                6
            );


        if( created_at != nullptr ) {
            record.created_at =
                reinterpret_cast<const char *>(
                    created_at
                );
        }


        result.push_back(
            record
        );
    }


    if( step_result != SQLITE_DONE ) {
        error = sqlite3_errmsg( db );
    }


    sqlite3_finalize(
        statement
    );

    sqlite3_close(
        db
    );


    if( !error.empty() ) {
        result.clear();
    }


    return result;
}
} // namespace npc_ai