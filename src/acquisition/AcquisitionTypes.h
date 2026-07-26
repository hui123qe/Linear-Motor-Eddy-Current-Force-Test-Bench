#pragma once

#include <QMetaType>
#include <QVector>

#include <array>

inline constexpr int kAcquisitionBlockCount = 5;
inline constexpr int kAcquisitionChannelCount = 6;
inline constexpr int kAcquisitionBlockCapacity = 16666;
inline constexpr double kAcquisitionSamplePeriodSeconds = 0.001;

enum class AcquisitionState
{
    Idle,
    Starting,
    Collecting,
    Stopping,
    Fault
};

struct AcsCollectionMetadata
{
    int controlEnabled = 0;
    int activeBlock = 0;
    int finishedBlock = 0;
    int publishedSequence = 0;
    int finishedCount = 0;
    bool finishedPartial = false;
    std::array<int, kAcquisitionBlockCount> validCounts{};
    std::array<int, kAcquisitionBlockCount> partialFlags{};
    std::array<int, kAcquisitionBlockCount> blockSequences{};
};

struct AcquisitionBlock
{
    int sequence = 0;
    int blockIndex = 0;
    int sampleCount = 0;
    bool partial = false;
    double samplePeriodSeconds = kAcquisitionSamplePeriodSeconds;
    QVector<double> accelerationMetersPerSecondSquared;
    QVector<double> velocityMetersPerSecond;
    QVector<double> motorCurrent;
    QVector<double> motorTemperature;
    QVector<double> forceNewtons;
    QVector<double> positionMeters;
};

Q_DECLARE_METATYPE(AcquisitionState)
Q_DECLARE_METATYPE(AcsCollectionMetadata)
Q_DECLARE_METATYPE(AcquisitionBlock)
