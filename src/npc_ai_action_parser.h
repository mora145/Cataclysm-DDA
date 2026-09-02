#pragma once
#ifndef CATA_SRC_NPC_AI_ACTION_PARSER_H
#define CATA_SRC_NPC_AI_ACTION_PARSER_H

#include <cstddef>
#include <string>
#include <vector>

class npc;

namespace npc_ai
{

struct ai_request_completion;

struct watch_action_result {
    bool attempted = false;
    bool success = false;
    bool is_watch = false;
    bool pending = false;

    std::string kind;
    std::vector<std::string> terms;
    std::vector<std::string> candidates;

    std::size_t catalog_size = 0;

    std::string control_marker;
    std::string raw_output;
};

watch_action_result parse_watch_action(
    npc &who,
    const std::string &player_line
);

watch_action_result parse_watch_action_response( const std::string &player_line,
        const std::string &model_output );
void apply_watch_ai_completion( npc &who, const ai_request_completion &completion );

// Los marcadores antiguos generados por la conversacion
// ya no tienen autoridad y se eliminan.
void strip_watch_markers(
    std::string &text
);

} // namespace npc_ai

#endif
