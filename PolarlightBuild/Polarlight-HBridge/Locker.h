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


#ifndef Locker_H
#define Locker_H

#ifdef WIN32
#include <Windows.h>
#else
#include <mutex> 
#endif

#if !defined(FORCE_INLINE)
#if defined(_MSC_VER)
// Microsoft Visual C++ Compiler
#define FORCE_INLINE __forceinline
#elif defined(__GNUC__) || defined(__clang__)
// GCC or Clang Compiler
#define FORCE_INLINE __attribute__((always_inline)) inline
#else
// Fallback for other compilers
#define FORCE_INLINE inline
#endif

#endif
class Locker
{
    void* m_cs = nullptr;
public:
    FORCE_INLINE Locker()
    {
#if (WIN32)
        auto* cs = new CRITICAL_SECTION();
        InitializeCriticalSection(cs);
        m_cs = (void*)cs;
#else
        m_cs = (void*)new std::mutex();
#endif
    }
    FORCE_INLINE ~Locker()
    {
#if (WIN32)
        DeleteCriticalSection((CRITICAL_SECTION*)m_cs);
        delete (CRITICAL_SECTION*)m_cs;
#else
        delete (std::mutex*)m_cs;
#endif
    }

    FORCE_INLINE void Lock()
    {
#if (WIN32)
        ::EnterCriticalSection((CRITICAL_SECTION*)m_cs);
#else
        ((std::mutex*)m_cs)->lock();
#endif
    }
    FORCE_INLINE void Unlock()
    {
#if (WIN32)
        ::LeaveCriticalSection((CRITICAL_SECTION*)m_cs);
#else
        ((std::mutex*)m_cs)->unlock();
#endif
    }
};

class AutoLock
{
public:
    FORCE_INLINE AutoLock(Locker& lk)
    {
        m_lock = &lk;
        lk.Lock();
    }

    FORCE_INLINE ~AutoLock()
    {
        if (m_lock)
        {
            m_lock->Unlock();
        }
    }

private:
    Locker* m_lock;
};

#endif //Locker_H
