/*-----------------------------------------------------------------------

Matt Marchant 2017 - 2020
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

#include <crogine/detail/Assert.hpp>
#include <crogine/detail/Types.hpp>

#include <SDL_rwops.h>

#include <string>
#include <algorithm>

#ifdef _WIN32
#include <shellapi.h>
#else
#include <stdlib.h>
#endif


namespace cro::Util::String
{
    /*!
    \brief Remove all instances of c from line
    */
    static inline void removeChar(std::string& line, const char c)
    {
        line.erase(std::remove(line.begin(), line.end(), c), line.end());
    }

    /*!
    \brief Replaces all instances of c with r
    */
    static inline void replace(std::string& str, char c, char r)
    {
        std::size_t start = 0u;
        auto next = str.find_first_of(c, start);
        while (next != std::string::npos && start < str.length())
        {
            str.replace(next, 1, &r);
            start = ++next;
            next = str.find_first_of(c, start);
            if (next > str.length()) next = std::string::npos;
        }
    }

    /*!
    \brief Replace all instances of s with r
    https://stackoverflow.com/a/3418285/6740859
    */
    static inline void replace(std::string& str, const std::string& s, const std::string& r)
    {
        if (s.empty())
        {
            return;
        }

        std::size_t startPos = 0;
        while ((startPos = str.find(s, startPos)) != std::string::npos) 
        {
            str.replace(startPos, s.length(), r);
            startPos += r.length();
        }
    }

    /*!
    \brief Converts the given string to lower case
    */
    static inline std::string toLower(const std::string& str)
    {
        std::string retVal(str);
        std::transform(retVal.begin(), retVal.end(), retVal.begin(), ::tolower);
        return retVal;
    }

    /*!
    \brief Emulates the C function fgetc with a RWops file/stream
    */
    static inline int rwgetc(SDL_RWops *file)
    {
        unsigned char c;
        return SDL_RWread(file, &c, 1, 1) == 1 ? c : EOF;
    }

    /*!
    \brief Emulates the C function fgets with an SDL rwops file
    */
    static inline char* rwgets(char* dest, std::int32_t size, SDL_RWops* file, std::int64_t* bytesRead)
    {
        std::size_t count = 0;
        char* compareStr = dest;

        char readChar = 0;

        while (--size > 0 && (count = file->read(file, &readChar, 1, 1)) != 0)
        {
            //if ((*compareStr++ = readChar) == '\n') break;
            (*bytesRead)++;
            if (readChar != '\r' && readChar != '\n') *compareStr++ = readChar;
            if (readChar == '\n') break;
        }
        *compareStr = '\0';
        return (count == 0 && compareStr == dest) ? nullptr : dest;
    }

    /*!
    \brief Returns the position of the nth instance of char c
    or std::string::npos if not found
    */
    static inline std::size_t findNthOf(const std::string& str, char c, std::size_t n)
    {
        if (n == 0)
        {
            return std::string::npos;
        }

        std::size_t currPos = 0;
        std::size_t currOffset = 0;
        std::size_t i = 0;

        while (i < n)
        {
            currPos = str.find(c, currOffset);
            if (currPos == std::string::npos)
            {
                break;
            }
            currOffset = currPos + 1;
            ++i;
        }
        return currPos;
    }

    /*!
    \brief Decode a UTF8 encoded string to a vector of std::uint32_t codepoints.
    Donated by therocode https://github.com/therocode
    */
    static inline std::vector<uint32_t> getCodepoints(const std::string& str)
    {
        std::vector<uint32_t> codePoints;

        for (uint32_t i = 0; i < str.size(); ++i)
        {
            if ((str[i] >> 7) == 0)
            {
                codePoints.push_back(uint32_t(str[i]));
            }
            else if (((uint8_t)str[i] >> 5) == 6)
            {
                // add "x"s to the unicode bits //
                uint8_t character = str[i + 1];
                // take away the "10" //
                uint8_t bitmask = 127; // 0x01111111
                uint32_t codePoint = uint32_t(character & bitmask);

                // add "y"s to the unicode bits //
                character = str[i];
                // take away the "110" //
                bitmask = 63; // 0x00111111
                uint32_t yValues = uint32_t(character & bitmask);
                codePoint = codePoint | (yValues << 6);

                codePoints.push_back(codePoint);
                ++i;
            }
            else if (((uint8_t)str[i] >> 4) == 14)
            {
                // add "x"s to the unicode bits //
                uint8_t character = str[i + 2];
                // take away the "10" //
                uint8_t bitmask = 127; // 0x01111111
                uint32_t codePoint = uint32_t(character & bitmask);

                // add "y"s to the unicode bits //
                character = str[i + 1];
                // take away the "10" //
                uint32_t yValues = uint32_t(character & bitmask);
                codePoint = codePoint | (yValues << 6);

                // add "z"s to the unicode bits //
                character = str[i];
                // take away the "1110" //
                bitmask = 31; // 0x00011111
                uint32_t zValues = uint32_t(character & bitmask);
                codePoint = codePoint | (zValues << 12);

                codePoints.push_back(codePoint);
                i += 2;
            }
            else if (((uint8_t)str[i] >> 3) == 30)
            {
                // add "x"s to the unicode bits //
                uint8_t character = str[i + 3];
                // take away the "10" //
                uint8_t bitmask = 127; // 0x01111111
                uint32_t codePoint = uint32_t(character & bitmask);

                // add "y"s to the unicode bits //
                character = str[i + 2];
                // take away the "10" //
                uint32_t yValues = uint32_t(character & bitmask);
                codePoint = codePoint | (yValues << 6);

                // add "z"s to the unicode bits //
                character = str[i + 1];
                // take away the "10" //
                uint32_t zValues = uint32_t(character & bitmask);
                codePoint = codePoint | (zValues << 12);

                // add "w"s to the unicode bits //
                character = str[i];
                // take away the "11110" //
                bitmask = 7; // 0x00001111
                uint32_t wValues = uint32_t(character & bitmask);
                codePoint = codePoint | (wValues << 18);

                codePoints.push_back(codePoint);
                i += 3;
            }
        }
        return codePoints;
    }

    /*!
    \brief Splits a string with a given token and returns a vector of results
    */
    static inline std::vector<std::string> tokenize(const std::string& str, char delim, bool keepEmpty = false)
    {
        CRO_ASSERT(!str.empty(), "string empty");
        std::stringstream ss(str);
        std::string token;
        std::vector<std::string> output;
        while (std::getline(ss, token, delim))
        {
            if (!token.empty() ||
                (token.empty() && keepEmpty))
            {
                output.push_back(token);
            }
        }
        return output;
    }

    /*!
    \brief Attempts to parse the given URL and open it in the
    system's current default editor/viewer. For example a website
    URL will open in a browser, or an image will open in a viewer.
    */
#ifdef _WIN32
    static inline void parseURL(const std::string& url)
    {
        ShellExecuteA(nullptr, "open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    }
#elif defined __APPLE__
    static inline void parseURL(const std::string& url)
    {
        std::string str = "open " + url;
        system(str.c_str());
    }
#else
    static inline void parseURL(const std::string& url)
    {
        std::string str = "xdg-open " + url;
        system(str.c_str());
    }
#endif
}