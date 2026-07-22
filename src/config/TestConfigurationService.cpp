#include "TestConfigurationService.h"

#include <QFileInfo>

#include <utility>

TestConfigurationService::TestConfigurationService(TestParametersStore store)
    : store_(std::move(store))
{
}

bool TestConfigurationService::loadSavedConfiguration(QString* errorMessage)
{
    savedConfiguration_.reset();

    const QString filePath = store_.filePath();
    if (!filePath.isEmpty() && !QFileInfo::exists(filePath)) {
        return true;
    }

    TestParameters parameters;
    if (!store_.load(&parameters, errorMessage)) {
        return false;
    }

    savedConfiguration_ = parameters;
    return true;
}

bool TestConfigurationService::saveConfiguration(const TestParameters& parameters,
                                                 QString* errorMessage)
{
    if (!store_.save(parameters, errorMessage)) {
        return false;
    }

    savedConfiguration_ = parameters;
    return true;
}

bool TestConfigurationService::lockConfiguration(const TestParameters& parameters,
                                                 QString* errorMessage)
{
    if (!validateTestParameters(parameters, errorMessage)) {
        return false;
    }

    lockedConfiguration_ = parameters;
    return true;
}

void TestConfigurationService::unlockConfiguration()
{
    lockedConfiguration_.reset();
}

bool TestConfigurationService::isLocked() const
{
    return lockedConfiguration_.has_value();
}

QString TestConfigurationService::configurationFilePath() const
{
    return store_.filePath();
}

std::optional<TestParameters> TestConfigurationService::savedConfiguration() const
{
    return savedConfiguration_;
}

std::optional<TestParameters> TestConfigurationService::lockedConfiguration() const
{
    return lockedConfiguration_;
}

std::optional<TestParameters> TestConfigurationService::displayConfiguration() const
{
    if (lockedConfiguration_.has_value()) {
        return lockedConfiguration_;
    }
    return savedConfiguration_;
}
