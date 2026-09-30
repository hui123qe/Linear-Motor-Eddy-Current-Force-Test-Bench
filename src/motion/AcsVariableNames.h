#pragma once

#include <array>

namespace AcsVariableNames {

inline constexpr std::array<const char*, 5> pressureValues = {
    "CurrentX1",
    "CurrentX2",
    "CurrentX3",
    "CurrentX4",
    "CurrentX5"
};

inline constexpr char forceValue[] = "CURRFORCE";
inline constexpr char homeDone[] = "HomeDone";
inline constexpr char homeRunning[] = "HomeRunning";
inline constexpr char positioningAcceleration[] = "G_N_ACC";
inline constexpr char positioningDeceleration[] = "G_N_DEC";
inline constexpr char positioningJerk[] = "G_N_JERK";

} // namespace AcsVariableNames
