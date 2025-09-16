#include <Windows.h>
#include <iostream>
#include "HaloAPI.h"
#include "HaloType.h"
#include "halo.h"

namespace Polarlight
{
    namespace Halo
    {
        
        HaloFunction g_http;
        HaloCallTable g_HaloCallTable = { 0 };
        HMODULE hDll;
        const char* dllName = "libhalo.dll";

        
        std::string GetLastErrorAsString() {

            DWORD error = GetLastError();

            if (error == 0) {
                return "No error has occurred.";
            }

            LPVOID lpMsgBuf;
            DWORD dwFlags = FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS;
            FormatMessage(dwFlags, NULL, error, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (LPTSTR)&lpMsgBuf, 0, NULL);

            std::string errorString((char*)lpMsgBuf);

            LocalFree(lpMsgBuf);

            return errorString;
        }

        bool loadHalo() {
            if (GetFileAttributes(dllName) == INVALID_FILE_ATTRIBUTES) {
                std::cerr << "File " << dllName << " does not exist." << std::endl;
                return false;
            }
            hDll = LoadLibrary(dllName);
            if (hDll == NULL) {
                std::cerr << "Load library "
                    << std::string_view(dllName)
                    << " failed"
                    << std::endl;
                std::cerr << GetLastErrorAsString() << std::endl;
                return false;
            }

            g_http.m_start = (HALO_VOID_FUNCPTR)GetProcAddress(hDll, "StartHalo");
            if (g_http.m_start == NULL) {
                std::cerr << "StartHalo not found" << std::endl;
                return false;
            }

            g_http.m_stop = (HALO_GENERAL_FUNCPTR)GetProcAddress(hDll, "StopHalo");
            if (g_http.m_stop == NULL) {
                std::cerr << "StopHalo not found" << std::endl;
                return false;
            }
            return true;
        }

        bool unloadHalo() {
            BOOL succ = FreeLibrary(hDll);
            if (succ == FALSE) {
                std::cerr << "Unload library "
                    << std::string_view(dllName)
                    << " failed"
                    << std::endl;
                std::cerr << GetLastErrorAsString() << std::endl;
            }
            return succ != FALSE;
        }

        void startHalo() {

            g_HaloCallTable.OnClientAction = (f_OnClientAction)(
                [](const char *name, const char *jsonArgs, const char *clientUUID)-> void {
                std::cout << "FireOnClientActionEvent : " << std::endl;
                HBridge::HaloAPI::I().FireOnClientActionEvent(name, jsonArgs, clientUUID);
            });

            g_HaloCallTable.OnClientSendMsg = (f_OnClientSendMsg)(
                [](const char *clientId, const char *type, const char *msg, const char* key)-> void {
                std::cout << "FireOnClientSendMsgEvent : type = " << type << ", key = " << key << std::endl;
                HBridge::HaloAPI::I().FireOnClientSendMsgEvent(clientId, type, msg, key);
            });
            
            g_HaloCallTable.TriggerPairTask = (f_TriggerPairTask)([](const char * uniqueID)-> void {
                std::cout << "FireTriggerPairTask" << std::endl;
                HBridge::HaloAPI::I().FireTriggerPairTaskEvent(uniqueID);
            });

            g_HaloCallTable.QueryRuntimeInfo = (f_QueryRuntimeInfo)([](const char* name) -> void {
                return HBridge::HaloAPI::I().FireOnQueryRuntimeInfoEvent(name);
                });

            g_HaloCallTable.OnPortalConfigChange = (f_OnPortalConfigChange)([](const char* jsonConfig)-> void {
                return HBridge::HaloAPI::I().FireOnPortalConfigChangeEvent(jsonConfig);
                });

            g_HaloCallTable.QueryWordbooks = (f_QueryWordbooks)(
                [](const char *key, unsigned char *buffer1, int *len1, unsigned char* buffer2, int *len2 )-> bool {
                X::Value val = HBridge::HaloAPI::I().QueryWordbooks(key);
                if (val.IsObject()) {
                    auto obj = val.GetObj();
                    auto t = obj->GetType();
                    switch (t) {
                        case X::ObjType::Str: {
                            //std::cerr << "val is obj.str" << std::endl;
                            std::string tmpStr(dynamic_cast<X::XStr *>(obj)->Buffer());
                            if (*len1 < tmpStr.size()) {
                                std::cerr << "buffer length : " << *len1 << " is smaller than value size : " << tmpStr.size() << std::endl;
                                tmpStr.resize(*len1);
                            }
                            // halo use vector<char> save buffer,  no need last \0
                            memcpy(buffer1, tmpStr.c_str(), tmpStr.size());
                            *len2 = 0;
                            *len1 = (int)tmpStr.size();
                            return true;
                        }break;
                        case X::ObjType::List: {
                            X::List list(val);
                            //std::cerr << "val is obj.binary" << std::endl;
                            int size = (int)list.Size();
                            if (size != 2) {//list[0]bin, list[1] type
                                *len1 = 0;
                                return true;
                            }

                            auto item = list->Get(0);
                            X::XBin* bin = nullptr;
                            if (item.IsObject()) // item maybe not bin, why?
                            {
                                auto obj0 = item.GetObj();
                                bin = dynamic_cast<X::XBin*>(obj0);
                            }
                            if (bin == nullptr) {
                                *len2 = 0;
                                *len1 = 0;
                                return true;
                            }
                            char* data1 = bin->Data();
                            auto size1 = bin->Size();
                            if (*len1 < size1) {
                                std::cerr << "buffer1 length : " << *len1 << " is smaller than binary size : " << size1 << std::endl;
                                size1 = *len1;
                            }
                            memcpy(buffer1, data1, size1);
                            *len1 = (int)size1;

                            std::string contextType = list->Get(1).ToString();
                            int size2 = (int)contextType.size();
                            if (*len2 < size2) {
                                std::cerr << "buffer2 length : " << *len2 << " is smaller than contextType size : " << size2 << std::endl;
                                size2 = *len2;
                            }
                            memcpy(buffer2, contextType.c_str(), size2);
                            *len2 = size2;
                            return true;
                        }break;
                        case X::ObjType::Binary: {
                            //std::cerr << "val is obj.binary" << std::endl;
                            X::XBin* bin = dynamic_cast<X::XBin*>(obj);
                            char* data = bin->Data();
                            auto size = bin->Size();

                            if (*len1 < size) {
                                std::cerr << "buffer length : " << *len1 << " is smaller than binary size : " << size << std::endl;
                                size = *len1;
                            }
                            memcpy(buffer1, data, size);
                            *len1 = (int)size;
                            *len2 = 0;
                            return true;
                        }break;
                        default: {
                            std::cerr << "unhandled wordbook value type : X::ObjType::" << static_cast<int>(t) << std::endl;
                        }
                    }
                } else if (val.GetType() != X::ValueType::Invalid) {
                    std::cerr << "" << val.GetValueType() << std::endl;
                }
                // query failed
                *len1 = 0;
                return false;
            });

            g_HaloCallTable.RemoveWordbooks = (f_RemoveWordbooks)([](const char* key)-> int {
                return HBridge::HaloAPI::I().RemoveWordbooks(key);
            });


            g_http.m_start((void*)&g_HaloCallTable);
        }

        void stopHalo() {
            g_http.m_stop();
        }
    }
}