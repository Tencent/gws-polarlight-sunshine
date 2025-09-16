#pragma once
#include <string>
#include <map>
#include <mutex>
#include <functional>
#include <iostream>
#include "singleton.h"

using metrics_callback_func =  std::function<double ()>;
using metrics_callback_func_reset =  std::function<void ()>;

struct metrics_callback_info{
    metrics_callback_func callback_func;
    metrics_callback_func_reset callback_func_reset;
};

class MetricsManager:
    public Singleton <MetricsManager> {
    inline unsigned long long
    CombineIds(int setId, int counterId) {
        unsigned long long Ids = setId;
        Ids <<= 32;
        Ids |= counterId;
        return Ids;
    }
    inline int
    getSetId(unsigned long long combinedId) {
        int setId;
        combinedId >>= 32;
        setId = (int) combinedId;
        return setId;
    }
    inline int
    getcounterId(unsigned long long combinedId) {
        int counterId;
        combinedId &= 0xFFFFFFFF;
        counterId = (int) counterId;
        return counterId;
    }

public:
    MetricsManager() {
    }
    ~MetricsManager() {
    }
    void register_metrics (std::string metrics_key,
        int setId,int counterId,
        metrics_callback_func callback_func, metrics_callback_func_reset callback_func_reset){
        lock_metrics();
        metrics_map.emplace (std::make_pair(metrics_key, metrics_callback_info{callback_func, callback_func_reset}));
        //if some metrics are not open for PerfCounter,skipt them
        if (setId >= 0 && counterId >= 0) {
            counter_map.emplace(std::make_pair(CombineIds(setId, counterId),
                                                metrics_callback_info{callback_func, callback_func_reset}));
        }
        unlock_metrics();
    };
    double query_metrics (std::string metrics_key) {
        metrics_callback_info my_func;
        bool func_found = false;
        lock_metrics();
        auto metrics_it = metrics_map.find (metrics_key);
        if (metrics_it != metrics_map.end()) {
            func_found = true;
            my_func = metrics_it->second;
        }
        unlock_metrics();
        if (func_found) {
            return my_func.callback_func();
        }
        else {
            return 0.0;
        }
    }
    double query_metrics (int setId,int counterId) {
        metrics_callback_info my_func;
        bool func_found = false;
        lock_metrics();
        auto metrics_it = counter_map.find(CombineIds(setId, counterId));
        if (metrics_it != counter_map.end()) {
            func_found = true;
            my_func = metrics_it->second;
        }
        unlock_metrics();
        if (func_found) {
            return my_func.callback_func();
        }
        else {
            return 0.0;
        }
    }
    void reset_metrics () {
        lock_metrics();
        for(auto &metrics_item:metrics_map) {
            unlock_metrics();
            metrics_item.second.callback_func_reset();
            lock_metrics();
        }
        unlock_metrics();
    }

private:
    std::map <std::string, metrics_callback_info> metrics_map;
    std::map<unsigned long long, metrics_callback_info> counter_map;
    std::mutex metrics_mutex;
    void lock_metrics() {
        metrics_mutex.lock();
    }
    void unlock_metrics() {
        metrics_mutex.unlock();
    }
};
