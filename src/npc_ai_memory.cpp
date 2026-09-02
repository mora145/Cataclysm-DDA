#include "npc_ai_memory.h"

#include "npc_ai_database.h"
#include <algorithm>
#include <cctype>
#include <deque>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "calendar.h"
#include "catacharset.h"
#include "cata_utility.h"
#include "cata_path.h"
#include "filesystem.h"
#include "npc.h"
#include "npc_ai_profiler.h"
#include "path_info.h"
#include "worldfactory.h"
#include "unicode.h"

namespace
{

bool persistent_memory_writes_enabled = true;

struct memory_exchange {
    std::string player;
    std::string npc_line;
};

struct recent_speech_entry {
    int turn = 0;
    std::string text;
    std::string normalized;
    std::string intent;
    int speaker_id = -1;
};

// Six lines and ten minutes are enough to steer phrasing without turning this
// short-lived anti-repetition cache into a second persistent memory system.
constexpr std::size_t recent_speech_limit = 6;
constexpr std::size_t recent_cross_speaker_limit = 24;
constexpr int recent_speech_max_age_turns = 600;
std::unordered_map<int, std::deque<recent_speech_entry>> recent_speech_by_npc;
std::deque<recent_speech_entry> recent_speech_all_npcs;

int current_turn_number()
{
    return to_turn<int>( calendar::turn );
}

int npc_key( const npc &who )
{
    return who.getID().get_value();
}

std::string normalize_speech( const std::string &text )
{
    std::string result;
    result.reserve( text.size() );
    bool previous_space = true;
    for( char32_t codepoint : utf8_to_utf32( text ) ) {
        const bool unicode_punctuation = codepoint == 0x00A1 || codepoint == 0x00BF ||
                                         ( codepoint >= 0x2000 && codepoint <= 0x206F );
        const bool word_codepoint = codepoint < 128 ?
                                    std::isalnum( static_cast<unsigned char>( codepoint ) ) != 0 :
                                    !unicode_punctuation;
        if( word_codepoint ) {
            u32_to_lowercase( codepoint );
            result += utf32_to_utf8( codepoint );
            previous_space = false;
        } else if( !previous_space ) {
            result.push_back( ' ' );
            previous_space = true;
        }
    }
    if( !result.empty() && result.back() == ' ' ) {
        result.pop_back();
    }
    return result;
}

std::unordered_set<std::string> speech_tokens( const std::string &normalized )
{
    std::unordered_set<std::string> result;
    std::istringstream input( normalized );
    std::string token;
    while( input >> token ) {
        if( token.size() > 1 ) {
            result.insert( std::move( token ) );
        }
    }
    return result;
}

bool speeches_are_similar( const std::string &lhs, const std::string &rhs )
{
    if( lhs.empty() || rhs.empty() ) {
        return false;
    }
    if( lhs == rhs ) {
        return true;
    }
    if( std::min( lhs.size(), rhs.size() ) >= 18 &&
        ( lhs.find( rhs ) != std::string::npos || rhs.find( lhs ) != std::string::npos ) ) {
        return true;
    }

    const std::unordered_set<std::string> left = speech_tokens( lhs );
    const std::unordered_set<std::string> right = speech_tokens( rhs );
    if( left.size() < 3 || right.size() < 3 ) {
        return false;
    }
    std::size_t intersection = 0;
    for( const std::string &token : left ) {
        intersection += right.count( token );
    }
    const std::size_t union_size = left.size() + right.size() - intersection;
    return union_size > 0 && static_cast<double>( intersection ) / union_size >= 0.70;
}

bool cross_speaker_speeches_are_similar( const std::string &lhs, const std::string &rhs )
{
    if( lhs.empty() || rhs.empty() ) {
        return false;
    }
    if( lhs == rhs ) {
        return true;
    }
    if( std::min( lhs.size(), rhs.size() ) >= 24 &&
        ( lhs.find( rhs ) != std::string::npos || rhs.find( lhs ) != std::string::npos ) ) {
        return true;
    }

    const std::unordered_set<std::string> left = speech_tokens( lhs );
    const std::unordered_set<std::string> right = speech_tokens( rhs );
    if( left.size() < 5 || right.size() < 5 ) {
        return false;
    }
    std::size_t intersection = 0;
    for( const std::string &token : left ) {
        intersection += right.count( token );
    }
    const std::size_t union_size = left.size() + right.size() - intersection;
    return union_size > 0 && static_cast<double>( intersection ) / union_size >= 0.90;
}

void prune_recent_speech( std::deque<recent_speech_entry> &entries, const int now,
                          const std::size_t limit = recent_speech_limit )
{
    while( !entries.empty() && now - entries.front().turn > recent_speech_max_age_turns ) {
        entries.pop_front();
    }
    while( entries.size() > limit ) {
        entries.pop_front();
    }
}

std::string escape_field( const std::string &input )
{
    std::string output;
    output.reserve( input.size() + 16 );

    for( const char c : input ) {
        switch( c ) {
            case '\\':
                output += "\\\\";
                break;

            case '\t':
                output += "\\t";
                break;

            case '\n':
                output += "\\n";
                break;

            case '\r':
                output += "\\r";
                break;

            default:
                output += c;
                break;
        }
    }

    return output;
}

std::string unescape_field( const std::string &input )
{
    std::string output;
    output.reserve( input.size() );

    for( std::size_t i = 0; i < input.size(); ++i ) {
        if( input[i] != '\\' || i + 1 >= input.size() ) {
            output += input[i];
            continue;
        }

        const char next = input[++i];

        switch( next ) {
            case '\\':
                output += '\\';
                break;

            case 't':
                output += '\t';
                break;

            case 'n':
                output += '\n';
                break;

            case 'r':
                output += '\r';
                break;

            default:
                output += '\\';
                output += next;
                break;
        }
    }

    return output;
}

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

std::filesystem::path memory_file( const npc &who )
{
    const int npc_id = who.getID().get_value();

    std::string filename;

    if( npc_id > 0 ) {
        filename =
            "npc_" +
            std::to_string( npc_id ) +
            ".memory";
    } else {
        filename =
            "npc_" +
            ensure_valid_file_name( who.get_name() ) +
            ".memory";
    }

    return memory_directory() / filename;
}

std::deque<memory_exchange> load_recent_exchanges(
    const npc &who,
    const std::size_t max_exchanges
)
{
    std::deque<memory_exchange> memories;

    const std::filesystem::path path =
        memory_file( who );

    if( !file_exist( path ) ) {
        return memories;
    }

    std::ifstream input(
        path,
        std::ios::binary
    );

    if( !input.is_open() ) {
        return memories;
    }

    std::string line;

    while( std::getline( input, line ) ) {

        const std::size_t separator =
            line.find( '\t' );

        if( separator == std::string::npos ) {
            continue;
        }

        memory_exchange exchange;

        exchange.player =
            unescape_field(
                line.substr( 0, separator )
            );

        exchange.npc_line =
            unescape_field(
                line.substr( separator + 1 )
            );

        memories.push_back( exchange );

        while( memories.size() > max_exchanges ) {
            memories.pop_front();
        }
    }

    return memories;
}

bool is_routine_watch_memory(
    std::string text
)
{
    for( char &c : text ) {
        if( c >= 'A' && c <= 'Z' ) {
            c = static_cast<char>( c - 'A' + 'a' );
        }
    }

    // Ordenes de vigilancia del jugador.
    // La watchlist estructurada ya conserva esta informacion.
    if( text.find( "avisame" ) != std::string::npos ||
        text.find( "avísame" ) != std::string::npos ||
        text.find( "avisa si" ) != std::string::npos ||
        text.find( "avisa cuando" ) != std::string::npos ) {

        return true;
    }

    // Respuestas rutinarias antiguas del NPC.
    if( text.find( "te aviso si" ) != std::string::npos ||
        text.find( "te aviso cuando" ) != std::string::npos ||
        text.find( "me pediste que te avisara" ) != std::string::npos ) {

        return true;
    }

    return false;
}

std::deque<memory_exchange> load_recent_exchanges_sqlite(
    const npc &who,
    const std::size_t max_exchanges,
    bool &sqlite_ok
)
{
    std::deque<memory_exchange> memories;

    sqlite_ok = false;

    std::string sqlite_error;

    const std::vector<npc_ai::conversation_record> records =
        npc_ai::load_recent_conversations(
            who,
            static_cast<int>(
                max_exchanges
            ),
            sqlite_error
        );


    if( !sqlite_error.empty() ) {
        return memories;
    }


    sqlite_ok = true;


    for( const npc_ai::conversation_record &record : records ) {

        memory_exchange exchange;

        exchange.player =
            record.player_line;

        exchange.npc_line =
            record.npc_line;

        memories.push_back(
            exchange
        );
    }


    return memories;
}

} // namespace

namespace npc_ai
{

std::string build_memory_context(
    const npc &who,
    const std::size_t max_exchanges
)
{
    scoped_profile profile( profile_subsystem::memory );
    bool sqlite_ok = false;

    std::deque<memory_exchange> memories =
        load_recent_exchanges_sqlite(
            who,
            max_exchanges,
            sqlite_ok
        );


    // SQLite es la fuente principal de memoria conversacional.
    //
    // El archivo .memory queda como fallback de seguridad.
    if( !sqlite_ok || memories.empty() ) {

        const std::deque<memory_exchange> legacy_memories =
            load_recent_exchanges(
                who,
                max_exchanges
            );


        if( !legacy_memories.empty() ) {

            memories =
                legacy_memories;
        }
    }

    std::ostringstream output;

    output
            << "=== MEMORIA PERSONAL DEL NPC ===\n";

    if( memories.empty() ) {
        output
                << "No recuerdas conversaciones anteriores "
                << "con el jugador.\n";

        return output.str();
    }

    output
            << "Estas son conversaciones que realmente "
            << "tuviste anteriormente con el jugador.\n\n"

            << "REGLAS IMPORTANTES DE MEMORIA:\n"
            << "- Los mensajes anteriores del jugador son la fuente "
            << "principal para recordar datos que el jugador te dijo.\n"
            << "- Si el jugador afirmo explicitamente un dato anteriormente, "
            << "debes considerarlo un recuerdo valido.\n"
            << "- Una respuesta previa tuya diciendo que no sabias algo "
            << "NO invalida un dato que el jugador ya te habia dicho.\n"
            << "- Tus respuestas previas sirven para mantener continuidad, "
            << "pero tienen menor prioridad que los hechos dichos por el jugador.\n"
            << "- Antes de responder una pregunta sobre el pasado, revisa "
            << "primero los mensajes anteriores del jugador.\n"
            << "- No inventes recuerdos que no aparezcan aqui.\n\n"

            << "=== MENSAJES ANTERIORES DEL JUGADOR ===\n";

    for( const memory_exchange &memory : memories ) {

        if( is_routine_watch_memory(
                memory.player
            ) ) {

            continue;
        }

        output
                << "- "
                << memory.player
                << "\n";
    }

    output
            << "\n=== TUS RESPUESTAS ANTERIORES "
            << "(PRIORIDAD MENOR) ===\n";

    for( const memory_exchange &memory : memories ) {

        if( is_routine_watch_memory(
                memory.npc_line
            ) ) {

            continue;
        }

        output
                << "- "
                << memory.npc_line
                << "\n";
    }

    output << "\n";

    return output.str();
}

void remember_recent_speech( const npc &who, const std::string &npc_line,
                             const std::string &intent )
{
    const std::string normalized = normalize_speech( npc_line );
    if( normalized.empty() ) {
        return;
    }
    const int now = current_turn_number();
    const int speaker_id = npc_key( who );
    std::deque<recent_speech_entry> &entries = recent_speech_by_npc[speaker_id];
    prune_recent_speech( entries, now );
    if( !entries.empty() && entries.back().normalized == normalized ) {
        entries.back().turn = now;
        if( !intent.empty() ) {
            entries.back().intent = intent;
        }
    } else {
        entries.push_back( { now, npc_line, normalized, intent, speaker_id } );
        prune_recent_speech( entries, now );
    }

    prune_recent_speech( recent_speech_all_npcs, now, recent_cross_speaker_limit );
    const auto existing = std::find_if( recent_speech_all_npcs.rbegin(),
                                        recent_speech_all_npcs.rend(),
    [&]( const recent_speech_entry & entry ) {
        return entry.speaker_id == speaker_id && entry.normalized == normalized;
    } );
    if( existing != recent_speech_all_npcs.rend() ) {
        existing->turn = now;
        existing->text = npc_line;
        if( !intent.empty() ) {
            existing->intent = intent;
        }
    } else {
        recent_speech_all_npcs.push_back( { now, npc_line, normalized, intent, speaker_id } );
        prune_recent_speech( recent_speech_all_npcs, now, recent_cross_speaker_limit );
    }
}

bool recent_speech_is_duplicate( const npc &who, const std::string &candidate )
{
    const std::string normalized = normalize_speech( candidate );
    if( normalized.empty() ) {
        return false;
    }
    const int now = current_turn_number();
    const int speaker_id = npc_key( who );
    const auto found = recent_speech_by_npc.find( speaker_id );
    if( found != recent_speech_by_npc.end() ) {
        std::deque<recent_speech_entry> &entries = found->second;
        prune_recent_speech( entries, now );
        if( std::any_of( entries.begin(), entries.end(),
        [&]( const recent_speech_entry & entry ) {
            return speeches_are_similar( normalized, entry.normalized );
        } ) ) {
            return true;
        }
    }

    prune_recent_speech( recent_speech_all_npcs, now, recent_cross_speaker_limit );
    return std::any_of( recent_speech_all_npcs.begin(), recent_speech_all_npcs.end(),
    [&]( const recent_speech_entry & entry ) {
        return entry.speaker_id != speaker_id &&
               cross_speaker_speeches_are_similar( normalized, entry.normalized );
    } );
}

bool recent_speech_mentions( const npc &who, const std::string &name )
{
    if( name.empty() ) {
        return false;
    }
    const auto found = recent_speech_by_npc.find( npc_key( who ) );
    if( found == recent_speech_by_npc.end() ) {
        return false;
    }
    std::deque<recent_speech_entry> &entries = found->second;
    prune_recent_speech( entries, current_turn_number() );
    return std::any_of( entries.rbegin(), entries.rend(), [&]( const recent_speech_entry &entry ) {
        return lcmatch( entry.text, name );
    } );
}

std::string build_recent_speech_context( const npc &who, const std::size_t max_entries )
{
    const auto found = recent_speech_by_npc.find( npc_key( who ) );
    if( found == recent_speech_by_npc.end() || max_entries == 0 ) {
        return "";
    }
    std::deque<recent_speech_entry> &entries = found->second;
    prune_recent_speech( entries, current_turn_number() );
    if( entries.empty() ) {
        return "";
    }

    std::ostringstream output;
    output << "=== HABLA RECIENTE: IDEAS/FRASES QUE NO DEBES REPETIR ===\n";
    const std::size_t first = entries.size() > max_entries ? entries.size() - max_entries : 0;
    for( std::size_t index = first; index < entries.size(); ++index ) {
        output << "- ";
        if( !entries[index].intent.empty() ) {
            output << "idea=" << entries[index].intent << "; ";
        }
        output << "frase=\"" << entries[index].text << "\"\n";
    }
    output << "Estas frases sirven solo para evitar repeticion; no son evidencia de que su "
              "contenido siga siendo cierto ahora.\n"
              "No reformules esas mismas ideas. No vuelvas a usar inmediatamente un nombre "
              "que aparezca en esas frases salvo que una urgencia real lo exija.\n\n";
    return output.str();
}

void reset_recent_speech_for_test( const npc &who )
{
    const int speaker_id = npc_key( who );
    recent_speech_by_npc.erase( speaker_id );
    recent_speech_all_npcs.erase( std::remove_if( recent_speech_all_npcs.begin(),
    recent_speech_all_npcs.end(), [&]( const recent_speech_entry & entry ) {
        return entry.speaker_id == speaker_id;
    } ), recent_speech_all_npcs.end() );
}

void reset_all_recent_speech()
{
    recent_speech_by_npc.clear();
    recent_speech_all_npcs.clear();
}

void set_persistent_memory_writes_for_test( const bool enabled )
{
    persistent_memory_writes_enabled = enabled;
}

void remember_exchange(
    const npc &who,
    const std::string &player_line,
    const std::string &npc_line
)
{
    remember_recent_speech( who, npc_line );

    if( !persistent_memory_writes_enabled ) {
        return;
    }

    const std::filesystem::path directory =
        memory_directory();

    if( !assure_dir_exist( directory ) ) {
        return;
    }

    const std::filesystem::path path =
        memory_file( who );

    std::ofstream output(
        path,
        std::ios::binary |
        std::ios::app
    );

    if( !output.is_open() ) {
        return;
    }

    output
            << escape_field( player_line )
            << '\t'
            << escape_field( npc_line )
            << '\n';
    // CDDA-AI SQLite migration:
    // durante esta etapa seguimos conservando el archivo .memory.
    std::string sqlite_error;

    npc_ai::store_conversation(
        who,
        player_line,
        npc_line,
        sqlite_error
    );
}

void remember_combat_event( const npc &who, const std::string &event_kind,
                            const std::string &detail, const int importance )
{
    if( !persistent_memory_writes_enabled ) {
        return;
    }
    std::string sqlite_error;
    npc_ai::store_conversation(
        who,
        "[COMBAT_EVENT " + event_kind + " importance=" + std::to_string( importance ) + "]",
        detail,
        sqlite_error
    );
}

} // namespace npc_ai
