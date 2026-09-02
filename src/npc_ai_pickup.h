#pragma once
#ifndef CATA_SRC_NPC_AI_PICKUP_H
#define CATA_SRC_NPC_AI_PICKUP_H

#include <cstddef>
#include <string>
#include <vector>

class npc;

namespace npc_ai
{

struct ai_request_completion;

enum class acquisition_intent : int {
    automatic,
    wield,
    store
};

struct acquisition_intent_classification {
    bool command = false;
    acquisition_intent intent = acquisition_intent::automatic;
    std::string source;
};

struct pickup_command_result {
    bool handled = false;
    bool started = false;
    bool pending = false;
    acquisition_intent intent = acquisition_intent::automatic;
    std::string intent_source;
    std::string message;
};

struct group_acquisition_command_result {
    bool handled = false;
    std::vector<npc *> affected;
    std::size_t pending = 0;
    std::size_t failed = 0;
    std::string reply;
    npc *failure_speaker = nullptr;
    std::string failure_reply;
};

acquisition_intent_classification classify_acquisition_intent(
    const std::string &player_line );
const char *acquisition_intent_name( acquisition_intent intent );

pickup_command_result try_handle_pickup_command(
    npc &who,
    const std::string &player_line
);

group_acquisition_command_result execute_group_acquisition_command(
    const std::vector<npc *> &targets, const std::string &player_line );

void apply_pickup_ai_completion( npc &who, const ai_request_completion &completion );

} // namespace npc_ai

#endif // CATA_SRC_NPC_AI_PICKUP_H
