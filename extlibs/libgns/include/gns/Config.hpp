/*-----------------------------------------------------------------------

Matt Marchant 2022
http://trederia.blogspot.com

crogine - Zlib license.

This software is provided 'as-is', without any express or
implied warranty.In no event will the authors be held
liable for any damages arising from the use of this software.

Permission is granted to anyone to use this software for any purpose,
including commercial applications, and to alter it and redistribute
it freely, subject to the following restrictions :

1. The origin of this software must not be misrepresented;
you must not claim that you wrote the original software.
If you use this software in a product, an acknowledgment
in the product documentation would be appreciated but
is not required.

2. Altered source versions must be plainly marked as such,
and must not be misrepresented as being the original software.

3. This notice may not be removed or altered from any
source distribution.

-----------------------------------------------------------------------*/

#pragma once

#if defined(_WIN32)

#ifndef NOMINMAX
#define NOMINMAX
#endif

#ifdef GNS_BUILD
//windows compilers need specific (and different) keywords for export
#define GNS_EXPORT_API __declspec(dllexport)
#else
#define GNS_EXPORT_API __declspec(dllimport)
#endif //GNS_BUILD


//for vc compilers we also need to turn off this annoying C4251 warning
#ifdef _MSC_VER
#pragma warning(disable: 4251)
#endif //_MSC_VER

#else //linux, FreeBSD, Mac OS X

#if __GNUC__ >= 4

//gcc 4 has special keywords for showing/hiding symbols,
//the same keyword is used for both importing and exporting
#define GNS_EXPORT_API __attribute__ ((__visibility__ ("default")))

#else

//gcc < 4 has no mechanism to explicitly hide symbols, everything's exported
#define GNS_EXPORT_API
#endif //__GNUC__

#endif //_WIN32

#include <steam/steamnetworkingsockets.h>
#include <steam/steamnetworkingtypes.h>
#include <steam/steam_api_common.h>

#ifndef STEAMNETWORKINGSOCKETS_OPENSOURCE
#include <steam/steam_api.h>
#else
#define GNS_OS
#define ISockets SteamNetworkingSockets
#endif

#ifdef GNS_DEBUG
#define DLOG(x) std::cerr << x << "\n"
#else
#define DLOG(x)
#endif