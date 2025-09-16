#pragma once


#ifdef __cplusplus
extern "C" {
#endif

    //Call from halo to HBridge
    typedef void (*f_OnClientAction)(const char *name, const char *jsonArgs, const char *clientUUID);
    typedef void (*f_OnClientSendMsg)(const char *clientId, const char *type, const char *msg, const char *key);
    typedef bool (*f_QueryWordbooks)(const char* key, unsigned char* buffer, int* len1, unsigned char* buffer2, int* len2);
    typedef int (*f_RemoveWordbooks)(const char *key);
    typedef void (*f_QueryRuntimeInfo)(const char *name);
    typedef void (*f_TriggerPairTask)(const char *uniqueID);
    typedef void (*f_OnPortalConfigChange)(const char* jsonConfig);

    //call from HBridge to halo
    typedef void (*f_CloseBlockedPair)(const char* uniqueID);


    struct HaloCallTable
    {
        //Call from halo to polaris
        f_OnClientAction OnClientAction;
        f_OnClientSendMsg OnClientSendMsg;
        f_QueryWordbooks QueryWordbooks;
        f_RemoveWordbooks RemoveWordbooks;
        f_QueryRuntimeInfo QueryRuntimeInfo;
        f_TriggerPairTask TriggerPairTask;
        f_OnPortalConfigChange OnPortalConfigChange;
        //call from HBridge to halo
        f_CloseBlockedPair CloseBlockedPair;
    };

#ifdef __cplusplus
}
#endif
