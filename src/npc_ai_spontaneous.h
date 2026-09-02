#pragma once

#include <cstdint>
#include <string>

class npc;

namespace npc_ai
{

struct ai_request_completion;
struct ai_completion_apply_timings;
enum class conversation_origin : int;

enum class spontaneous_response_decision : int {
    silent,
    talk,
    empty_text
};

struct spontaneous_response_parse_result {
    spontaneous_response_decision decision = spontaneous_response_decision::empty_text;
    std::string text;
    std::string empty_reason;
};

spontaneous_response_parse_result parse_spontaneous_response( const std::string &raw );

// Called from the normal NPC movement loop.
// Detects meaningful real game events and only then asks the LLM
// whether the NPC should speak or remain silent.
void process_spontaneous_speech(
    npc &who
);

// Applies a worker result on the main thread after revalidating live state.
void apply_spontaneous_ai_completion( npc &who, const ai_request_completion &completion,
                                      ai_completion_apply_timings *timings );
bool maybe_enqueue_npc_reply( const npc &speaker, const std::string &spoken,
                              conversation_origin origin,
                              std::uint64_t conversation_id, int current_reply_depth );
void reset_spontaneous_state_for_test( const npc &who );
void reset_all_spontaneous_states();

} // namespace npc_ai
