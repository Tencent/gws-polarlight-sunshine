#pragma once

#ifdef __cplusplus
extern "C" {
#endif
    enum SunshineStatus
    {
        ClientStatus_Connect,
        ClientStatus_Disconnect,

        SessionStatus_Launch,
        SessionStatus_Terminate
    };
    //Call from bridge to sunshine
    typedef bool (*f_SetDisplayList)(const char* list);
    typedef bool (*f_StopVideoStreaming)(int streamId);
    typedef bool (*f_SetWordbooks)(const char* key,char* value);
    typedef int  (*f_QueryWordbooks)(const char* key, char* buffer, int len);
    typedef int (*f_OnQueryRuntimeInfo)(const char* name);
    typedef double (*f_OnQueryMetrics)(const char* name);
    //typedef int  (*f_QueryWordbooks)(std::string key, std::string val);

    //call from sunshine to bridge
    typedef void (*f_OnClientStatusChanged)(const char* clientId,int status);
    typedef void (*f_OnSessionStatusChanged)(int status,const char* statusInfo);

    //typedef void (*f_OnClientSendMsg)(const char* clientId,const char* type, const char* msg);
    typedef int (*f_OnClientAction)(const char* name, const char* jsonArgs, const char* aesKey);
    typedef bool (*f_ValidateDisplay)(const char* displayName);
    typedef void (*f_OnDummyDisplay)(int displayId, bool isDummy, const char* displayName);

#ifdef __cplusplus
}
#endif

struct SunshineCallTable
{
    f_SetDisplayList SetDisplayList;
    f_StopVideoStreaming StopVideoStreaming;
    f_SetWordbooks SetWordbooks;
    f_QueryWordbooks QueryWordbooks;
    f_OnClientStatusChanged OnClientStatusChanged;
    f_OnSessionStatusChanged OnSessionStatusChanged;
    f_OnClientAction OnClientAction;
    f_ValidateDisplay ValidateDisplay;
    f_OnQueryRuntimeInfo OnQueryRuntimeInfo;
    f_OnQueryMetrics OnQueryMetrics;
    f_OnDummyDisplay OnDummyDisplay;
};