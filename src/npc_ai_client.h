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

} // namespace npc_ai

#endif // CATA_SRC_NPC_AI_CLIENT_H
