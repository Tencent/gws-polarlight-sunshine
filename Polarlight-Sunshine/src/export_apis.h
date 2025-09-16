#pragma once

#include <string>

void OnClientStatusChanged(std::string clientId,int status);
void OnSessionStatusChanged(int status, std::string statusInfo);
void OnDummyDisplay(int displayId, bool isDummy, const std::string& displayName);
bool LoadBridge(const char* hostPath);
void UnloadBridge();
bool QueryWordbooks(std::string strKey,std::string& strValue);
bool SetWordbooks(const char *key, char *value);
bool RemoveWordbooks(std::string strKey);
