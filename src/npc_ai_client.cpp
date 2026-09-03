#include "npc_ai_client.h"

#include <atomic>
#include <cctype>
#include <chrono>
#include <cstdlib>
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

struct winhttp_post_result {
    bool transport_ok = false;
    DWORD status_code = 0;
    std::string body;
    std::string error;
};

// ASCII-only conversion.  Hosts, paths and API keys sent through this client
// are plain ASCII; anything else would be a caller bug, not user data.
std::wstring ascii_to_wide( const std::string &input )
{
    return std::wstring( input.begin(), input.end() );
}

// One blocking POST.  Shared by every provider so timeouts, status handling
// and body reading stay identical regardless of backend.
winhttp_post_result winhttp_post( const std::wstring &host, const INTERNET_PORT port,
                                  const bool secure, const std::wstring &path,
                                  const std::wstring &headers,
                                  const std::string &request_body )
{
    winhttp_post_result result;

    winhttp_handle session(
        WinHttpOpen( L"CDDA-AI/0.1", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                     WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0 ) );

    if( !session ) {
        result.error = "Could not create WinHTTP session.";
        return result;
    }

    WinHttpSetTimeouts( session.get(), 3000, 5000, 5000, 60000 );

    winhttp_handle connection(
        WinHttpConnect( session.get(), host.c_str(), port, 0 ) );

    if( !connection ) {
        result.error = "Could not connect to the LLM endpoint.";
        return result;
    }

    winhttp_handle request(
        WinHttpOpenRequest( connection.get(), L"POST", path.c_str(), nullptr,
                            WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                            secure ? WINHTTP_FLAG_SECURE : 0 ) );

    if( !request ) {
        result.error = "Could not create the HTTP request.";
        return result;
    }

    const BOOL sent =
        WinHttpSendRequest( request.get(), headers.c_str(), static_cast<DWORD>( -1L ),
                            const_cast<char *>( request_body.data() ),
                            static_cast<DWORD>( request_body.size() ),
                            static_cast<DWORD>( request_body.size() ), 0 );

    if( !sent ) {
        result.error = "Could not send the HTTP request (error " +
                       std::to_string( GetLastError() ) + ").";
        return result;
    }

    if( !WinHttpReceiveResponse( request.get(), nullptr ) ) {
        result.error = "The LLM endpoint did not return a response.";
        return result;
    }

    DWORD status_size = sizeof( result.status_code );

    if( !WinHttpQueryHeaders(
            request.get(), WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
            WINHTTP_HEADER_NAME_BY_INDEX, &result.status_code, &status_size,
            WINHTTP_NO_HEADER_INDEX ) ) {
        result.error = "Could not read the HTTP status.";
        return result;
    }

    while( true ) {
        DWORD available = 0;

        if( !WinHttpQueryDataAvailable( request.get(), &available ) ) {
            result.error = "Failed while reading the HTTP response.";
            return result;
        }

        if( available == 0 ) {
            break;
        }

        std::string buffer( available, '\0' );
        DWORD downloaded = 0;

        if( !WinHttpReadData( request.get(), buffer.data(), available,
                              &downloaded ) ) {
            result.error = "Failed while downloading the HTTP response.";
            return result;
        }

        buffer.resize( downloaded );
        result.body += buffer;
    }

    result.transport_ok = true;
    return result;
}

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

// Gemini (remote).  Sampling mirrors the Ollama profile so prompt behaviour is
// comparable across providers; only the transport and wire format differ.
constexpr const char *gemini_host = "generativelanguage.googleapis.com";
constexpr const char *gemini_default_model = "gemini-2.5-flash";
constexpr int gemini_default_max_output_tokens = 256;
constexpr const char *gemini_temperature = "0.4";
constexpr const char *gemini_top_p = "0.85";
constexpr const char *gemini_top_k = "20";
constexpr int gemini_seed_value = 1;
// Gemini 2.5 models reason by default.  NPC lines are short and grounded by
// the prompt; reasoning only adds latency and cost, so it is disabled.
constexpr int gemini_thinking_budget = 0;
// After a quota (429) or overload (503) answer, fail fast for this long instead
// of hammering the endpoint.  The queue already treats a failed request as a
// dropped line, so this only degrades social volume, never gameplay speed.
constexpr std::int64_t gemini_backoff_ms = 20000;

std::atomic<std::int64_t> gemini_backoff_until_ms{ 0 };

std::int64_t monotonic_ms()
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now().time_since_epoch() )
           .count();
}

std::string environment_value( const char *name )
{
    const char *value = std::getenv( name );
    return value != nullptr ? std::string( value ) : std::string();
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

void log_llm_request( const char *provider_label, const std::string &parameters,
                      const std::string &prompt, const std::string &system_prompt )
{
    if( !runtime_debug_enabled() ) {
        return;
    }
    debug_stream output( ollama_diagnostics_file );
    if( !output ) {
        return;
    }
    output << "\n=== " << provider_label << " REQUEST ===\n"
           << "PARAMETERS " << parameters << '\n'
           << "SYSTEM_FINAL_BYTES=" << system_prompt.size()
           << "\nSYSTEM_FINAL_BEGIN\n"
           << system_prompt << "\nSYSTEM_FINAL_END\n"
           << "PROMPT_FINAL_BYTES=" << prompt.size() << "\nPROMPT_FINAL_BEGIN\n"
           << prompt << "\nPROMPT_FINAL_END\n";
}

void log_llm_response( const std::string &raw, const std::string &clean,
                       const std::string &parse_error, const int prompt_eval_count,
                       const int eval_count, const int context_limit,
                       const bool truncated )
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
               << " num_ctx=" << context_limit
               << " context_truncated=" << ( truncated ? "yes" : "no" ) << '\n';
        output << "RESPONSE_CLEAN_BYTES=" << clean.size()
               << "\nRESPONSE_CLEAN_BEGIN\n"
               << clean << "\nRESPONSE_CLEAN_END\n";
    } else {
        output << "RESPONSE_CLEAN_ERROR=" << parse_error << '\n';
    }
}

// Never logged, never copied anywhere else.  Read once per process.
const std::string &gemini_api_key()
{
    static const std::string key = environment_value( "CDDA_NPC_AI_GEMINI_API_KEY" );
    return key;
}

} // namespace

// ---------------------------------------------------------------------------
// Ollama (local)
// ---------------------------------------------------------------------------

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
    log_llm_request( "OLLAMA", ollama_request_parameters_summary(), prompt, system_prompt );
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
        const bool truncated = prompt_eval_count >= ollama_num_ctx_value;
        log_llm_response( response_body, response_text, "", prompt_eval_count,
                          eval_count, ollama_num_ctx_value, truncated );
        return {true, response_text, "", http_completed_ms, parsed_ms,
                prompt_eval_count, eval_count, truncated};
    } catch( const JsonError &err ) {
        const std::int64_t parsed_ms = monotonic_ms();
        const std::string error =
            std::string( "Invalid JSON from Ollama: " ) + err.what();
        log_llm_response( response_body, "", error, 0, 0, ollama_num_ctx_value, false );
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

    const winhttp_post_result post = winhttp_post(
                                         L"localhost", 11434, false, L"/api/generate",
                                         L"Content-Type: application/json; charset=utf-8\r\n",
                                         request_body );

    if( !post.transport_ok ) {
        return {false, "", "Ollama: " + post.error};
    }

    if( post.status_code != 200 ) {
        return {false, "",
                "Ollama returned HTTP status " + std::to_string( post.status_code )};
    }

    return parse_ollama_response_json( post.body, monotonic_ms() );

#endif
}

// ---------------------------------------------------------------------------
// Gemini (remote)
// ---------------------------------------------------------------------------

llm_provider active_llm_provider()
{
    static const llm_provider provider = []() {
        std::string value = environment_value( "CDDA_NPC_AI_PROVIDER" );
        for( char &c : value ) {
            c = static_cast<char>( std::tolower( static_cast<unsigned char>( c ) ) );
        }
        return value == "gemini" ? llm_provider::gemini : llm_provider::ollama;
    }();
    return provider;
}

const char *active_llm_provider_name()
{
    return active_llm_provider() == llm_provider::gemini ? "gemini" : "ollama";
}

std::string gemini_model_name()
{
    static const std::string model = []() {
        const std::string value = environment_value( "CDDA_NPC_AI_GEMINI_MODEL" );
        return value.empty() ? std::string( gemini_default_model ) : value;
    }();
    return model;
}

int gemini_max_output_tokens()
{
    static const int tokens = []() {
        const std::string value = environment_value( "CDDA_NPC_AI_GEMINI_MAX_TOKENS" );
        const int parsed = value.empty() ? 0 : std::atoi( value.c_str() );
        return parsed > 0 ? parsed : gemini_default_max_output_tokens;
    }();
    return tokens;
}

bool gemini_api_key_available()
{
    return !gemini_api_key().empty();
}

std::string gemini_request_parameters_summary()
{
    return "model=" + gemini_model_name() +
           " temperature=" + gemini_temperature + " top_p=" + gemini_top_p +
           " top_k=" + gemini_top_k +
           " max_output_tokens=" + std::to_string( gemini_max_output_tokens() ) +
           " seed=" + std::to_string( gemini_seed_value ) +
           " thinking_budget=" + std::to_string( gemini_thinking_budget ) +
           " system_instruction=SENT api_key=" +
           ( gemini_api_key_available() ? "present" : "MISSING" );
}

std::string build_gemini_request_json( const std::string &prompt,
                                       const std::string &system_prompt )
{
    log_llm_request( "GEMINI", gemini_request_parameters_summary(), prompt, system_prompt );
    return std::string( "{\"system_instruction\":{\"parts\":[{\"text\":\"" ) +
           escape_json_string( system_prompt ) +
           "\"}]},\"contents\":[{\"role\":\"user\",\"parts\":[{\"text\":\"" +
           escape_json_string( prompt ) +
           "\"}]}],\"generationConfig\":{\"temperature\":" + gemini_temperature +
           ",\"topP\":" + gemini_top_p + ",\"topK\":" + gemini_top_k +
           ",\"maxOutputTokens\":" + std::to_string( gemini_max_output_tokens() ) +
           ",\"seed\":" + std::to_string( gemini_seed_value ) +
           ",\"thinkingConfig\":{\"thinkingBudget\":" +
           std::to_string( gemini_thinking_budget ) + "}}}";
}

ai_response parse_gemini_response_json( const std::string &response_body,
                                        const std::int64_t http_completed_ms )
{
    try {
        std::istringstream stream( response_body );
        TextJsonIn json_input( stream );
        TextJsonObject object = json_input.get_object();
        object.allow_omitted_members();

        // Error envelope: {"error":{"code":429,"message":"...","status":"..."}}
        if( object.has_object( "error" ) ) {
            TextJsonObject error_object = object.get_object( "error" );
            error_object.allow_omitted_members();
            const std::string error = "Gemini error " +
                                      std::to_string( error_object.get_int( "code", 0 ) ) + " " +
                                      error_object.get_string( "status", "" ) + ": " +
                                      error_object.get_string( "message", "" );
            log_llm_response( response_body, "", error, 0, 0, 0, false );
            return {false, "", error, http_completed_ms, monotonic_ms()};
        }

        int prompt_tokens = 0;
        int output_tokens = 0;
        if( object.has_object( "usageMetadata" ) ) {
            TextJsonObject usage = object.get_object( "usageMetadata" );
            usage.allow_omitted_members();
            prompt_tokens = usage.get_int( "promptTokenCount", 0 );
            output_tokens = usage.get_int( "candidatesTokenCount", 0 );
        }

        if( !object.has_array( "candidates" ) ) {
            std::string reason = "no candidates";
            if( object.has_object( "promptFeedback" ) ) {
                TextJsonObject feedback = object.get_object( "promptFeedback" );
                feedback.allow_omitted_members();
                reason = "prompt blocked: " + feedback.get_string( "blockReason", "unknown" );
            }
            const std::string error = "Gemini returned " + reason;
            log_llm_response( response_body, "", error, prompt_tokens, output_tokens, 0, false );
            return {false, "", error, http_completed_ms, monotonic_ms(), prompt_tokens,
                    output_tokens, false};
        }

        TextJsonArray candidates = object.get_array( "candidates" );
        if( !candidates.has_more() ) {
            const std::string error = "Gemini returned an empty candidate list";
            log_llm_response( response_body, "", error, prompt_tokens, output_tokens, 0, false );
            return {false, "", error, http_completed_ms, monotonic_ms(), prompt_tokens,
                    output_tokens, false};
        }

        TextJsonObject candidate = candidates.next_object();
        candidate.allow_omitted_members();
        const std::string finish_reason = candidate.get_string( "finishReason", "" );

        std::string response_text;
        if( candidate.has_object( "content" ) ) {
            TextJsonObject content = candidate.get_object( "content" );
            content.allow_omitted_members();
            if( content.has_array( "parts" ) ) {
                TextJsonArray parts = content.get_array( "parts" );
                while( parts.has_more() ) {
                    TextJsonObject part = parts.next_object();
                    part.allow_omitted_members();
                    // Thought parts are excluded by design; only spoken text
                    // is ever handed to the validators.
                    if( part.has_string( "text" ) ) {
                        response_text += part.get_string( "text" );
                    }
                }
            }
        }

        if( response_text.empty() ) {
            const std::string error = "Gemini candidate had no text (finishReason=" +
                                      finish_reason + ")";
            log_llm_response( response_body, "", error, prompt_tokens, output_tokens, 0, false );
            return {false, "", error, http_completed_ms, monotonic_ms(), prompt_tokens,
                    output_tokens, false};
        }

        // MAX_TOKENS means the model was cut off mid-answer.  Downstream treats
        // context_truncated as unusable output, which is exactly what a
        // half-written JSON batch or sentence deserves.
        const bool truncated = finish_reason == "MAX_TOKENS";
        const std::int64_t parsed_ms = monotonic_ms();
        log_llm_response( response_body, response_text, "", prompt_tokens, output_tokens,
                          gemini_max_output_tokens(), truncated );
        return {true, response_text, "", http_completed_ms, parsed_ms, prompt_tokens,
                output_tokens, truncated};
    } catch( const JsonError &err ) {
        const std::int64_t parsed_ms = monotonic_ms();
        const std::string error =
            std::string( "Invalid JSON from Gemini: " ) + err.what();
        log_llm_response( response_body, "", error, 0, 0, 0, false );
        return {false, "", error, http_completed_ms, parsed_ms};
    }
}

ai_response ask_gemini( const std::string &prompt,
                        const std::string &system_prompt )
{
    if( !gemini_api_key_available() ) {
        return {false, "", "CDDA_NPC_AI_GEMINI_API_KEY is not set."};
    }
    // Keep the same byte guard as Ollama.  Gemini accepts far more, but the
    // prompt builders are budgeted around this limit and it bounds cost.
    if( !ollama_prompt_fits_context( prompt, system_prompt ) ) {
        return {false, "", "NPC AI prompt exceeds the configured context budget."};
    }
    const std::int64_t now_ms = monotonic_ms();
    if( now_ms < gemini_backoff_until_ms.load( std::memory_order_relaxed ) ) {
        return {false, "", "Gemini is in quota backoff; request skipped."};
    }
#if !defined(_WIN32)

    return {false, "", "NPC AI currently supports Windows only."};

#else

    const std::string request_body =
        build_gemini_request_json( prompt, system_prompt );

    const std::wstring path = ascii_to_wide( "/v1beta/models/" + gemini_model_name() +
                              ":generateContent" );
    const std::wstring headers =
        L"Content-Type: application/json; charset=utf-8\r\n"
        L"x-goog-api-key: " + ascii_to_wide( gemini_api_key() ) + L"\r\n";

    const winhttp_post_result post = winhttp_post(
                                         ascii_to_wide( gemini_host ), INTERNET_DEFAULT_HTTPS_PORT, true,
                                         path, headers, request_body );

    if( !post.transport_ok ) {
        return {false, "", "Gemini: " + post.error};
    }

    if( post.status_code == 429 || post.status_code == 503 ) {
        gemini_backoff_until_ms.store( monotonic_ms() + gemini_backoff_ms,
                                       std::memory_order_relaxed );
    }

    if( post.status_code != 200 ) {
        // The body usually carries a structured error; surface it when present.
        const ai_response parsed = parse_gemini_response_json( post.body, monotonic_ms() );
        if( !parsed.error.empty() && parsed.error.rfind( "Invalid JSON", 0 ) != 0 ) {
            return {false, "", parsed.error + " (HTTP " + std::to_string( post.status_code ) + ")"};
        }
        return {false, "",
                "Gemini returned HTTP status " + std::to_string( post.status_code )};
    }

    return parse_gemini_response_json( post.body, monotonic_ms() );

#endif
}

// ---------------------------------------------------------------------------
// Dispatcher
// ---------------------------------------------------------------------------

ai_response ask_llm( const std::string &prompt,
                     const std::string &system_prompt )
{
    switch( active_llm_provider() ) {
        case llm_provider::gemini:
            return ask_gemini( prompt, system_prompt );
        case llm_provider::ollama:
        default:
            return ask_ollama( prompt, system_prompt );
    }
}

} // namespace npc_ai
