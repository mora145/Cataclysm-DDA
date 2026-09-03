// Hidden live scenario runner: builds real in-engine situations, lets the real
// NPC AI pipeline generate the exact prompts, sends them to the active remote
// provider (ask_openai) and writes a Markdown report with prompts, raw model
// output, what the NPC actually said after validation, latency and tokens.
//
// Run explicitly:  Cataclysm-test-...exe "[.npc_ai_live_scenarios]" --rng-seed 1
// Requires CDDA_NPC_AI_OPENAI_API_KEY.  Report path: CDDA_NPC_AI_SCENARIO_REPORT
// (default npc_ai_scenarios_report.md in the working directory).

#include "cata_catch.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <mutex>
#include <string>
#include <vector>

#include "avatar.h"
#include "bodypart.h"
#include "calendar.h"
#include "faction.h"
#include "game.h"
#include "item.h"
#include "map.h"
#include "map_helpers.h"
#include "messages.h"
#include "monster.h"
#include "npc.h"
#include "npc_ai_async.h"
#include "npc_ai_client.h"
#include "npc_ai_combat_social.h"
#include "npc_ai_context.h"
#include "npc_ai_memory.h"
#include "npc_ai_pickup.h"
#include "npc_ai_spontaneous.h"
#include "npctalk.h"
#include "player_helpers.h"
#include "point.h"
#include "sounds.h"
#include "translation_manager.h"
#include "translations.h"
#include "type_id.h"

using namespace std::chrono_literals;

namespace
{

static const faction_id faction_your_followers( "your_followers" );
static const efftype_id effect_grabbed( "grabbed" );
static const efftype_id effect_bleed( "bleed" );
static const itype_id itype_backpack( "backpack" );
static const itype_id itype_bandages( "bandages" );
static const itype_id itype_bat( "bat" );
static const itype_id itype_knife_combat( "knife_combat" );

struct exchange_record {
    std::string prompt;
    std::string system;
    std::string response;
    std::string error;
    long long ms = 0;
    bool success = false;
    bool truncated = false;
    int prompt_tokens = 0;
    int output_tokens = 0;
};

struct scenario_record {
    std::string name;
    std::string setup;
    std::string question;
    std::string expectation;
    std::vector<exchange_record> exchanges;
    std::vector<std::string> spoken;
    std::string outcome;
};

std::vector<scenario_record> g_records;
std::mutex g_exchange_mutex;
scenario_record *g_current = nullptr;

void install_live_executor()
{
    npc_ai::set_ai_request_executor_for_test(
    []( const std::string & prompt, const std::string & system ) {
        const auto started = std::chrono::steady_clock::now();
        const npc_ai::ai_response r = npc_ai::ask_openai( prompt, system );
        const long long ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                 std::chrono::steady_clock::now() - started ).count();
        std::lock_guard<std::mutex> lock( g_exchange_mutex );
        if( g_current != nullptr ) {
            g_current->exchanges.push_back( { prompt, system, r.text, r.error, ms, r.success,
                                              r.context_truncated, r.prompt_eval_count, r.eval_count } );
        }
        return r;
    }, false );
}

void refresh_visibility( npc &observer )
{
    map &here = get_map();
    here.invalidate_map_cache( observer.posz() );
    here.build_map_cache( observer.posz(), true );
    here.invalidate_visibility_cache();
    here.update_visibility_cache( observer.posz() );
    observer.recalc_sight_limits();
}

scenario_record &begin_scenario( const std::string &name, const std::string &setup,
                                 const std::string &question, const std::string &expectation )
{
    npc_ai::reset_ai_request_system_for_test();
    npc_ai::end_ai_session();
    g->faction_manager_ptr->create_if_needed();
    clear_map();
    clear_avatar();
    clear_npcs();
    sounds::reset_sounds();
    set_time_to_day();
    g->place_player( tripoint_bub_ms{ 60, 60, 0 } );
    map &here = get_map();
    here.invalidate_map_cache( 0 );
    here.build_map_cache( 0, true );
    here.invalidate_visibility_cache();
    here.update_visibility_cache( 0 );
    npc_ai::reset_all_combat_social_states();
    npc_ai::reset_all_spontaneous_states();
    npc_ai::reset_all_recent_speech();
    npc_ai::set_combat_social_batching_for_test( false );
    Messages::clear_messages();

    g_records.push_back( scenario_record{} );
    scenario_record &rec = g_records.back();
    rec.name = name;
    rec.setup = setup;
    rec.question = question;
    rec.expectation = expectation;
    {
        std::lock_guard<std::mutex> lock( g_exchange_mutex );
        g_current = &rec;
    }
    install_live_executor();
    npc_ai::begin_ai_session();
    return rec;
}

npc &spawn_follower( const point &offset, const std::string &name )
{
    npc &follower = spawn_npc( get_avatar().pos_bub().xy() + offset, "test_talker" );
    follower.name = name;
    follower.set_fac( faction_your_followers );
    follower.set_attitude( NPCATT_FOLLOW );
    refresh_visibility( follower );
    follower.regen_ai_cache();
    return follower;
}

// Drains the queue, applies completions on the main thread (validators,
// language retry, npc::say) and returns everything that reached the log.
void settle_and_collect( scenario_record &rec )
{
    npc_ai::ai_request_queue &queue = npc_ai::get_ai_request_queue();
    queue.start();
    for( int round = 0; round < 4; ++round ) {
        REQUIRE( queue.wait_until_idle_for_test( 90s ) );
        npc_ai::process_ai_completions();
        if( queue.pending_count() == 0 && queue.ready_completion_count() == 0 ) {
            break;
        }
    }
    sounds::process_sound_markers( &get_avatar() );
    for( const auto &message : Messages::recent_messages( 0 ) ) {
        rec.spoken.push_back( message.second );
    }
    Messages::clear_messages();
}

void ask_directly( npc &who, const std::string &line, scenario_record &rec )
{
    const npc_ai::ai_enqueue_result queued =
        npc_ai::enqueue_direct_dialogue( who, line, npc_ai::build_npc_prompt( who, line ) );
    REQUIRE( queued.accepted );
    settle_and_collect( rec );
}

std::string replace_all( std::string text, const std::string &from, const std::string &to )
{
    for( std::size_t at = text.find( from ); at != std::string::npos;
         at = text.find( from, at + to.size() ) ) {
        text.replace( at, from.size(), to );
    }
    return text;
}

void write_report()
{
    const char *env_path = std::getenv( "CDDA_NPC_AI_SCENARIO_REPORT" );
    const std::string path = env_path != nullptr ? env_path : "npc_ai_scenarios_report.md";
    std::ofstream out( path, std::ios::binary | std::ios::trunc );
    REQUIRE( out.is_open() );

    long long total_ms = 0;
    int total_prompt_tokens = 0;
    int total_output_tokens = 0;
    int total_calls = 0;
    int failed_calls = 0;
    for( const scenario_record &rec : g_records ) {
        for( const exchange_record &ex : rec.exchanges ) {
            total_ms += ex.ms;
            total_prompt_tokens += ex.prompt_tokens;
            total_output_tokens += ex.output_tokens;
            ++total_calls;
            failed_calls += ex.success ? 0 : 1;
        }
    }

    out << "# NPC AI live scenario report\n\n"
        << "Provider: openai-compatible, model `" << npc_ai::openai_model_name() << "`, host `"
        << npc_ai::openai_host() << "`.  Dialogue language: `"
        << npc_ai::current_dialogue_language_code() << "`.\n\n"
        << "| scenarios | model calls | failed | avg latency ms | prompt tokens | output tokens |\n"
        << "|---|---|---|---|---|---|\n"
        << "| " << g_records.size() << " | " << total_calls << " | " << failed_calls << " | "
        << ( total_calls > 0 ? total_ms / total_calls : 0 ) << " | " << total_prompt_tokens
        << " | " << total_output_tokens << " |\n\n";

    int index = 0;
    for( const scenario_record &rec : g_records ) {
        ++index;
        out << "## " << index << ". " << rec.name << "\n\n"
            << "- **Setup:** " << rec.setup << "\n"
            << "- **Input:** " << rec.question << "\n"
            << "- **Expected:** " << rec.expectation << "\n"
            << "- **Outcome:** " << ( rec.outcome.empty() ? "(see below)" : rec.outcome ) << "\n\n";
        if( rec.exchanges.empty() ) {
            out << "_No model call was made._\n\n";
        }
        int call = 0;
        for( const exchange_record &ex : rec.exchanges ) {
            ++call;
            out << "**Call " << call << "** — " << ex.ms << " ms, prompt " << ex.prompt_tokens
                << " tok / " << ex.prompt.size() << " B, output " << ex.output_tokens << " tok"
                << ( ex.truncated ? ", TRUNCATED" : "" ) << ( ex.success ? "" : ", FAILED: " + ex.error )
                << "\n\n```text\n" << replace_all( ex.response, "```", "'''" ) << "\n```\n\n";
        }
        out << "**Spoken in game (after validation):**\n\n";
        if( rec.spoken.empty() ) {
            out << "_nothing reached the message log_\n\n";
        } else {
            for( const std::string &line : rec.spoken ) {
                out << "- " << line << "\n";
            }
            out << "\n";
        }
    }

    out << "\n# Appendix: exact prompts\n\n";
    index = 0;
    for( const scenario_record &rec : g_records ) {
        ++index;
        int call = 0;
        for( const exchange_record &ex : rec.exchanges ) {
            ++call;
            out << "## " << index << "." << call << " " << rec.name << "\n\n### system\n\n```text\n"
                << replace_all( ex.system, "```", "'''" ) << "\n```\n\n### prompt\n\n```text\n"
                << replace_all( ex.prompt, "```", "'''" ) << "\n```\n\n";
        }
    }
    WARN( "scenario report written to " << path );
}

} // namespace

TEST_CASE( "npc_ai_live_scenarios_against_remote_provider", "[.npc_ai_live_scenarios]" )
{
    if( !npc_ai::openai_api_key_available() ) {
        WARN( "CDDA_NPC_AI_OPENAI_API_KEY not set; live scenarios skipped" );
        return;
    }
    std::string previous_language;
#if defined( LOCALIZE )
    previous_language = TranslationManager::GetInstance().GetCurrentLanguage();
    set_language( "es_ES" );
#endif
    npc_ai::set_persistent_memory_writes_for_test( false );
    g_records.clear();

    // ---------------------------------------------------------------- direct
    {
        scenario_record &rec = begin_scenario( "Saludo simple",
                                               "Liam sano, sin enemigos, de dia.", "Hola Liam, ¿cómo va todo?",
                                               "Saludo breve en español, sin inventar heridas ni peligros." );
        npc &liam = spawn_follower( point::east, "Liam" );
        ask_directly( liam, rec.question, rec );
    }
    {
        scenario_record &rec = begin_scenario( "Estado propio sano",
                                               "Liam con HP completo, sin dolor.", "¿Cómo estás? ¿Te duele algo?",
                                               "Debe decir que esta bien. Cualquier herida es invencion." );
        npc &liam = spawn_follower( point::east, "Liam" );
        ask_directly( liam, rec.question, rec );
    }
    {
        scenario_record &rec = begin_scenario( "Brazo derecho grave",
                                               "Liam con brazo derecho al 10 % de HP.", "¿Estás herido?",
                                               "Debe mencionar el brazo derecho y su gravedad. No otras partes." );
        npc &liam = spawn_follower( point::east, "Liam" );
        liam.set_part_hp_cur( body_part_arm_r,
                              std::max( 1, liam.get_part_hp_max( body_part_arm_r ) / 10 ) );
        ask_directly( liam, rec.question, rec );
    }
    {
        scenario_record &rec = begin_scenario( "Sangrado y dolor",
                                               "Liam sangrando por el brazo derecho, dolor 40.", "¿Necesitas ayuda?",
                                               "Debe pedir o aceptar ayuda, mencionar sangrado/dolor; nada de otras heridas." );
        npc &liam = spawn_follower( point::east, "Liam" );
        liam.add_effect( effect_bleed, 10_minutes, body_part_arm_r );
        liam.set_pain( 40 );
        ask_directly( liam, rec.question, rec );
    }
    {
        scenario_record &rec = begin_scenario( "Pierna rota, no puede caminar",
                                               "Liam con pierna izquierda a 0 HP.", "¿Puedes caminar?",
                                               "Debe decir que la pierna izquierda esta rota o inutil. Hallazgo 1 del backlog: la movilidad puede no llegar al contexto." );
        npc &liam = spawn_follower( point::east, "Liam" );
        liam.set_part_hp_cur( body_part_leg_l, 0 );
        ask_directly( liam, rec.question, rec );
    }
    {
        scenario_record &rec = begin_scenario( "Percepcion con dos zombis",
                                               "Dos zombis visibles a 4 y 6 casillas al este.", "¿Qué ves?",
                                               "Debe mencionar dos zombis y direccion/distancia aproximada. No mas enemigos." );
        npc &liam = spawn_follower( point::east, "Liam" );
        const tripoint_bub_ms origin = liam.pos_bub( get_map() );
        spawn_test_monster( "mon_zombie", origin + tripoint_rel_ms{ 4, 0, 0 } );
        spawn_test_monster( "mon_zombie", origin + tripoint_rel_ms{ 6, 0, 0 } );
        refresh_visibility( liam );
        liam.regen_ai_cache();
        ask_directly( liam, rec.question, rec );
    }
    {
        scenario_record &rec = begin_scenario( "Percepcion sin enemigos",
                                               "Ningun monstruo en el mapa.", "¿Hay zombis cerca?",
                                               "Debe negar. Cualquier enemigo es invencion." );
        npc &liam = spawn_follower( point::east, "Liam" );
        ask_directly( liam, rec.question, rec );
    }
    {
        scenario_record &rec = begin_scenario( "Trampa de alucinacion: el oso",
                                               "Ningun animal en el mapa.", "¿Viste al oso que pasó corriendo hace un momento?",
                                               "Debe decir que no vio ningun oso. Si lo confirma, alucina." );
        npc &liam = spawn_follower( point::east, "Liam" );
        ask_directly( liam, rec.question, rec );
    }
    {
        scenario_record &rec = begin_scenario( "Inventario real",
                                               "Liam lleva mochila con 3 vendas y un cuchillo de combate, sin arma empuñada.",
                                               "¿Qué llevas encima?",
                                               "Debe citar mochila, vendas (3) y cuchillo. No otros objetos." );
        npc &liam = spawn_follower( point::east, "Liam" );
        liam.wear_item( item( itype_backpack ) );
        for( int i = 0; i < 3; ++i ) {
            liam.i_add( item( itype_bandages ) );
        }
        liam.i_add( item( itype_knife_combat ) );
        ask_directly( liam, rec.question, rec );
    }
    {
        scenario_record &rec = begin_scenario( "Arma empuñada",
                                               "Liam empuña un bate de beisbol.", "¿Con qué estás armado?",
                                               "Debe decir bate (de beisbol). No armas de fuego." );
        npc &liam = spawn_follower( point::east, "Liam" );
        item bat( itype_bat );
        REQUIRE( liam.wield( bat ) );
        ask_directly( liam, rec.question, rec );
    }
    {
        scenario_record &rec = begin_scenario( "Sed alta",
                                               "Liam con sed 600.", "¿Tienes sed?",
                                               "Debe decir que si, mucha. Verifica que la necesidad llegue al contexto." );
        npc &liam = spawn_follower( point::east, "Liam" );
        liam.set_thirst( 600 );
        ask_directly( liam, rec.question, rec );
    }
    {
        scenario_record &rec = begin_scenario( "Noche",
                                               "Tres horas despues del atardecer.", "¿Se ve algo? ¿Es de noche?",
                                               "Debe reconocer la oscuridad/noche." );
        set_time( sunset( calendar::turn ) + 3_hours );
        npc &liam = spawn_follower( point::east, "Liam" );
        ask_directly( liam, rec.question, rec );
    }
    {
        scenario_record &rec = begin_scenario( "Bloqueo de idioma",
                                               "Juego en es_ES, jugador habla en ingles.", "How are you holding up, Liam?",
                                               "La respuesta debe ser en español (LANGUAGE LOCK). Si sale en ingles, el validador la filtra/reintenta." );
        npc &liam = spawn_follower( point::east, "Liam" );
        ask_directly( liam, rec.question, rec );
    }
    {
        scenario_record &rec = begin_scenario( "Memoria de conversacion",
                                               "Primero se le dice el nombre del jugador; luego se le pregunta.",
                                               "Me llamo Yeshua, recuérdalo. | ¿Cómo me llamo?",
                                               "La segunda respuesta debe contener Yeshua." );
        npc &liam = spawn_follower( point::east, "Liam" );
        ask_directly( liam, "Me llamo Yeshua, recuérdalo.", rec );
        ask_directly( liam, "¿Cómo me llamo?", rec );
    }
    {
        scenario_record &rec = begin_scenario( "Dialogo grupal con estados distintos",
                                               "Liam sano; Kim con brazo derecho grave y sangrando.", "¿Cómo están todos?",
                                               "Cada uno habla de su propio estado: Liam bien, Kim herida. Sin frases clonadas." );
        npc &liam = spawn_follower( point::east, "Liam" );
        npc &kim = spawn_follower( point::south, "Kim" );
        kim.set_part_hp_cur( body_part_arm_r, std::max( 1, kim.get_part_hp_max( body_part_arm_r ) / 10 ) );
        kim.add_effect( effect_bleed, 10_minutes, body_part_arm_r );
        REQUIRE( npc_ai::enqueue_group_ai_dialogue( { &liam, &kim }, rec.question ) == 2 );
        settle_and_collect( rec );
    }

    // ---------------------------------------------------------- combat social
    {
        scenario_record &rec = begin_scenario( "Combat Social: primer avistamiento",
                                               "Zombi aparece a 3 casillas; process_combat_social genera el evento.",
                                               "(evento first_sight)",
                                               "Contrato DECISION=TALK/TEXT=. Aviso corto, en español, sin inventar mas enemigos." );
        npc &liam = spawn_follower( point::east, "Liam" );
        const tripoint_bub_ms origin = liam.pos_bub( get_map() );
        spawn_test_monster( "mon_zombie", origin + tripoint_rel_ms{ 3, 0, 0 } );
        refresh_visibility( liam );
        liam.regen_ai_cache();
        const npc_ai::combat_social_process_result result = npc_ai::process_combat_social( liam );
        rec.outcome = result.request_queued ? "request queued for event " +
                      npc_ai::combat_social_event_name( result.event.type ) : "no request queued";
        settle_and_collect( rec );
    }
    {
        scenario_record &rec = begin_scenario( "Combat Social: aliada agarrada",
                                               "Zombi visible; Sarah (aliada) queda agarrada 5 turnos despues.",
                                               "(evento ally_grabbed)",
                                               "Debe nombrar a Sarah y el agarre. Urgente." );
        npc &liam = spawn_follower( point::east, "Liam" );
        npc &sarah = spawn_follower( point::south_east, "Sarah" );
        const tripoint_bub_ms origin = liam.pos_bub( get_map() );
        spawn_test_monster( "mon_zombie", origin + tripoint_rel_ms{ 3, 0, 0 } );
        refresh_visibility( liam );
        liam.regen_ai_cache();
        npc_ai::process_combat_social( liam );
        calendar::turn += 5_turns;
        sarah.add_effect( effect_grabbed, 10_turns, bodypart_id( "arm_l" ), false, 1, true );
        refresh_visibility( liam );
        liam.regen_ai_cache();
        const npc_ai::combat_social_process_result urgent = npc_ai::process_combat_social( liam );
        rec.outcome = urgent.request_queued ? "urgent request queued for " +
                      npc_ai::combat_social_event_name( urgent.event.type ) : "no urgent request";
        settle_and_collect( rec );
    }
    {
        scenario_record &rec = begin_scenario( "Combat Social por lotes (JSON)",
                                               "Batching activado; Liam y Kim ven dos zombis.", "(lote de hechos)",
                                               "JSON estricto {candidates:[{slot,event_ids,claim_level,text}]}. Texto solo sobre hechos del lote." );
        npc_ai::set_combat_social_batching_for_test( true );
        npc &liam = spawn_follower( point::east, "Liam" );
        npc &kim = spawn_follower( point::south, "Kim" );
        const tripoint_bub_ms origin = liam.pos_bub( get_map() );
        spawn_test_monster( "mon_zombie", origin + tripoint_rel_ms{ 3, 0, 0 } );
        spawn_test_monster( "mon_zombie_dog", origin + tripoint_rel_ms{ 4, 1, 0 } );
        refresh_visibility( liam );
        refresh_visibility( kim );
        liam.regen_ai_cache();
        kim.regen_ai_cache();
        const npc_ai::combat_social_process_result first = npc_ai::process_combat_social( liam );
        const npc_ai::combat_social_process_result second = npc_ai::process_combat_social( kim );
        rec.outcome = std::string( "liam queued=" ) + ( first.request_queued ? "yes" : "no" ) +
                      ", kim queued=" + ( second.request_queued ? "yes" : "no" );
        settle_and_collect( rec );
        npc_ai::set_combat_social_batching_for_test( false );
    }

    // ------------------------------------------------------------ spontaneous
    {
        scenario_record &rec = begin_scenario( "Habla espontanea por dolor",
                                               "Sin enemigos; el dolor de Liam sube a 10 entre dos evaluaciones.",
                                               "(evento espontaneo)",
                                               "Comentario breve sobre el dolor, o silencio. Nada de enemigos." );
        npc &liam = spawn_follower( point::east, "Liam" );
        npc_ai::reset_spontaneous_state_for_test( liam );
        npc_ai::process_spontaneous_speech( liam );
        calendar::turn += 20_turns;
        liam.set_pain( 10 );
        npc_ai::process_spontaneous_speech( liam );
        rec.outcome = "pending after trigger = " +
                      std::to_string( npc_ai::get_ai_request_queue().pending_count() );
        settle_and_collect( rec );
    }

    // ------------------------------------------------------------------ orders
    {
        scenario_record &rec = begin_scenario( "Orden: recoge la mochila",
                                               "Una mochila en el suelo a 2 casillas de Liam; ademas un cuchillo como distractor.",
                                               "Recoge la mochila.",
                                               "El LLM elige el candidato correcto (PICKUP_INDEX de la mochila) y Liam la recoge." );
        npc &liam = spawn_follower( point::east, "Liam" );
        map &here = get_map();
        const tripoint_bub_ms spot = liam.pos_bub( here ) + tripoint_rel_ms{ 2, 0, 0 };
        here.add_item_or_charges( spot, item( itype_backpack ) );
        here.add_item_or_charges( spot, item( itype_knife_combat ) );
        refresh_visibility( liam );
        liam.regen_ai_cache();
        const npc_ai::pickup_command_result result =
            npc_ai::try_handle_pickup_command( liam, rec.question );
        rec.outcome = std::string( "handled=" ) + ( result.handled ? "yes" : "no" ) +
                      " pending=" + ( result.pending ? "yes" : "no" ) + " msg=" + result.message;
        if( result.pending ) {
            settle_and_collect( rec );
        }
        for( int step = 0; step < 20 && liam.ai_directed_pickup; ++step ) {
            liam.set_moves( 2000 );
            liam.pick_up_item();
        }
        const bool has_backpack = std::any_of( liam.all_items_loc().begin(), liam.all_items_loc().end(),
        []( const item_location & loc ) {
            return loc && loc->typeId() == itype_backpack;
        } );
        rec.outcome += has_backpack ? " | RESULT: Liam has the backpack" :
                       " | RESULT: Liam does NOT have the backpack";
    }

    {
        std::lock_guard<std::mutex> lock( g_exchange_mutex );
        g_current = nullptr;
    }
    npc_ai::set_combat_social_batching_for_test( false );
    npc_ai::reset_ai_request_system_for_test();
    write_report();
#if defined( LOCALIZE )
    set_language( previous_language );
#endif
    CHECK( !g_records.empty() );
}
