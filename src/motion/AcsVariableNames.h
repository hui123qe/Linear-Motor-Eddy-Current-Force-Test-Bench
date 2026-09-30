#pragma once

#include <array>

namespace AcsVariableNames {

inline constexpr std::array<const char*, 5> pressureValues = {
    "Down_FLowA",
    "Side2_FLowA",
    "Side1_FLowA",
    "Up2_FLowA",
    "Up1_FLowA"
};

inline constexpr char forceValue[] = "Froce_Sensor";
inline constexpr char homeDone[] = "HomeDone";
inline constexpr char homeRunning[] = "HomeRunning";
inline constexpr char positioningAcceleration[] = "G_N_ACC";
inline constexpr char positioningDeceleration[] = "G_N_DEC";
inline constexpr char positioningJerk[] = "G_N_JERK";

} // namespace AcsVariableNames
