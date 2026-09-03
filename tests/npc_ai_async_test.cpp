#include "cata_catch.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "avatar.h"
#include "calendar.h"
#include "faction.h"
#include "game.h"
#include "map_helpers.h"
#include "messages.h"
#include "npc.h"
#include "npc_ai_async.h"
#include "npc_ai_client.h"
#include "npc_ai_context.h"
#include "npc_ai_debug.h"
#include "npc_ai_memory.h"
#include "npc_ai_spontaneous.h"
#include "overmapbuffer.h"
#include "player_helpers.h"
#include "point.h"
#include "rng.h"

using namespace std::chrono_literals;

namespace
{

static const faction_id faction_your_followers( "your_followers" );

npc &prepare_async_follower()
{
    npc_ai::end_ai_session();
    clear_map();
    clear_avatar();
    clear_npcs();
    set_time_to_day();
    g->faction_manager_ptr->create_if_needed();
    g->place_player( tripoint_bub_ms{ 60, 60, 0 } );
    npc &who = spawn_npc( point_bub_ms{ 61, 60 }, "test_talker" );
    who.name = "Liam";
    who.set_fac( faction_your_followers );
    who.set_attitude( NPCATT_FOLLOW );
    Messages::clear_messages();
    npc_ai::begin_ai_session();
    return who;
}

struct reset_async_executor {
    ~reset_async_executor() {
        npc_ai::reset_ai_request_system_for_test();
    }
};

void prepare_ready_legacy_completions(
    const std::vector<std::pair<std::string, npc_ai::ai_request_priority>> &requests,
    int *executor_calls = nullptr )
{
    npc_ai::set_ai_request_executor_for_test( [executor_calls]( const std::string &prompt ) {
        if( executor_calls != nullptr ) {
            ++*executor_calls;
        }
        return npc_ai::ai_response{ true, prompt, "" };
    }, false );
    npc_ai::begin_ai_session();
    npc_ai::ai_request_queue &queue = npc_ai::get_ai_request_queue();
    for( const auto &entry : requests ) {
        npc_ai::ai_request_snapshot request;
        request.type = npc_ai::ai_request_type::legacy_prompt;
        request.priority = entry.second;
        request.player_id = get_player_character().getID().get_value();
        request.prompt = entry.first;
        REQUIRE( queue.enqueue( std::move( request ) ).accepted );
    }
    queue.start();
    REQUIRE( queue.wait_until_idle_for_test( 1s ) );
    REQUIRE( queue.ready_completion_count() == requests.size() );
}

struct remove_ollama_diagnostics {
    remove_ollama_diagnostics() :
        path( npc_ai::debug_file_path( "npc_ai_ollama_diagnostics.txt" ) ) {
        std::error_code error;
        std::filesystem::remove( std::filesystem::u8path( path ), error );
    }

    ~remove_ollama_diagnostics() {
        npc_ai::set_runtime_debug_enabled_for_test( false );
        std::error_code error;
        std::filesystem::remove( std::filesystem::u8path( path ), error );
    }

    std::string contents() const {
        std::ifstream input( std::filesystem::u8path( path ), std::ios::binary );
        return std::string( std::istreambuf_iterator<char>( input ),
                            std::istreambuf_iterator<char>() );
    }

    std::string path;
};

} // namespace

TEST_CASE( "ollama_request_contract_and_response_parser_are_explicit",
           "[npc_ai][npc_ai_ollama]" )
{
    remove_ollama_diagnostics cleanup;
    npc_ai::set_runtime_debug_enabled_for_test( false );

    CHECK( std::string( npc_ai::ollama_model_name() ) == "qwen3:14b" );
    const std::string parameters = npc_ai::ollama_request_parameters_summary();
    CHECK( parameters.find( "temperature=0.4" ) != std::string::npos );
    CHECK( parameters.find( "top_p=0.85" ) != std::string::npos );
    CHECK( parameters.find( "top_k=20" ) != std::string::npos );
    CHECK( parameters.find( "repeat_penalty=1.1" ) != std::string::npos );
    CHECK( parameters.find( "num_predict=192" ) != std::string::npos );
    CHECK( parameters.find( "num_ctx=16384" ) != std::string::npos );
    CHECK( parameters.find( "seed=1" ) != std::string::npos );
    CHECK( parameters.find( "stop_tokens=<|im_start|>,<|im_end|>" ) !=
           std::string::npos );
    CHECK( parameters.find( "system_field=SENT" ) != std::string::npos );

    CHECK(
        npc_ai::build_ollama_request_json( "linea \"uno\"\\dos\nfin",
                                           "sistema\nfijo" ) ==
        "{\"model\":\"qwen3:14b\",\"system\":\"sistema\\nfijo\","
        "\"prompt\":\"linea \\\"uno\\\"\\\\dos\\nfin\",\"stream\":false,"
        "\"think\":false,\"keep_alive\":\"30m\",\"options\":{\"temperature\":0.4,"
        "\"top_p\":0.85,\"top_k\":20,\"repeat_penalty\":1.1,\"num_predict\":192,"
        "\"num_ctx\":16384,\"seed\":1,\"stop\":[\"<|im_start|>\",\"<|im_end|>\"]}}" );
    CHECK_FALSE( std::filesystem::exists( std::filesystem::u8path( cleanup.path ) ) );

    const npc_ai::ai_response parsed = npc_ai::parse_ollama_response_json(
                                           "{\"response\":\"respuesta limpia\",\"done\":true,"
                                           "\"prompt_eval_count\":321,\"eval_count\":17}", 1 );
    REQUIRE( parsed.success );
    CHECK( parsed.text == "respuesta limpia" );
    CHECK( parsed.http_completed_ms == 1 );
    CHECK( parsed.parse_completed_ms >= parsed.http_completed_ms );
    CHECK( parsed.prompt_eval_count == 321 );
    CHECK( parsed.eval_count == 17 );
    CHECK_FALSE( parsed.context_truncated );
    CHECK( npc_ai::ollama_hard_input_budget_bytes() == 15680 );
    CHECK( npc_ai::ollama_prompt_fits_context( "small", "system" ) );
    CHECK_FALSE( npc_ai::ollama_prompt_fits_context(
                     std::string( npc_ai::ollama_hard_input_budget_bytes(), 'x' ), "system" ) );

    const npc_ai::ai_response invalid =
        npc_ai::parse_ollama_response_json( "not-json" );
    CHECK_FALSE( invalid.success );
    CHECK( invalid.error.find( "Invalid JSON from Ollama" ) != std::string::npos );
    CHECK_FALSE( std::filesystem::exists( std::filesystem::u8path( cleanup.path ) ) );
}

TEST_CASE( "gemini_request_contract_and_response_parser_are_explicit",
           "[npc_ai][npc_ai_gemini][npc_ai_async]" )
{
    // Wire format: system instruction, single user turn, sampling config and
    // reasoning disabled.  The API key is never part of the body.
    const std::string body = npc_ai::build_gemini_request_json( "hola \"NPC\"", "sistema" );
    CHECK( body.find( "\"system_instruction\":{\"parts\":[{\"text\":\"sistema\"}]}" ) !=
           std::string::npos );
    CHECK( body.find( "\"contents\":[{\"role\":\"user\",\"parts\":[{\"text\":\"hola \\\"NPC\\\"\"}]}]" )
           != std::string::npos );
    CHECK( body.find( "\"temperature\":0.4" ) != std::string::npos );
    CHECK( body.find( "\"topP\":0.85" ) != std::string::npos );
    CHECK( body.find( "\"topK\":20" ) != std::string::npos );
    CHECK( body.find( "\"thinkingConfig\":{\"thinkingBudget\":0}" ) != std::string::npos );
    CHECK( body.find( "key" ) == std::string::npos );

    const npc_ai::ai_response parsed = npc_ai::parse_gemini_response_json(
                                           "{\"candidates\":[{\"content\":{\"parts\":[{\"text\":\"respuesta \"},"
                                           "{\"text\":\"limpia\"}],\"role\":\"model\"},\"finishReason\":\"STOP\"}],"
                                           "\"usageMetadata\":{\"promptTokenCount\":321,\"candidatesTokenCount\":17,"
                                           "\"totalTokenCount\":338},\"modelVersion\":\"gemini-2.5-flash\"}", 1 );
    REQUIRE( parsed.success );
    CHECK( parsed.text == "respuesta limpia" );
    CHECK( parsed.http_completed_ms == 1 );
    CHECK( parsed.parse_completed_ms >= parsed.http_completed_ms );
    CHECK( parsed.prompt_eval_count == 321 );
    CHECK( parsed.eval_count == 17 );
    CHECK_FALSE( parsed.context_truncated );

    // A cut-off answer is flagged exactly like an Ollama context overflow so
    // Combat Social discards it instead of validating half a JSON batch.
    const npc_ai::ai_response truncated = npc_ai::parse_gemini_response_json(
                                              "{\"candidates\":[{\"content\":{\"parts\":[{\"text\":\"[{\\\"slot\\\":1\"}]},"
                                              "\"finishReason\":\"MAX_TOKENS\"}]}" );
    REQUIRE( truncated.success );
    CHECK( truncated.context_truncated );

    const npc_ai::ai_response quota = npc_ai::parse_gemini_response_json(
                                          "{\"error\":{\"code\":429,\"message\":\"Quota exceeded\","
                                          "\"status\":\"RESOURCE_EXHAUSTED\"}}" );
    CHECK_FALSE( quota.success );
    CHECK( quota.error.find( "429" ) != std::string::npos );
    CHECK( quota.error.find( "RESOURCE_EXHAUSTED" ) != std::string::npos );

    const npc_ai::ai_response blocked = npc_ai::parse_gemini_response_json(
                                            "{\"promptFeedback\":{\"blockReason\":\"SAFETY\"}}" );
    CHECK_FALSE( blocked.success );
    CHECK( blocked.error.find( "SAFETY" ) != std::string::npos );

    const npc_ai::ai_response empty = npc_ai::parse_gemini_response_json(
                                          "{\"candidates\":[{\"finishReason\":\"SAFETY\"}]}" );
    CHECK_FALSE( empty.success );

    const npc_ai::ai_response invalid = npc_ai::parse_gemini_response_json( "not-json" );
    CHECK_FALSE( invalid.success );
    CHECK( invalid.error.find( "Invalid JSON from Gemini" ) != std::string::npos );

    // Without a key the remote path fails closed before touching the network.
    if( !npc_ai::gemini_api_key_available() ) {
        const npc_ai::ai_response no_key = npc_ai::ask_gemini( "hola", "sistema" );
        CHECK_FALSE( no_key.success );
        CHECK( no_key.error.find( "CDDA_NPC_AI_GEMINI_API_KEY" ) != std::string::npos );
    }
}

TEST_CASE( "openai_compatible_request_contract_and_response_parser_are_explicit",
           "[npc_ai][npc_ai_openai][npc_ai_async]" )
{
    const std::string body = npc_ai::build_openai_request_json( "hola", "sistema" );
    CHECK( body.find( "\"messages\":[{\"role\":\"system\",\"content\":\"sistema\"},"
                      "{\"role\":\"user\",\"content\":\"hola\\n/no_think\"}]" ) != std::string::npos );
    CHECK( body.find( "\"temperature\":0.4" ) != std::string::npos );
    CHECK( body.find( "\"top_p\":0.85" ) != std::string::npos );
    CHECK( body.find( "\"stream\":false" ) != std::string::npos );
    CHECK( body.find( "Bearer" ) == std::string::npos );

    // Qwen3 with /no_think still emits an empty think block; it must never
    // reach the validators.
    const npc_ai::ai_response parsed = npc_ai::parse_openai_response_json(
                                           "{\"id\":\"x\",\"choices\":[{\"index\":0,\"message\":{\"role\":\"assistant\","
                                           "\"content\":\"<think>\\n\\n</think>\\n\\nBrazo duele. Sangrando.\"},"
                                           "\"finish_reason\":\"stop\"}],\"usage\":{\"prompt_tokens\":181,"
                                           "\"completion_tokens\":12,\"total_tokens\":193,\"estimated_cost\":0.00001}}", 1 );
    REQUIRE( parsed.success );
    CHECK( parsed.text == "Brazo duele. Sangrando." );
    CHECK( parsed.prompt_eval_count == 181 );
    CHECK( parsed.eval_count == 12 );
    CHECK_FALSE( parsed.context_truncated );

    const npc_ai::ai_response truncated = npc_ai::parse_openai_response_json(
                                              "{\"choices\":[{\"message\":{\"content\":\"[{\\\"slot\\\":1\"},"
                                              "\"finish_reason\":\"length\"}]}" );
    REQUIRE( truncated.success );
    CHECK( truncated.context_truncated );

    const npc_ai::ai_response openai_error = npc_ai::parse_openai_response_json(
                "{\"error\":{\"message\":\"Rate limit exceeded\",\"type\":\"rate_limit_error\"}}" );
    CHECK_FALSE( openai_error.success );
    CHECK( openai_error.error.find( "rate_limit_error" ) != std::string::npos );

    const npc_ai::ai_response deepinfra_error = npc_ai::parse_openai_response_json(
                "{\"detail\":{\"error\":\"Invalid token\"}}" );
    CHECK_FALSE( deepinfra_error.success );
    CHECK( deepinfra_error.error.find( "Invalid token" ) != std::string::npos );

    const npc_ai::ai_response only_think = npc_ai::parse_openai_response_json(
                "{\"choices\":[{\"message\":{\"content\":\"<think>razonando</think>\"},\"finish_reason\":\"stop\"}]}" );
    CHECK_FALSE( only_think.success );

    const npc_ai::ai_response invalid = npc_ai::parse_openai_response_json( "not-json" );
    CHECK_FALSE( invalid.success );

    if( !npc_ai::openai_api_key_available() ) {
        const npc_ai::ai_response no_key = npc_ai::ask_openai( "hola", "sistema" );
        CHECK_FALSE( no_key.success );
        CHECK( no_key.error.find( "CDDA_NPC_AI_OPENAI_API_KEY" ) != std::string::npos );
    }
}

// Hidden: needs network and CDDA_NPC_AI_OPENAI_API_KEY / CDDA_NPC_AI_GEMINI_API_KEY.
// Run explicitly with "[.npc_ai_live]" to prove the WinHTTP + parser path
// end to end against the real providers.  Skips silently without a key.
TEST_CASE( "remote_providers_answer_a_grounded_spanish_prompt_live",
           "[.npc_ai_live]" )
{
    const std::string system =
        "Eres Kim, superviviente en el apocalipsis zombi. Responde en español, una frase corta, "
        "solo con hechos del contexto. Estado: brazo derecho herido, 3 vendas en la mochila.";
    const std::string prompt = "Kim, ¿tienes vendas?";

    if( npc_ai::openai_api_key_available() ) {
        const npc_ai::ai_response r = npc_ai::ask_openai( prompt, system );
        INFO( "openai error: " << r.error );
        REQUIRE( r.success );
        CHECK_FALSE( r.text.empty() );
        CHECK( r.text.find( "<think>" ) == std::string::npos );
        CHECK( r.prompt_eval_count > 0 );
        CHECK_FALSE( r.context_truncated );
        WARN( "openai/" << npc_ai::openai_model_name() << " -> " << r.text
              << " (prompt_tokens=" << r.prompt_eval_count << ", eval=" << r.eval_count << ")" );
    } else {
        WARN( "CDDA_NPC_AI_OPENAI_API_KEY not set; OpenAI-compatible live check skipped" );
    }

    if( npc_ai::gemini_api_key_available() ) {
        const npc_ai::ai_response r = npc_ai::ask_gemini( prompt, system );
        INFO( "gemini error: " << r.error );
        // The free tier may be in quota backoff; a 429 is reported, not a failure.
        if( r.success ) {
            CHECK_FALSE( r.text.empty() );
            CHECK( r.prompt_eval_count > 0 );
            WARN( "gemini/" << npc_ai::gemini_model_name() << " -> " << r.text );
        } else {
            WARN( "gemini live call failed (acceptable on free tier): " << r.error );
        }
    } else {
        WARN( "CDDA_NPC_AI_GEMINI_API_KEY not set; Gemini live check skipped" );
    }
}

TEST_CASE( "ollama_debug_gate_records_prompt_response_and_aggregate_latency",
           "[npc_ai][npc_ai_ollama][npc_ai_async]" )
{
    reset_async_executor reset;
    remove_ollama_diagnostics cleanup;
    npc &who = prepare_async_follower();
    npc_ai::set_runtime_debug_enabled_for_test( true );

    const std::string prompt_marker = "PROMPT_DIAGNOSTIC_MARKER";
    const std::string system_marker = "SYSTEM_DIAGNOSTIC_MARKER";
    const std::string raw_marker = "RAW_DIAGNOSTIC_MARKER";
    const std::string clean_marker = "CLEAN_DIAGNOSTIC_MARKER";
    npc_ai::build_ollama_request_json( prompt_marker, system_marker );
    const npc_ai::ai_response parsed = npc_ai::parse_ollama_response_json(
                                           "{\"response\":\"" + clean_marker + "\",\"raw_marker\":\"" + raw_marker +
                                           "\"}" );
    REQUIRE( parsed.success );

    npc_ai::set_ai_request_executor_for_test(
    []( const std::string & ) {
        return npc_ai::ai_response{true, "Estoy listo.", ""};
    },
    false );
    npc_ai::begin_ai_session();
    REQUIRE( npc_ai::enqueue_direct_dialogue( who, "Prueba",
             "diagnostic async prompt", 9001 )
             .accepted );
    npc_ai::get_ai_request_queue().start();
    REQUIRE( npc_ai::get_ai_request_queue().wait_until_idle_for_test( 1s ) );
    npc_ai::process_ai_completions();

    const std::string output = cleanup.contents();
    CHECK( output.find( "PARAMETERS model=qwen3:14b" ) != std::string::npos );
    CHECK( output.find( "SYSTEM_FINAL_BEGIN\n" + system_marker ) !=
           std::string::npos );
    CHECK( output.find( "PROMPT_FINAL_BEGIN\n" + prompt_marker ) !=
           std::string::npos );
    CHECK( output.find( "RESPONSE_RAW_BEGIN\n" ) != std::string::npos );
    CHECK( output.find( raw_marker ) != std::string::npos );
    CHECK( output.find( "RESPONSE_CLEAN_BEGIN\n" + clean_marker ) !=
           std::string::npos );
    CHECK( output.find( "NPC_AI_LATENCY_REQUEST" ) != std::string::npos );
    CHECK( output.find( "NPC_AI_ROUTING" ) != std::string::npos );
    CHECK( output.find( "request_type=DIRECT_PLAYER_DIALOGUE" ) != std::string::npos );
    CHECK( output.find( "origin=DIRECT_PLAYER_DIALOGUE" ) != std::string::npos );
    CHECK( output.find( "context_categories=GENERAL" ) != std::string::npos );
    CHECK( output.find( "npc_to_npc_reply_allowed=no" ) != std::string::npos );
    CHECK( output.find( "suppression_reason=none" ) != std::string::npos );
    CHECK( output.find( "prompt_bytes=23" ) != std::string::npos );
    CHECK( output.find( "system_bytes=" ) != std::string::npos );
    CHECK( output.find( "NPC_AI_LATENCY_SUMMARY samples=1 successes=1 errors=0" ) !=
           std::string::npos );
    CHECK( output.find( "http_ms_avg=" ) != std::string::npos );
    CHECK( output.find( "parse_ms_avg=" ) != std::string::npos );
    CHECK( output.find( "ASYNC_DRAIN" ) != std::string::npos );
    CHECK( output.find( "queue_depth_before=1 taken=1 applied=1 deferred=0" ) !=
           std::string::npos );
    CHECK( output.find( "dropped_stale=0 queue_depth_after=0" ) !=
           std::string::npos );
    CHECK( output.find( "budget_stop=no count_stop=no" ) != std::string::npos );
}

TEST_CASE( "npc_ai_async_enqueue_never_waits_for_executor",
           "[npc_ai][npc_ai_async]" )
{
    std::mutex mutex;
    std::condition_variable cv;
    bool entered = false;
    bool release = false;
    npc_ai::ai_request_queue queue( [&]( const std::string & ) {
        std::unique_lock<std::mutex> lock( mutex );
        entered = true;
        cv.notify_all();
        cv.wait( lock, [&]() {
            return release;
        } );
        return npc_ai::ai_response{true, "done", ""};
    } );

    npc_ai::ai_request_snapshot request;
    request.prompt = "slow";
    const npc_ai::ai_enqueue_result queued = queue.enqueue( request );
    CHECK( queued.accepted );
    {
        std::unique_lock<std::mutex> lock( mutex );
        REQUIRE( cv.wait_for( lock, 1s, [&]() {
            return entered;
        } ) );
    }
    CHECK( queue.take_completions( 1 ).empty() );
    {
        std::lock_guard<std::mutex> lock( mutex );
        release = true;
    }
    cv.notify_all();
    REQUIRE( queue.wait_until_idle_for_test( 1s ) );
    CHECK( queue.take_completions( 1 ).size() == 1 );
}

TEST_CASE( "npc_ai_async_prioritizes_direct_work", "[npc_ai][npc_ai_async]" )
{
    std::vector<std::string> execution_order;
    npc_ai::ai_request_queue queue( [&]( const std::string & prompt ) {
        execution_order.push_back( prompt );
        return npc_ai::ai_response{ true, prompt, "" };
    }, false );

    npc_ai::ai_request_snapshot low;
    low.prompt = "low";
    low.priority = npc_ai::ai_request_priority::low;
    npc_ai::ai_request_snapshot high;
    high.prompt = "high";
    high.priority = npc_ai::ai_request_priority::high;
    REQUIRE( queue.enqueue( low ).accepted );
    REQUIRE( queue.enqueue( high ).accepted );
    queue.start();
    REQUIRE( queue.wait_until_idle_for_test( 1s ) );
    REQUIRE( execution_order.size() == 2 );
    CHECK( execution_order[0] == "high" );
    CHECK( execution_order[1] == "low" );
}

TEST_CASE( "npc_ai_async_deduplicates_and_supersedes_requests", "[npc_ai][npc_ai_async]" )
{
    npc_ai::ai_request_queue queue( []( const std::string & prompt ) {
        return npc_ai::ai_response{ true, prompt, "" };
    }, false );
    npc_ai::ai_request_snapshot first;
    first.prompt = "old";
    first.deduplication_key = "npc:event";
    npc_ai::ai_request_snapshot newer = first;
    newer.prompt = "new";

    REQUIRE( queue.enqueue( first ).accepted );
    CHECK_FALSE( queue.enqueue( first ).accepted );
    REQUIRE( queue.enqueue( newer, true ).accepted );
    queue.start();
    REQUIRE( queue.wait_until_idle_for_test( 1s ) );
    const std::vector<npc_ai::ai_request_completion> completed = queue.take_completions( 1 );
    REQUIRE( completed.size() == 1 );
    CHECK( completed.front().response.text == "new" );
}

TEST_CASE( "npc_ai_async_failure_and_session_invalidation_are_nonfatal",
           "[npc_ai][npc_ai_async]" )
{
    SECTION( "executor failure becomes a completion" ) {
        npc_ai::ai_request_queue queue( []( const std::string & ) {
            return npc_ai::ai_response{ false, "", "Ollama unavailable" };
        } );
        npc_ai::ai_request_snapshot request;
        request.prompt = "failure";
        REQUIRE( queue.enqueue( request ).accepted );
        REQUIRE( queue.wait_until_idle_for_test( 1s ) );
        const auto completed = queue.take_completions( 1 );
        REQUIRE( completed.size() == 1 );
        CHECK_FALSE( completed.front().response.success );
    }

    SECTION( "world teardown discards an in-flight result" ) {
        std::mutex mutex;
        std::condition_variable cv;
        bool entered = false;
        bool release = false;
        npc_ai::ai_request_queue queue( [&]( const std::string & ) {
            std::unique_lock<std::mutex> lock( mutex );
            entered = true;
            cv.notify_all();
            cv.wait( lock, [&]() {
                return release;
            } );
            return npc_ai::ai_response{ true, "stale", "" };
        } );
        npc_ai::ai_request_snapshot request;
        request.prompt = "old world";
        REQUIRE( queue.enqueue( request ).accepted );
        {
            std::unique_lock<std::mutex> lock( mutex );
            REQUIRE( cv.wait_for( lock, 1s, [&]() {
                return entered;
            } ) );
        }
        queue.invalidate_session();
        {
            std::lock_guard<std::mutex> lock( mutex );
            release = true;
        }
        cv.notify_all();
        REQUIRE( queue.wait_until_idle_for_test( 1s ) );
        CHECK( queue.take_completions( 1 ).empty() );
    }

    SECTION( "world teardown clears a ready completion" ) {
        npc_ai::ai_request_queue queue( []( const std::string &prompt ) {
            return npc_ai::ai_response{ true, prompt, "" };
        } );
        npc_ai::ai_request_snapshot request;
        request.prompt = "ready in old world";
        REQUIRE( queue.enqueue( request ).accepted );
        REQUIRE( queue.wait_until_idle_for_test( 1s ) );
        REQUIRE( queue.ready_completion_count() == 1 );
        queue.invalidate_session();
        CHECK( queue.ready_completion_count() == 0 );
        CHECK( queue.take_completions( 1 ).empty() );
    }
}

TEST_CASE( "npc_ai_ready_completions_are_bounded_conserved_and_fifo_within_lane",
           "[npc_ai][npc_ai_async][npc_ai_completion_pump]" )
{
    for( const int completion_count : { 3, 10, 20 } ) {
        DYNAMIC_SECTION( "ready completions=" << completion_count ) {
            npc_ai::ai_request_queue queue( []( const std::string &prompt ) {
                return npc_ai::ai_response{ true, prompt, "" };
            }, false );
            for( int index = 0; index < completion_count; ++index ) {
                npc_ai::ai_request_snapshot request;
                request.priority = npc_ai::ai_request_priority::normal_combat;
                request.prompt = std::to_string( index );
                REQUIRE( queue.enqueue( request ).accepted );
            }
            queue.start();
            REQUIRE( queue.wait_until_idle_for_test( 1s ) );
            REQUIRE( queue.ready_completion_count() ==
                     static_cast<std::size_t>( completion_count ) );

            std::vector<std::string> applied;
            std::vector<npc_ai::ai_request_completion> first = queue.take_completions( 2 );
            CHECK( first.size() == 2 );
            CHECK( queue.ready_completion_count() ==
                   static_cast<std::size_t>( completion_count - 2 ) );
            for( const npc_ai::ai_request_completion &completion : first ) {
                applied.push_back( completion.response.text );
            }
            while( queue.ready_completion_count() > 0 ) {
                const std::vector<npc_ai::ai_request_completion> batch =
                    queue.take_completions( 2 );
                REQUIRE_FALSE( batch.empty() );
                CHECK( batch.size() <= 2 );
                for( const npc_ai::ai_request_completion &completion : batch ) {
                    applied.push_back( completion.response.text );
                }
            }

            REQUIRE( applied.size() == static_cast<std::size_t>( completion_count ) );
            for( int index = 0; index < completion_count; ++index ) {
                CHECK( applied[index] == std::to_string( index ) );
            }
        }
    }
}

TEST_CASE( "npc_ai_ready_completion_priority_preserves_player_responsiveness_and_fairness",
           "[npc_ai][npc_ai_async][npc_ai_completion_pump]" )
{
    npc_ai::ai_request_queue queue( []( const std::string &prompt ) {
        return npc_ai::ai_response{ true, prompt, "" };
    }, false );
    for( int index = 0; index < 20; ++index ) {
        npc_ai::ai_request_snapshot ambient;
        ambient.priority = npc_ai::ai_request_priority::npc_to_npc;
        ambient.prompt = "ambient-" + std::to_string( index );
        REQUIRE( queue.enqueue( ambient ).accepted );
    }
    queue.start();
    REQUIRE( queue.wait_until_idle_for_test( 1s ) );

    npc_ai::ai_request_snapshot player;
    player.priority = npc_ai::ai_request_priority::player_dialogue;
    player.type = npc_ai::ai_request_type::legacy_prompt;
    player.prompt = "player";
    REQUIRE( queue.enqueue( player ).accepted );
    REQUIRE( queue.wait_until_idle_for_test( 1s ) );
    REQUIRE( queue.ready_completion_count() == 21 );

    const std::vector<npc_ai::ai_request_completion> first = queue.take_completions( 1 );
    REQUIRE( first.size() == 1 );
    CHECK( first.front().response.text == "player" );

    std::size_t ambient_applied = 0;
    while( queue.ready_completion_count() > 0 ) {
        const std::vector<npc_ai::ai_request_completion> batch = queue.take_completions( 2 );
        REQUIRE_FALSE( batch.empty() );
        ambient_applied += batch.size();
    }
    CHECK( ambient_applied == 20 );
}

TEST_CASE( "npc_ai_completion_take_releases_mutex_for_reentrant_and_concurrent_enqueue",
           "[npc_ai][npc_ai_async][npc_ai_completion_pump]" )
{
    std::mutex mutex;
    std::condition_variable cv;
    bool second_entered = false;
    bool release_second = false;
    npc_ai::ai_request_queue queue( [&]( const std::string &prompt ) {
        if( prompt == "second" ) {
            std::unique_lock<std::mutex> lock( mutex );
            second_entered = true;
            cv.notify_all();
            cv.wait( lock, [&]() {
                return release_second;
            } );
        }
        return npc_ai::ai_response{ true, prompt, "" };
    } );

    npc_ai::ai_request_snapshot first_request;
    first_request.prompt = "first";
    REQUIRE( queue.enqueue( first_request ).accepted );
    REQUIRE( queue.wait_until_idle_for_test( 1s ) );
    REQUIRE( queue.ready_completion_count() == 1 );

    npc_ai::ai_request_snapshot second_request;
    second_request.prompt = "second";
    REQUIRE( queue.enqueue( second_request ).accepted );
    {
        std::unique_lock<std::mutex> lock( mutex );
        REQUIRE( cv.wait_for( lock, 1s, [&]() {
            return second_entered;
        } ) );
    }

    const std::vector<npc_ai::ai_request_completion> applying = queue.take_completions( 1 );
    REQUIRE( applying.size() == 1 );
    REQUIRE( applying.front().response.text == "first" );
    // This enqueue models application scheduling follow-up work.  It must not
    // re-enter a mutex retained by take_completions().
    npc_ai::ai_request_snapshot reentrant_request;
    reentrant_request.prompt = "reentrant";
    REQUIRE( queue.enqueue( reentrant_request ).accepted );
    {
        std::lock_guard<std::mutex> lock( mutex );
        release_second = true;
    }
    cv.notify_all();
    REQUIRE( queue.wait_until_idle_for_test( 1s ) );

    const std::vector<npc_ai::ai_request_completion> remaining =
        queue.take_completions( 2 );
    REQUIRE( remaining.size() == 2 );
    CHECK( remaining[0].response.text == "second" );
    CHECK( remaining[1].response.text == "reentrant" );
    CHECK( queue.ready_completion_count() == 0 );
}

TEST_CASE( "npc_ai_completion_pump_has_count_cap_soft_budget_and_combat_burst_progress",
           "[npc_ai][npc_ai_async][npc_ai_completion_pump]" )
{
    reset_async_executor reset;
    prepare_async_follower();
    const std::vector<std::string> event_kinds = {
        "COMBAT_START", "ENEMY_SPOTTED", "NPC_ATTACK", "NPC_TO_NPC_REPLY"
    };
    std::vector<std::pair<std::string, npc_ai::ai_request_priority>> requests;
    for( int index = 0; index < 20; ++index ) {
        requests.emplace_back( event_kinds[index % event_kinds.size()] + "-" +
                               std::to_string( index ),
                               npc_ai::ai_request_priority::normal_combat );
    }
    int executor_calls = 0;
    prepare_ready_legacy_completions( requests, &executor_calls );
    CHECK( executor_calls == 20 );
    Messages::clear_messages();

    const auto fixed_now = std::chrono::steady_clock::time_point{};
    npc_ai::set_ai_completion_clock_for_test( [fixed_now]() {
        return fixed_now;
    } );
    npc_ai::process_ai_completions();
    CHECK( npc_ai::get_ai_request_queue().ready_completion_count() == 18 );
    CHECK( Messages::size() == 2 );
    CHECK( executor_calls == 20 );

    while( npc_ai::get_ai_request_queue().ready_completion_count() > 0 ) {
        npc_ai::process_ai_completions();
    }
    CHECK( Messages::size() == 20 );
    CHECK( executor_calls == 20 );
    const auto messages = Messages::recent_messages( 0 );
    for( int index = 0; index < 20; ++index ) {
        const std::string marker = event_kinds[index % event_kinds.size()] + "-" +
                                   std::to_string( index );
        CHECK( std::count_if( messages.begin(), messages.end(), [&]( const auto &message ) {
            return message.second == "Qwen3: " + marker;
        } ) == 1 );
    }

    Messages::clear_messages();
    prepare_ready_legacy_completions( requests, &executor_calls );
    CHECK( executor_calls == 40 );
    int clock_calls = 0;
    npc_ai::set_ai_completion_clock_for_test( [fixed_now, &clock_calls]() mutable {
        return fixed_now + ( clock_calls++ == 0 ? 0ms : 6ms );
    } );
    npc_ai::process_ai_completions();
    CHECK( npc_ai::get_ai_request_queue().ready_completion_count() == 19 );
    CHECK( Messages::size() == 1 );
    CHECK( executor_calls == 40 );
}

TEST_CASE( "direct_npc_ai_response_is_applied_only_on_main_thread_processing",
           "[npc_ai][npc_ai_async][npc_ai_conversation]" )
{
    reset_async_executor reset;
    npc &who = prepare_async_follower();
    const std::string response = "ASYNC_TEST_" + std::to_string(
                                     std::chrono::steady_clock::now().time_since_epoch().count() );
    REQUIRE( npc_ai::build_memory_context( who ).find( response ) == std::string::npos );
    npc_ai::set_ai_request_executor_for_test( [&]( const std::string & ) {
        return npc_ai::ai_response{ true, response, "" };
    } );
    npc_ai::begin_ai_session();

    REQUIRE( npc_ai::enqueue_direct_dialogue( who, "Hola", "prompt" ).accepted );
    REQUIRE( npc_ai::get_ai_request_queue().wait_until_idle_for_test( 1s ) );
    CHECK( npc_ai::build_memory_context( who ).find( response ) == std::string::npos );

    // No dialogue UI object is retained.  Delivery remains valid after it closes.
    npc_ai::process_ai_completions();
    CHECK( npc_ai::build_memory_context( who ).find( response ) != std::string::npos );
}

TEST_CASE( "direct_npc_ai_requests_use_high_priority",
           "[npc_ai][npc_ai_async][npc_ai_conversation]" )
{
    reset_async_executor reset;
    npc &who = prepare_async_follower();
    npc_ai::set_ai_request_executor_for_test( []( const std::string & ) {
        return npc_ai::ai_response{ true, "priority", "" };
    }, false );
    npc_ai::begin_ai_session();

    REQUIRE( npc_ai::enqueue_direct_dialogue( who, "Pregunta", "prompt" ).accepted );
    npc_ai::get_ai_request_queue().start();
    REQUIRE( npc_ai::get_ai_request_queue().wait_until_idle_for_test( 1s ) );
    const auto completed = npc_ai::get_ai_request_queue().take_completions( 1 );
    REQUIRE( completed.size() == 1 );
    CHECK( completed.front().request.type == npc_ai::ai_request_type::direct_dialogue );
    CHECK( completed.front().request.priority == npc_ai::ai_request_priority::high );
}

TEST_CASE( "direct_npc_ai_requests_are_bounded_and_validate_npc_lifetime",
           "[npc_ai][npc_ai_async][npc_ai_conversation]" )
{
    reset_async_executor reset;
    npc &who = prepare_async_follower();
    std::mutex mutex;
    std::condition_variable cv;
    bool entered = false;
    bool release = false;
    npc_ai::set_ai_request_executor_for_test( [&]( const std::string & ) {
        std::unique_lock<std::mutex> lock( mutex );
        entered = true;
        cv.notify_all();
        cv.wait( lock, [&]() {
            return release;
        } );
        return npc_ai::ai_response{ true, "No debe decirse", "" };
    } );
    npc_ai::begin_ai_session();

    REQUIRE( npc_ai::enqueue_direct_dialogue( who, "Primera", "one", 100 ).accepted );
    CHECK( npc_ai::enqueue_direct_dialogue( who, "Segunda", "two", 101 ).accepted );
    CHECK( npc_ai::enqueue_direct_dialogue( who, "Tercera", "three", 102 ).accepted );
    CHECK( npc_ai::enqueue_direct_dialogue( who, "Cuarta", "four", 103 ).accepted );
    const npc_ai::ai_enqueue_result overflow =
        npc_ai::enqueue_direct_dialogue( who, "Quinta", "five", 104 );
    CHECK_FALSE( overflow.accepted );
    CHECK( overflow.error.find( "queue is full" ) != std::string::npos );
    {
        std::unique_lock<std::mutex> lock( mutex );
        REQUIRE( cv.wait_for( lock, 1s, [&]() {
            return entered;
        } ) );
    }
    const character_id removed_id = who.getID();
    g->remove_npc( removed_id );
    overmap_buffer.remove_npc( removed_id );
    {
        std::lock_guard<std::mutex> lock( mutex );
        release = true;
    }
    cv.notify_all();
    REQUIRE( npc_ai::get_ai_request_queue().wait_until_idle_for_test( 1s ) );
    REQUIRE( g->find_npc( removed_id ) == nullptr );
    while( npc_ai::get_ai_request_queue().ready_completion_count() > 0 ) {
        npc_ai::process_ai_completions();
    }
    CHECK( npc_ai::get_ai_request_queue().pending_count() == 0 );
}

TEST_CASE( "spontaneous_npc_ai_requests_suppress_duplicate_events",
           "[npc_ai][npc_ai_async][npc_ai_spontaneous]" )
{
    reset_async_executor reset;
    npc &who = prepare_async_follower();
    npc_ai::set_ai_request_executor_for_test( []( const std::string & ) {
        return npc_ai::ai_response{ true, "DECISION=SILENT", "" };
    }, false );
    npc_ai::begin_ai_session();

    REQUIRE( npc_ai::enqueue_spontaneous_dialogue( who, "prompt", "pain", "pain changed", 50,
             false ).accepted );
    CHECK_FALSE( npc_ai::enqueue_spontaneous_dialogue( who, "prompt", "pain", "pain changed", 50,
                 false ).accepted );
}

TEST_CASE( "spontaneous_response_parser_accepts_multiline_and_terminal_compact_text",
           "[npc_ai][npc_ai_async][npc_ai_spontaneous]" )
{
    using decision = npc_ai::spontaneous_response_decision;

    SECTION( "legacy multiline format remains valid" ) {
        const npc_ai::spontaneous_response_parse_result parsed =
            npc_ai::parse_spontaneous_response(
                "DECISION=TALK\nTYPE=COMMENT\nQUESTION=no\nTEXT=Hola." );
        CHECK( parsed.decision == decision::talk );
        CHECK( parsed.text == "Hola." );
    }

    SECTION( "compact terminal TEXT is extracted" ) {
        const npc_ai::spontaneous_response_parse_result parsed =
            npc_ai::parse_spontaneous_response(
                "DECISION=TALK, TYPE=COMMENT, QUESTION=no, TEXT=Hola." );
        CHECK( parsed.decision == decision::talk );
        CHECK( parsed.text == "Hola." );
    }

    SECTION( "commas inside human text are preserved literally" ) {
        const npc_ai::spontaneous_response_parse_result parsed =
            npc_ai::parse_spontaneous_response(
                "DECISION=TALK, TYPE=COMMENT, QUESTION=no, TEXT=Kim, ven aquí, rápido." );
        CHECK( parsed.decision == decision::talk );
        CHECK( parsed.text == "Kim, ven aquí, rápido." );
    }

    SECTION( "field case and whitespace are tolerated" ) {
        const npc_ai::spontaneous_response_parse_result parsed =
            npc_ai::parse_spontaneous_response(
                "DECISION=TALK, TYPE=COMMENT, QUESTION=no, Text = Hola." );
        CHECK( parsed.decision == decision::talk );
        CHECK( parsed.text == "Hola." );
    }

    SECTION( "SILENT takes priority over residual text" ) {
        const npc_ai::spontaneous_response_parse_result parsed =
            npc_ai::parse_spontaneous_response( "DECISION=SILENT, TEXT=No debo hablar." );
        CHECK( parsed.decision == decision::silent );
        CHECK( parsed.text.empty() );
    }

    SECTION( "empty diagnostics distinguish failure modes" ) {
        const npc_ai::spontaneous_response_parse_result no_field =
            npc_ai::parse_spontaneous_response( "DECISION=TALK\nTYPE=COMMENT" );
        CHECK( no_field.decision == decision::empty_text );
        CHECK( no_field.empty_reason == "NO_TEXT_FIELD" );

        const npc_ai::spontaneous_response_parse_result model_empty =
            npc_ai::parse_spontaneous_response(
                "DECISION=TALK, TYPE=COMMENT, QUESTION=no, TEXT=" );
        CHECK( model_empty.decision == decision::empty_text );
        CHECK( model_empty.empty_reason == "MODEL_EMPTY" );

        const npc_ai::spontaneous_response_parse_result sanitized =
            npc_ai::parse_spontaneous_response(
                "DECISION=TALK, TYPE=COMMENT, QUESTION=no, TEXT=\"\"" );
        CHECK( sanitized.decision == decision::empty_text );
        CHECK( sanitized.empty_reason == "SANITIZED_TO_EMPTY" );
    }
}

TEST_CASE( "spontaneous_event_detection_enqueues_once_and_discards_stale_distance",
           "[npc_ai][npc_ai_async][npc_ai_spontaneous]" )
{
    reset_async_executor reset;
    npc &who = prepare_async_follower();
    npc_ai::reset_spontaneous_state_for_test( who );
    const std::string response = "STALE_SPONTANEOUS_" + random_string( 20 );
    npc_ai::set_ai_request_executor_for_test( [&]( const std::string & ) {
        return npc_ai::ai_response{ true, "DECISION=TALK\nTEXT=" + response, "" };
    }, false );
    npc_ai::begin_ai_session();

    npc_ai::process_spontaneous_speech( who );
    calendar::turn += 20_turns;
    who.set_pain( 10 );
    npc_ai::process_spontaneous_speech( who );
    CHECK( npc_ai::get_ai_request_queue().pending_count() == 1 );

    calendar::turn += 20_turns;
    npc_ai::process_spontaneous_speech( who );
    CHECK( npc_ai::get_ai_request_queue().pending_count() == 1 );

    npc_ai::get_ai_request_queue().start();
    REQUIRE( npc_ai::get_ai_request_queue().wait_until_idle_for_test( 1s ) );
    who.setpos( get_map(), tripoint_bub_ms{ 90, 90, 0 } );
    npc_ai::process_ai_completions();
    CHECK( npc_ai::build_memory_context( who ).find( response ) ==
           std::string::npos );
}

TEST_CASE( "wrong_language_output_can_be_retried_once_without_blocking",
           "[npc_ai][npc_ai_async][npc_ai_language]" )
{
    reset_async_executor reset;
    npc_ai::set_ai_request_executor_for_test(
    []( const std::string & prompt ) {
        return npc_ai::ai_response{true, prompt, ""};
    },
    false );
    npc_ai::begin_ai_session();

    npc_ai::ai_request_snapshot request;
    request.type = npc_ai::ai_request_type::spontaneous;
    request.prompt = "prompt original";
    request.dialogue_language_code = "es_ES";
    request.dialogue_language_name = "espanol";
    request.deduplication_key = "language-retry-test";
    const npc_ai::ai_enqueue_result retry =
        npc_ai::enqueue_language_retry( request );
    REQUIRE( retry.accepted );
    CHECK( npc_ai::get_ai_request_queue().pending_count() == 1 );

    npc_ai::get_ai_request_queue().start();
    REQUIRE( npc_ai::get_ai_request_queue().wait_until_idle_for_test( 1s ) );
    const std::vector<npc_ai::ai_request_completion> completions =
        npc_ai::get_ai_request_queue().take_completions(
            npc_ai::get_ai_request_queue().ready_completion_count() );
    REQUIRE( completions.size() == 1 );
    CHECK( completions.front().request.language_retry_count == 1 );
    CHECK( completions.front().request.system_prompt.find(
               "CORRECCION PRIORITARIA DE IDIOMA" ) != std::string::npos );
    CHECK_FALSE(
        npc_ai::enqueue_language_retry( completions.front().request ).accepted );
}

TEST_CASE( "explicit_dialogue_turns_are_fifo_and_keep_their_correlation_id",
           "[npc_ai][npc_ai_async][npc_ai_conversation]" )
{
    int executor_calls = 0;
    npc_ai::ai_request_queue queue(
    [&]( const std::string & prompt, const std::string & system_prompt ) {
        ++executor_calls;
        if( prompt == "100" ) {
            return npc_ai::ai_response{true, "Could you repeat that?", ""};
        }
        if( system_prompt.find( "CORRECCION PRIORITARIA DE IDIOMA" ) !=
            std::string::npos ) {
            return npc_ai::ai_response{true, "S\u00ed, te escucho.", ""};
        }
        return npc_ai::ai_response{true, "Estoy bien.", ""};
    },
    false );
    for( const std::uint64_t turn : {
             100ULL, 101ULL
         } ) {
        npc_ai::ai_request_snapshot request;
        request.type = npc_ai::ai_request_type::direct_dialogue;
        request.priority = npc_ai::ai_request_priority::player_dialogue;
        request.npc_id = 42;
        request.conversation_id = turn;
        request.dialogue_language_code = "es_ES";
        request.dialogue_language_name = "espa\u00f1ol";
        request.prompt = std::to_string( turn );
        request.deduplication_key = "direct:1:42:" + std::to_string( turn );
        REQUIRE( queue.enqueue( request ).accepted );
    }
    queue.start();
    REQUIRE( queue.wait_until_idle_for_test( 1s ) );
    const auto completions = queue.take_completions( 2 );
    REQUIRE( completions.size() == 2 );
    CHECK( completions[0].request.conversation_id == 100 );
    CHECK( completions[1].request.conversation_id == 101 );
    CHECK( completions[0].request.language_retry_count == 1 );
    CHECK( completions[0].response.text == "S\u00ed, te escucho." );
    CHECK( completions[1].response.text == "Estoy bien." );
    CHECK( executor_calls == 3 );
}

TEST_CASE( "ai_scheduler_prioritizes_player_but_services_old_ambient_work",
           "[npc_ai][npc_ai_async][npc_ai_scheduler]" )
{
    std::vector<std::string> order;
    npc_ai::ai_request_queue queue( [&]( const std::string & prompt ) {
        order.push_back( prompt );
        return npc_ai::ai_response{ true, prompt, "" };
    }, false );
    npc_ai::ai_request_snapshot ambient;
    ambient.priority = npc_ai::ai_request_priority::ambient;
    ambient.prompt = "ambient";
    REQUIRE( queue.enqueue( ambient ).accepted );
    for( int index = 0; index < 9; ++index ) {
        npc_ai::ai_request_snapshot player;
        player.priority = npc_ai::ai_request_priority::player_dialogue;
        player.type = npc_ai::ai_request_type::legacy_prompt;
        player.prompt = "player" + std::to_string( index );
        REQUIRE( queue.enqueue( player ).accepted );
    }
    queue.start();
    REQUIRE( queue.wait_until_idle_for_test( 1s ) );
    REQUIRE( order.size() == 10 );
    CHECK( order.front() == "player0" );
    CHECK( order[8] == "ambient" );
    CHECK( order.back() == "player8" );
}

TEST_CASE( "social_director_allows_two_important_reactions_and_one_normal_reaction",
           "[npc_ai][npc_ai_async][npc_ai_social_director]" )
{
    npc_ai::ai_request_queue queue( []( const std::string & prompt ) {
        return npc_ai::ai_response{ true, prompt, "" };
    }, false );

    int accepted = 0;
    for( const int npc_id : {
             1, 2, 3
         } ) {
        npc_ai::ai_request_snapshot request;
        request.type = npc_ai::ai_request_type::combat_social;
        request.priority = npc_ai::ai_request_priority::immediate_danger;
        request.npc_id = npc_id;
        request.created_turn = 100;
        request.prompt = "important";
        request.social_event_key = "shared-important-event";
        request.social_reaction_budget = npc_ai::social_reaction_budget_for_priority( 99 );
        request.social_group_size = 3;
        accepted += queue.enqueue( request ).accepted ? 1 : 0;
    }
    CHECK( npc_ai::social_reaction_budget_for_priority( 99 ) == 2 );
    CHECK( accepted == 2 );
    CHECK( queue.pending_count() == 2 );

    npc_ai::ai_request_snapshot normal;
    normal.type = npc_ai::ai_request_type::combat_social;
    normal.npc_id = 2;
    normal.created_turn = 101;
    normal.prompt = "normal";
    normal.social_event_key = "next-normal-event";
    normal.social_reaction_budget = npc_ai::social_reaction_budget_for_priority( 80 );
    normal.social_group_size = 3;
    CHECK( queue.enqueue( normal ).accepted );

    normal.npc_id = 1;
    CHECK_FALSE( queue.enqueue( normal ).accepted );
    CHECK( queue.pending_count() == 3 );
}

TEST_CASE( "spanish_dialogue_validator_rejects_english_grammar_but_allows_"
           "entity_names",
           "[npc_ai][npc_ai_language]" )
{
    CHECK_FALSE( npc_ai::generated_text_matches_dialogue_language(
                     "Could be a bloodbath in the making.", "es_ES" ) );
    CHECK_FALSE( npc_ai::generated_text_matches_dialogue_language(
                     "Move over, colleague.", "es_ES" ) );
    CHECK_FALSE( npc_ai::generated_text_matches_dialogue_language(
                     "C'mon, bandages!", "es_ES" ) );
    CHECK_FALSE(
        npc_ai::generated_text_matches_dialogue_language( "Watch out!", "es_ES" ) );
    CHECK_FALSE(
        npc_ai::generated_text_matches_dialogue_language( "Okay.", "es_ES" ) );
    CHECK_FALSE( npc_ai::generated_text_matches_dialogue_language(
                     "Sure, sounds good.", "es_ES" ) );
    CHECK( npc_ai::generated_text_matches_dialogue_language(
               "Mierda, hay un boomer enorme al suroeste.", "es_ES" ) );
    CHECK( npc_ai::generated_text_matches_dialogue_language(
               "Cuidado con ese carnerapaz.", "es_ES" ) );
}
