// HaloAPI.cpp
#include "HaloAPI.h"

namespace HBridge
{
    // RAII guard for Locker
    namespace {
        class LockerGuard
        {
        public:
            explicit LockerGuard(Locker& lock) : 
                m_lock(lock) { m_lock.Lock(); }
            ~LockerGuard() { m_lock.Unlock(); }
        private:
            Locker& m_lock;
        };
    }

    void HaloAPI::Init()
    {
    }

    void HaloAPI::SetHostApis(HaloCallTable* hostApis)
    {
        if (!hostApis)
        {
            throw std::invalid_argument("SetHostApis: hostApis pointer is null");
        }
        mHostApis = hostApis;
    }

    void HaloAPI::SetHostObject(const X::Value& hostObj)
    {
        if (!hostObj.IsObject())
        {
            std::cerr << "HaloAPI::SetHostObject: hostObj is null or invalid.\n";
        }
        mHostObject = hostObj;
    }

    void HaloAPI::SetWordbooks(const std::string& key, const X::Value& value)
    {
        if (key.empty())
        {
            std::cerr << "SetWordbooks: key is empty.\n";
            return;
        }
        try
        {
            LockerGuard guard(m_wordbook_lock);
            m_wordbooks[key] = value;
        }
        catch (const std::exception& e)
        {
            std::cerr << "SetWordbooks exception: " << e.what() << "\n";
        }
    }

    X::Value HaloAPI::QueryWordbooks(const std::string& key)
    {
        X::Value result;
        if (key.empty())
        {
            std::cerr << "QueryWordbooks: key is empty.\n";
            return result;
        }
        try
        {
            LockerGuard guard(m_wordbook_lock);
            auto it = m_wordbooks.find(key);
            if (it != m_wordbooks.end())
            {
                result = it->second;
            }
        }
        catch (const std::exception& e)
        {
            std::cerr << "QueryWordbooks exception: " << e.what() << "\n";
        }
        return result;
    }

    int HaloAPI::RemoveWordbooks(const std::string& key)
    {
        if (key.empty())
        {
            std::cerr << "RemoveWordbooks: key is empty.\n";
            return 0;
        }
        int removed = 0;
        try
        {
            LockerGuard guard(m_wordbook_lock);
            removed = static_cast<int>(m_wordbooks.erase(key));
        }
        catch (const std::exception& e)
        {
            std::cerr << "RemoveWordbooks exception: " << e.what() << "\n";
        }
        return removed;
    }

    void HaloAPI::CloseBlockedPair(const std::string& uniqueID)
    {
        if (!mHostApis || !mHostApis->CloseBlockedPair)
        {
            std::cerr << "CloseBlockedPair failed: HostApis not set or function pointer is null.\n";
            return;
        }
        if (uniqueID.empty())
        {
            std::cerr << "CloseBlockedPair: uniqueID is empty.\n";
            return;
        }
        try
        {
            mHostApis->CloseBlockedPair(uniqueID.c_str());
        }
        catch (const std::exception& e)
        {
            std::cerr << "CloseBlockedPair exception: " << e.what() << "\n";
        }
    }

    void HaloAPI::FireOnClientActionEvent(const char* name, const char* jsonArgs, const char* clientUUID)
    {
        if (!name || !jsonArgs || !clientUUID)
        {
            std::cerr << "FireOnClientActionEvent: invalid arguments.\n";
            return;
        }
        try
        {
            X::ARGS args(3);
            args.push_back(std::string(name));
            args.push_back(std::string(jsonArgs));
            args.push_back(std::string(clientUUID));
            X::KWARGS kwargs;
            PostEvent(0, args, kwargs);
        }
        catch (const std::exception& e)
        {
            std::cerr << "FireOnClientActionEvent exception: " << e.what() << "\n";
        }
    }

    void HaloAPI::FireOnClientSendMsgEvent(const char* clientId, const char* type, const char* msg, const char* key)
    {
        if (!clientId || !type || !msg || !key)
        {
            std::cerr << "FireOnClientSendMsgEvent: invalid arguments.\n";
            return;
        }
        try
        {
            X::ARGS args(4);
            args.push_back(std::string(clientId));
            args.push_back(std::string(type));
            args.push_back(std::string(msg));
            args.push_back(std::string(key));

            X::KWARGS kwargs;
            PostEvent(1, args, kwargs);
        }
        catch (const std::exception& e)
        {
            std::cerr << "FireOnClientSendMsgEvent exception: " << e.what() << "\n";
        }
    }

    void HaloAPI::FireTriggerPairTaskEvent(const char* uniqueID)
    {
        if (!uniqueID)
        {
            std::cerr << "FireTriggerPairTaskEvent: uniqueID is null.\n";
            return;
        }
        try
        {
            X::ARGS args(1);
            args.push_back(std::string(uniqueID));
            X::KWARGS kwargs;
            PostEvent(2, args, kwargs);
        }
        catch (const std::exception& e)
        {
            std::cerr << "FireTriggerPairTaskEvent exception: " << e.what() << "\n";
        }
    }

    void HaloAPI::FireOnQueryRuntimeInfoEvent(const std::string& name)
    {
        if (name.empty())
        {
            std::cerr << "FireOnQueryRuntimeInfoEvent: name is empty.\n";
            return;
        }
        try
        {
            X::ARGS args(1);
            args.push_back(name);
            X::KWARGS kwargs;
            PostEvent(3, args, kwargs);
        }
        catch (const std::exception& e)
        {
            std::cerr << "FireOnQueryRuntimeInfoEvent exception: " << e.what() << "\n";
        }
    }

    void HaloAPI::FireOnPortalConfigChangeEvent(const std::string& jsonConfig)
    {
        if (jsonConfig.empty())
        {
            std::cerr << "FireOnPortalConfigChangeEvent: jsonConfig is empty.\n";
            return;
        }
        try
        {
            X::ARGS args(1);
            args.push_back(jsonConfig);
            X::KWARGS kwargs;
            PostEvent(4, args, kwargs);
        }
        catch (const std::exception& e)
        {
            std::cerr << "FireOnPortalConfigChangeEvent exception: " << e.what() << "\n";
        }
    }
}
