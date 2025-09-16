#pragma once
#include <string>
namespace Polarlight
{
    namespace Halo
    {
        using HALO_GENERAL_FUNCPTR = void (*)();
        using HALO_VOID_FUNCPTR = void (*)(void * callTable);
        struct HaloFunction {
            HALO_VOID_FUNCPTR m_start;
            HALO_GENERAL_FUNCPTR m_stop;
        } ;


        bool loadHalo();
        void startHalo();
        void stopHalo();
        bool unloadHalo();
        std::string GetLastErrorAsString();

        extern HaloCallTable g_HaloCallTable;
    }
}
