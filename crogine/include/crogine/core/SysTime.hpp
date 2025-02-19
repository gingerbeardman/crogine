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

#include <crogine/Config.hpp>

#include <string>

namespace cro
{
    /*!
    \brief Static functions for common date/time queries
    using the system clock. These are not thread safe so
    appropriate care should be taken when accessing the 
    time from multiple threads.
    */
    class CRO_EXPORT_API SysTime final
    {
    public:
        /*!
        \brief Struct containing data/time information
        */
        struct CRO_EXPORT_API Data final
        {
            Data();

            int seconds() const; //!< Seconds after the minute
            int minutes() const; //!< Minutes after the hour
            int hours() const; //!< Hours after midnight
            int days() const; //!< Day of the month
            int months() const; //!< Months of the year
            int year() const; //!< Current year

        private:
            mutable tm* m_time = nullptr;
            void update() const;
        };

        /*!
        \brief Returns a reference to a struct containing
        the current system date and time information
        */
        static const Data& now();

        /*!
        \brief Returns the time since the epoch, in seconds
        */
        static std::uint64_t epoch();

        /*!
        \brief Returns the current date as a dd/mm/yyyy
        formatted string
        */
        static std::string dateString();

        /*!
        \brief Returns the given time since epoch formatted
        as a string in local time
        */
        static std::string dateString(std::uint64_t epoch);

        /*!
        \brief Returns the current system time as a HH:MM:ss
        formatted string
        */
        static std::string timeString();

        /*!
        \brief Returns the given time since epoch as HH:MM::ss
        formatted string
        */
        static std::string timeString(std::uint64_t epoch);
    private:
        
    };
}
