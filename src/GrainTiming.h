#pragma once
#include <algorithm>
#include <array>
#include <string>

namespace rice
{
struct Division { std::string name; double beats; };

inline const std::array<Division, 30>& divisions()
{
    static const auto values = []
    {
        const std::array<const char*, 10> names { "1/32", "1/16", "1/8", "1/6", "1/4", "1/2", "1", "2", "3", "4" };
        const std::array<double, 10> beats { 0.125, 0.25, 0.5, 4.0 / 6.0, 1.0, 2.0, 4.0, 8.0, 12.0, 16.0 };
        std::array<Division, 30> result {};
        for (size_t i = 0; i < names.size(); ++i)
        {
            result[i * 3] = { std::string (names[i]) + " T", beats[i] * 2.0 / 3.0 };
            result[i * 3 + 1] = { names[i], beats[i] };
            result[i * 3 + 2] = { std::string (names[i]) + " D", beats[i] * 1.5 };
        }
        std::stable_sort (result.begin(), result.end(), [] (const auto& a, const auto& b) { return a.beats < b.beats; });
        return result;
    }();
    return values;
}

inline int divisionIndex (const std::string& name)
{
    const auto& values = divisions();
    for (size_t i = 0; i < values.size(); ++i)
        if (values[i].name == name)
            return static_cast<int> (i);
    return 0;
}

inline double millisecondsForDivision (int index, double bpm)
{
    return divisions()[static_cast<size_t> (std::clamp (index, 0, 29))].beats * 60000.0 / std::clamp (bpm, 20.0, 999.0);
}
}
