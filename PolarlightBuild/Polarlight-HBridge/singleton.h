/*
Copyright (C) 2024 The XLang Foundation
Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
*/

//Copied from Xlang

#pragma once


template <class T> 
class Singleton { 
public: 
    inline static T& I() { 
        static T _instance; 
        return _instance; 
    } 

protected: 
    Singleton(void) {} 
    virtual ~Singleton(void) {} 
    Singleton(const Singleton<T>&); 
    Singleton<T>& operator= (const Singleton<T> &); 
}; 