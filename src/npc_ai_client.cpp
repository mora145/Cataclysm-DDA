#include "npc_ai_client.h"

#include <chrono>
#include <sstream>
#include <string>

#include "json.h"
#include "npc_ai_debug.h"

#if defined(_WIN32)

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <winhttp.h>

#pragma comment(lib, "winhttp.lib")

namespace
{

class winhttp_handle
{
    public:
        explicit winhttp_handle( HINTERNET handle = nullptr ) : handle_( handle ) {}

        ~winhttp_handle() {
            if( handle_ != nullptr ) {
                WinHttpCloseHandle( handle_ );
            }
        }

        winhttp_handle( const winhttp_handle & ) = delete;
        winhttp_handle &operator=( const winhttp_handle & ) = delete;

        HINTERNET get() const {
            return handle_;
        }

        explicit operator bool() const {
            return handle_ != nullptr;
        }

    private:
        HINTERNET handle_;
};

} // namespace

#endif // _WIN32

namespace npc_ai
{

namespace
{

constexpr const char *ollama_model = "qwen3:14b";
constexpr const char *ollama_diagnostics_file = "npc_ai_ollama_diagnostics.txt";

// Short NPC speech needs some variation without letting low-probability tokens
// pull a one-to-three sentence answer off role or into another language.
constexpr const char *ollama_temperature = "0.4";
// Retain a useful natural-language nucleus while trimming the model's 0.95
// default, which is unnecessarily broad for grounded dialogue and selectors.
constexpr const char *ollama_top_p = "0.85";
// Qwen's local Modelfile also uses 20; making it explicit preserves a modest
// candidate set if that external file changes.
constexpr const char *ollama_top_k = "20";
// A light penalty reduces repeated phrases/loops without distorting short names
// or the fixed keys used by structured resolver responses.
constexpr const char *ollama_repeat_penalty = "1.1";
// A four-candidate Combat Social JSON batch can legitimately exceed 96 tokens
// even though every spoken line is short.  192 keeps the response bounded but
// prevents syntactically valid batches from being cut off mid-object.
constexpr const char *ollama_num_predict_json = "192";
// Explicitly match the measured per-request runner context.  The longest
// current game prompt consumes 3497 tokens / 11904 UTF-8 bytes.  16K leaves a
// large measured margin without paying for the model's full 40960-token
// window.  The queue applies a conservative byte upper bound before send.
constexpr int ollama_num_ctx_value = 16384;
constexpr int ollama_num_predict_value = 192;
constexpr int ollama_context_margin_value = 512;
// Fixed for reproducible RAW/NEW comparison.  Context and event changes still
// produce different dialogue; identical inputs no longer add sampling noise.
constexpr int ollama_seed_value = 1;
// These are the native Qwen chat boundaries reported by the installed
// qwen3:14b Modelfile.  They stop template leakage without truncating the
// multi-line DECISION/TEXT contracts.
constexpr const char *ollama_stop_start = "<|im_start|>";
constexpr const char *ollama_stop_end = "<|im_end|>";

std::int64_t monotonic_ms()
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now().time_since_epoch() )
           .count();
}

std::string escape_json_string( const std::string &input )
{
    std::string output;
    output.reserve( input.size() + 16 );

    for( const unsigned char c : input ) {
        switch( c ) {
            case '"':
                output += "\\\"";
                break;
            case '\\':
                output += "\\\\";
                break;
            case '\b':
                output += "\\b";
                break;
            case '\f':
                output += "\\f";
                break;
            case '\n':
                output += "\\n";
                break;
            case '\r':
                output += "\\r";
                break;
            case '\t':
                output += "\\t";
                break;
            default:
                if( c < 0x20 ) {
                    const char hex[] = "0123456789abcdef";
                    output += "\\u00";
                    output += hex[( c >> 4 ) & 0x0f];
                    output += hex[c & 0x0f];
                } else {
                    output += static_cast<char>( c );
                }
                break;
        }
    }

    return output;
}

void log_ollama_request( const std::string &prompt,
                         const std::string &system_prompt )
{
    if( !runtime_debug_enabled() ) {
        return;
    }
    debug_stream output( ollama_diagnostics_file );
    if( !output ) {
        return;
    }
    output << "\n=== OLLAMA REQUEST ===\n"
           << "PARAMETERS " << ollama_request_parameters_summary() << '\n'
           << "SYSTEM_FINAL_BYTES=" << system_prompt.size()
           << "\nSYSTEM_FINAL_BEGIN\n"
           << system_prompt << "\nSYSTEM_FINAL_END\n"
           << "PROMPT_FINAL_BYTES=" << prompt.size() << "\nPROMPT_FINAL_BEGIN\n"
           << prompt << "\nPROMPT_FINAL_END\n";
}

void log_ollama_response( const std::string &raw, const std::string &clean,
                          const std::string &parse_error,
                          const int prompt_eval_count = 0,
                          const int eval_count = 0 )
{
    if( !runtime_debug_enabled() ) {
        return;
    }
    debug_stream output( ollama_diagnostics_file );
    if( !output ) {
        return;
    }
    output << "RESPONSE_RAW_BYTES=" << raw.size() << "\nRESPONSE_RAW_BEGIN\n"
           << raw << "\nRESPONSE_RAW_END\n";
    if( parse_error.empty() ) {
        output << "TOKEN_USAGE prompt_eval_count=" << prompt_eval_count
               << " eval_count=" << eval_count
               << " num_ctx=" << ollama_num_ctx_value
               << " context_truncated="
               << ( prompt_eval_count >= ollama_num_ctx_value ? "yes" : "no" ) << '\n';
        output << "RESPONSE_CLEAN_BYTES=" << clean.size()
               << "\nRESPONSE_CLEAN_BEGIN\n"
               << clean << "\nRESPONSE_CLEAN_END\n";
    } else {
        output << "RESPONSE_CLEAN_ERROR=" << parse_error << '\n';
    }
}

} // namespace

const char *ollama_model_name()
{
    return ollama_model;
}

int ollama_num_ctx()
{
    return ollama_num_ctx_value;
}

int ollama_num_predict()
{
    return ollama_num_predict_value;
}

int ollama_context_margin_tokens()
{
    return ollama_context_margin_value;
}

std::size_t ollama_hard_input_budget_bytes()
{
    // One input token cannot encode less than one byte.  Treating every UTF-8
    // byte as a token is deliberately conservative and makes this a hard
    // pre-send guard rather than a heuristic token estimate.
    return static_cast<std::size_t>( ollama_num_ctx_value -
                                     ollama_num_predict_value -
                                     ollama_context_margin_value );
}

bool ollama_prompt_fits_context( const std::string &prompt,
                                 const std::string &system_prompt )
{
    return prompt.size() + system_prompt.size() <= ollama_hard_input_budget_bytes();
}

std::string ollama_request_parameters_summary()
{
    return std::string( "model=" ) + ollama_model +
           " temperature=" + ollama_temperature + " top_p=" + ollama_top_p +
           " top_k=" + ollama_top_k + " repeat_penalty=" + ollama_repeat_penalty +
           " num_predict=" + ollama_num_predict_json +
           " num_ctx=" + std::to_string( ollama_num_ctx_value ) +
           " seed=" + std::to_string( ollama_seed_value ) +
           " stop_tokens=<|im_start|>,<|im_end|> system_field=SENT stream=false "
           "think=false"
           " keep_alive=30m";
}

std::string build_ollama_request_json( const std::string &prompt,
                                       const std::string &system_prompt )
{
    log_ollama_request( prompt, system_prompt );
    return std::string( "{\"model\":\"" ) + ollama_model + "\",\"system\":\"" +
           escape_json_string( system_prompt ) + "\",\"prompt\":\"" +
           escape_json_string( prompt ) +
           "\",\"stream\":false,\"think\":false,\"keep_alive\":\"30m\","
           "\"options\":{\"temperature\":" +
           ollama_temperature + ",\"top_p\":" + ollama_top_p +
           ",\"top_k\":" + ollama_top_k +
           ",\"repeat_penalty\":" + ollama_repeat_penalty +
           ",\"num_predict\":" + ollama_num_predict_json +
           ",\"num_ctx\":" + std::to_string( ollama_num_ctx_value ) +
           ",\"seed\":" + std::to_string( ollama_seed_value ) + ",\"stop\":[\"" +
           ollama_stop_start + "\",\"" + ollama_stop_end + "\"]}}";
}

ai_response parse_ollama_response_json( const std::string &response_body,
                                        const std::int64_t http_completed_ms )
{
    try {
        std::istringstream stream( response_body );
        TextJsonIn json_input( stream );
        TextJsonObject object = json_input.get_object();

        const std::string response_text = object.get_string( "response" );
        const int prompt_eval_count = object.get_int( "prompt_eval_count", 0 );
        const int eval_count = object.get_int( "eval_count", 0 );
        object.allow_omitted_members();
        const std::int64_t parsed_ms = monotonic_ms();
        log_ollama_response( response_body, response_text, "", prompt_eval_count,
                             eval_count );
        return {true, response_text, "", http_completed_ms, parsed_ms,
                prompt_eval_count, eval_count,
                prompt_eval_count >= ollama_num_ctx_value};
    } catch( const JsonError &err ) {
        const std::int64_t parsed_ms = monotonic_ms();
        const std::string error =
            std::string( "Invalid JSON from Ollama: " ) + err.what();
        log_ollama_response( response_body, "", error );
        return {false, "", error, http_completed_ms, parsed_ms};
    }
}

ai_response ask_ollama( const std::string &prompt,
                        const std::string &system_prompt )
{
    if( !ollama_prompt_fits_context( prompt, system_prompt ) ) {
        return {false, "", "NPC AI prompt exceeds the configured Ollama context budget."};
    }
#if !defined(_WIN32)

    return {false, "", "NPC AI currently supports Windows only."};

#else

    const std::string request_body =
        build_ollama_request_json( prompt, system_prompt );

    winhttp_handle session(
        WinHttpOpen( L"CDDA-AI/0.1", WINHTTP_ACCESS_TYPE_NO_PROXY,
                     WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0 ) );

    if( !session ) {
        return {false, "", "Could not create WinHTTP session."};
    }

    WinHttpSetTimeouts( session.get(), 3000, 3000, 5000, 60000 );

    winhttp_handle connection(
        WinHttpConnect( session.get(), L"localhost", 11434, 0 ) );

    if( !connection ) {
        return {false, "", "Could not connect to Ollama."};
    }

    winhttp_handle request(
        WinHttpOpenRequest( connection.get(), L"POST", L"/api/generate", nullptr,
                            WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, 0 ) );

    if( !request ) {
        return {false, "", "Could not create Ollama request."};
    }

    const wchar_t *headers = L"Content-Type: application/json; charset=utf-8\r\n";

    const BOOL sent =
        WinHttpSendRequest( request.get(), headers, static_cast<DWORD>( -1L ),
                            const_cast<char *>( request_body.data() ),
                            static_cast<DWORD>( request_body.size() ),
                            static_cast<DWORD>( request_body.size() ), 0 );

    if( !sent ) {
        return {false, "", "Could not send request to Ollama."};
    }

    if( !WinHttpReceiveResponse( request.get(), nullptr ) ) {
        return {false, "", "Ollama did not return a response."};
    }

    DWORD status_code = 0;
    DWORD status_size = sizeof( status_code );

    if( !WinHttpQueryHeaders(
            request.get(), WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
            WINHTTP_HEADER_NAME_BY_INDEX, &status_code, &status_size,
            WINHTTP_NO_HEADER_INDEX ) ) {
        return {false, "", "Could not read Ollama HTTP status."};
    }

    if( status_code != 200 ) {
        return {false, "",
                "Ollama returned HTTP status " + std::to_string( status_code )};
    }

    std::string response_body;

    while( true ) {
        DWORD available = 0;

        if( !WinHttpQueryDataAvailable( request.get(), &available ) ) {
            return {false, "", "Failed while reading Ollama response."};
        }

        if( available == 0 ) {
            break;
        }

        std::string buffer( available, '\0' );
        DWORD downloaded = 0;

        if( !WinHttpReadData( request.get(), buffer.data(), available,
                              &downloaded ) ) {
            return {false, "", "Failed while downloading Ollama response."};
        }

        buffer.resize( downloaded );
        response_body += buffer;
    }

    return parse_ollama_response_json( response_body, monotonic_ms() );

#endif
}

} // namespace npc_ai
