#pragma once
#include "appearance.hpp"
#include <unordered_set>
namespace kalwer {
inline std::unordered_set<std::string> completed_calculations;
inline bool calculator_completed(const std::string& query) {
    if(query.empty() || completed_calculations.contains(query))return false;
    if(!wallet.credit(1,false))return false;
    completed_calculations.insert(query);return true;
}
}
