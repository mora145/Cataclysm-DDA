#pragma once
#ifndef CATA_SRC_NPC_AI_CLIENT_H
#define CATA_SRC_NPC_AI_CLIENT_H

#include <cstddef>
#include <cstdint>
#include <string>

namespace npc_ai
{

struct ai_response {
    bool success = false;
    std::string text;
    std::string error;
    std::int64_t http_completed_ms = 0;
    std::int64_t parse_completed_ms = 0;
    // Ollama's only observable truncation signal.  Keep it on every response
    // so callers and diagnostics can prove what context the model consumed.
    int prompt_eval_count = 0;
    int eval_count = 0;
    bool context_truncated = false;
};

const char *ollama_model_name();
int ollama_num_ctx();
int ollama_num_predict();
int ollama_context_margin_tokens();
std::size_t ollama_hard_input_budget_bytes();
bool ollama_prompt_fits_context( const std::string &prompt,
                                 const std::string &system_prompt );
std::string ollama_request_parameters_summary();
std::string build_ollama_request_json( const std::string &prompt,
                                       const std::string &system_prompt );
ai_response parse_ollama_response_json( const std::string &response_body,
                                        std::int64_t http_completed_ms = 0 );
ai_response ask_ollama( const std::string &prompt,
                        const std::string &system_prompt );

// Remote provider support (testing branch).  The provider is selected at
// runtime through the environment variable CDDA_NPC_AI_PROVIDER: "ollama"
// (default) or "gemini".  Secrets never live in code or in the repository:
// Gemini reads CDDA_NPC_AI_GEMINI_API_KEY from the environment at startup.
// Optional overrides: CDDA_NPC_AI_GEMINI_MODEL (default "gemini-2.5-flash")
// and CDDA_NPC_AI_GEMINI_MAX_TOKENS (default 256).
enum class llm_provider : int {
    ollama,
    gemini,
    // Any OpenAI-compatible chat completions endpoint.  Defaults target
    // DeepInfra with Qwen/Qwen3-14B, the same model the local Ollama profile
    // uses, so prompts and validators need no recalibration.
    // CDDA_NPC_AI_PROVIDER=openai (alias: deepinfra)
    // CDDA_NPC_AI_OPENAI_API_KEY   bearer token (required)
    // CDDA_NPC_AI_OPENAI_HOST      default api.deepinfra.com
    // CDDA_NPC_AI_OPENAI_PATH      default /v1/openai/chat/completions
    // CDDA_NPC_AI_OPENAI_MODEL     default Qwen/Qwen3-14B
    // CDDA_NPC_AI_OPENAI_MAX_TOKENS default 192 (matches Ollama num_predict)
    // CDDA_NPC_AI_OPENAI_EXTRA_JSON optional raw JSON members appended to the
    //                              request body, e.g. provider-specific knobs.
    openai
};

std::string openai_model_name();
std::string openai_host();
std::string openai_path();
int openai_max_output_tokens();
bool openai_api_key_available();
std::string openai_request_parameters_summary();
std::string build_openai_request_json( const std::string &prompt,
                                       const std::string &system_prompt );
ai_response parse_openai_response_json( const std::string &response_body,
                                        std::int64_t http_completed_ms = 0 );
ai_response ask_openai( const std::string &prompt,
                        const std::string &system_prompt );

llm_provider active_llm_provider();
const char *active_llm_provider_name();
std::string gemini_model_name();
int gemini_max_output_tokens();
bool gemini_api_key_available();
std::string gemini_request_parameters_summary();
std::string build_gemini_request_json( const std::string &prompt,
                                       const std::string &system_prompt );
ai_response parse_gemini_response_json( const std::string &response_body,
                                        std::int64_t http_completed_ms = 0 );
ai_response ask_gemini( const std::string &prompt,
                        const std::string &system_prompt );

// Production executor: dispatches to whichever provider is active.  Callers
// that need a specific backend keep calling ask_ollama / ask_gemini directly.
ai_response ask_llm( const std::string &prompt,
                     const std::string &system_prompt );

} // namespace npc_ai

#endif // CATA_SRC_NPC_AI_CLIENT_H
