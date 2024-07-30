Windows
=======

Requirements
------------
First you need to install `MSYS2 <https://www.msys2.org>`_, then startup "MSYS2 MinGW 64-bit" and execute the following
codes.

(shawn) just run MSYS2 or go to C:\msys64 folder click on mingw64.exe

Update all packages:
   .. code-block:: bash

      pacman -Suy

Install dependencies:
   .. code-block:: bash

      pacman -S base-devel cmake diffutils gcc git make mingw-w64-x86_64-binutils \
      mingw-w64-x86_64-boost mingw-w64-x86_64-cmake mingw-w64-x86_64-curl \
      mingw-w64-x86_64-libmfx mingw-w64-x86_64-openssl mingw-w64-x86_64-opus \
      mingw-w64-x86_64-toolchain

(shawn) use pacman -S mingw-w64-x86_64-nodejs 5/5/2023
after install, nodejs, set nodejs path into windows Path
and run mingw64 again, then go to Polarlight-Sunshine folder, run npm install

npm dependencies
----------------
Install nodejs and npm. Downloads available `here <https://nodejs.org/en/download/>`_.

Install npm dependencies.
   .. code-block:: bash
    
      npm install

Build
-----
(Victor) in MINGW, mkdir build folder in Polarlight-Sunshine
(Vector) git submodule update --init --recursive

.. Attention:: Ensure you are in the build directory created during the clone step earlier before continuing.
.. code-block:: bash

 (Shawn) in MINGW, mkdir build folder in Polarlight-Sunshine.
 (Shawn) cd build, then execute below commands.

   cmake -G "MinGW Makefiles" ..
   mingw32-make -j$(nproc)

(Shawn) 9/26/2023, when meet linking problem with libboost_thread-mt.dll
replace all libboost_thread-mt.dll with libboost_thread-mt.a in build folder
using this way can also make static link to boost, no dll dependencies for boost

Pacakging (optional)
-----
   (Vector) Goto https://nsis.sourceforge.io/Download to download NSIS and perform a full installation.
   cpack -G NSIS  # optionally, create a windows installer
   cpack -G ZIP  # optionally, create a windows standalone package

Run
-----
 (Shawn)  need to copy asserts folder from Polarlight-Sunshine\src_assets\windows to build folder
 (Vector) Clink sunshine.exe in build folder.
 (Vector) Open a web browser, enter https://localhost:47990 to continue.

