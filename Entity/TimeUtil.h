#pragma once
#include <chrono>
#include <cstdio>
#include <ctime>
#include <stdexcept>
#include <string>

// ---
// це повністю аішка - бо переводити з дати в рядки.. щось страшне
// ---

using TimePoint = std::chrono::system_clock::time_point;
inline TimePoint now() { return std::chrono::system_clock::now(); }

// time_point -> рядок для запису в БД
inline std::string toIso(TimePoint tp) {
    std::time_t t = std::chrono::system_clock::to_time_t(tp);
    std::tm tm{};
#ifdef _WIN32
    gmtime_s(&tm, &t);
#else
    gmtime_r(&t, &tm);
#endif
    char buf[21];
    std::strftime(buf, sizeof buf, "%Y-%m-%dT%H:%M:%SZ", &tm);
    return buf;
}

// рядок з БД -> time_point
inline TimePoint fromIso(const std::string& s) {
    std::tm tm{};
    if (std::sscanf(s.c_str(), "%4d-%2d-%2dT%2d:%2d:%2dZ",
                    &tm.tm_year, &tm.tm_mon, &tm.tm_mday,
                    &tm.tm_hour, &tm.tm_min, &tm.tm_sec) != 6)
        throw std::invalid_argument("Bad ISO timestamp: " + s);
    tm.tm_year -= 1900;
    tm.tm_mon -= 1;
#ifdef _WIN32
    std::time_t t = _mkgmtime(&tm);
#else
    std::time_t t = timegm(&tm);
#endif
    return std::chrono::system_clock::from_time_t(t);
}