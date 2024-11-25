#include<set>
#include<iostream>
#include<utility>

extern "C" {
    #include "cache_lib.h"
    #include "statistics.h"
    #include "globals/global_types.h"

    std::set<std::pair<uns, Addr>> visited;

    // If this function is called, a cache miss took place
    void cache_miss_type_counter(Cache* cache, uns set, Addr tag, uns procid){
        // Check if set and tag was previously accessed, if not, then compulsary miss
        std::pair<uns, Addr> key = std::make_pair(set, tag);
        if(visited.count(key) == 0){
            visited.insert(key);
            STAT_EVENT(procid, DCACHE_MISS_ONPATH_COMPULSORY);
            return;
        }

        // Else it's a non compulsary miss
        STAT_EVENT(procid, DCACHE_MISS_ONPATH_NON_COMPULSORY);
    }
}