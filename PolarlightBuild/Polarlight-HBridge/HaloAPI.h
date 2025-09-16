// HaloAPI.h
#pragma once

#include "xhost.h"
#include "xpackage.h"
#include "HaloType.h"
#include "Locker.h"
#include "singleton.h"
#include <unordered_map>
#include <string>
#include <stdexcept>
#include <iostream>
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
namespace HBridge
{
    class HaloAPI : public Singleton<HaloAPI>
    {
        // Registration of API functions and events
        BEGIN_PACKAGE(HaloAPI)
            APISET().AddEvent("OnClientAction");        // EventID: 0
            APISET().AddEvent("OnClientSendMsg");        // EventID: 1
            APISET().AddEvent("OnTriggerPairTask");     // EventID: 2
            APISET().AddEvent("OnQueryRuntimeInfo");    // EventID: 3
            APISET().AddEvent("OnPortalConfigChange");  // EventID: 4

            APISET().AddFunc<2>("SetWordBooks", &HaloAPI::SetWordbooks);
            APISET().AddFunc<1>("QueryWordBooks", &HaloAPI::QueryWordbooks);
            APISET().AddFunc<1>("RemoveWordbooks", &HaloAPI::RemoveWordbooks);
            APISET().AddFunc<1>("CloseBlockedPair", &HaloAPI::CloseBlockedPair);
        END_PACKAGE

    public:
        HaloAPI() {
            m_workerThread = std::thread(&HaloAPI::EventLoop, this);
        };
        ~HaloAPI() {
            {
                std::unique_lock<std::mutex> lock(m_queueMutex);
                m_stopWorker = true;
            }
            m_cv.notify_all();
            if (m_workerThread.joinable())
            {
                m_workerThread.join();
            }
        };

        // Initialize must be called after SetHostApis
        void Init();

        // Host API and object
        void SetHostApis(HaloCallTable* hostApis);
        void SetHostObject(const X::Value& hostObj);

        // Wordbook management
        void SetWordbooks(const std::string& key, const X::Value& value);
        X::Value QueryWordbooks(const std::string& key);
        int RemoveWordbooks(const std::string& key);

        // Pair management
        void CloseBlockedPair(const std::string& uniqueID);

        // Event firing
        void FireOnClientActionEvent(const char* name, const char* jsonArgs, const char* clientUUID);
        void FireOnClientSendMsgEvent(const char* clientId, const char* type, const char* msg, const char* key);
        void FireTriggerPairTaskEvent(const char* uniqueID);
        void FireOnQueryRuntimeInfoEvent(const std::string& name);
        void FireOnPortalConfigChangeEvent(const std::string& jsonConfig);
    private:
        struct EventInfo
        {
            int m_evntNum;
            X::ARGS m_args;
            X::KWARGS m_kwargs;
        };

        void PostEvent(int evntNum, X::ARGS args, X::KWARGS kwargs)
        {
            {
                std::unique_lock<std::mutex> lock(m_queueMutex);
                m_eventQueue.push({ evntNum, args, kwargs });
            }
            m_cv.notify_one();
        }

        void EventLoop()
        {
            const int loopInterval = 100;//ms
            const int evntNum_Timer = 2;
            int queueSize = 0;
            while (true)
            {
                EventInfo event;
                {
                    std::unique_lock<std::mutex> lock(m_queueMutex);
                    if (m_cv.wait_for(lock, std::chrono::microseconds(loopInterval),
                        [this] { return !m_eventQueue.empty() || m_stopWorker; }))
                    {
                        if (m_stopWorker && m_eventQueue.empty())
                            break;
                        event = m_eventQueue.front();
                        m_eventQueue.pop();
                        queueSize = m_eventQueue.size();
                    }
                    else
                    {
                        queueSize = m_eventQueue.size();
                        continue;
                    }
                }
                try
                {
                    Fire(event.m_evntNum, event.m_args, event.m_kwargs);
                }
                catch (const std::exception& e)
                {
                    std::cerr << "Fire Event exception: " << e.what() << "\n";
                }

                if (queueSize > 10)
                {
                    std::cerr << "Event queue size is too large: " << queueSize << std::endl;
                }
            }
        }
        HaloCallTable* mHostApis = nullptr;
        X::Value mHostObject;
        std::unordered_map<std::string, X::Value> m_wordbooks;
        Locker m_wordbook_lock;

        std::queue<EventInfo> m_eventQueue;
        std::mutex m_queueMutex;
        std::condition_variable m_cv;
        std::thread m_workerThread;
        bool m_stopWorker = false;
    };
}

