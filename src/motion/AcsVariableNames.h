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

inline constexpr char forceValue[] = "EC_AIN9";
inline constexpr std::array<const char*, 4> safetyLightValues = {
    "Safetylight1",
    "Safetylight2",
    "Safetylight3",
    "Safetylight4"
};
inline constexpr char safetyDoorLock[] = "SafetyDoorLock";inline constexpr char homeDone[] = "HomeDone";
inline constexpr char homeRunning[] = "HomeRunning";
inline constexpr char positioningAcceleration[] = "G_N_ACC";
inline constexpr char positioningDeceleration[] = "G_N_DEC";
inline constexpr char positioningJerk[] = "G_N_JERK";

} // namespace AcsVariableNames
