#pragma once
#ifndef CATA_SRC_NPC_AI_ITEM_CATALOG_H
#define CATA_SRC_NPC_AI_ITEM_CATALOG_H

#include <cstddef>
#include <string>
#include <vector>

namespace npc_ai
{

struct item_catalog_result {
    std::size_t total_types = 0;

    // Nombres legibles. Se usan principalmente para debug.
    std::vector<std::string> candidates;

    // IDs internos persistentes de CDDA.
    // Estos son los que debe utilizar la watchlist.
    std::vector<std::string> candidate_ids;
};

item_catalog_result resolve_item_candidates(
    const std::string &kind,
    const std::vector<std::string> &terms,
    std::size_t max_candidates = 40
);

} // namespace npc_ai

#endif