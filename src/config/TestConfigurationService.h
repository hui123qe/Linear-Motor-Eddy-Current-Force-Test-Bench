#pragma once

#include "TestParameters.h"

#include <optional>

class TestConfigurationService final
{
public:
    explicit TestConfigurationService(TestParametersStore store = TestParametersStore{});

    [[nodiscard]] bool loadSavedConfiguration(QString* errorMessage = nullptr);
    [[nodiscard]] bool saveConfiguration(const TestParameters& parameters,
                                         QString* errorMessage = nullptr);
    [[nodiscard]] bool lockConfiguration(const TestParameters& parameters,
                                         QString* errorMessage = nullptr);
    void unlockConfiguration();

    [[nodiscard]] bool isLocked() const;
    [[nodiscard]] QString configurationFilePath() const;
    [[nodiscard]] std::optional<TestParameters> savedConfiguration() const;
    [[nodiscard]] std::optional<TestParameters> lockedConfiguration() const;
    [[nodiscard]] std::optional<TestParameters> displayConfiguration() const;

private:
    TestParametersStore store_;
    std::optional<TestParameters> savedConfiguration_;
    std::optional<TestParameters> lockedConfiguration_;
};
