#pragma once

#include "AcquisitionTypes.h"

#include <QElapsedTimer>
#include <QObject>

class AcsClient;
class QTimer;

class DataAcquisitionService final : public QObject
{
    Q_OBJECT

public:
    static DataAcquisitionService& instance();

    void initialize(AcsClient* client);
    void shutdown();

    [[nodiscard]] bool startCollection(QString* errorMessage = nullptr);
    [[nodiscard]] bool stopCollection(QString* errorMessage = nullptr);
    [[nodiscard]] AcquisitionState state() const;
    [[nodiscard]] bool isReady() const;

signals:
    void readinessChanged(bool ready, const QString& message);
    void collectionStateChanged(AcquisitionState state);
    void collectionStarted();
    void collectionStopped();
    void blockReady(const AcquisitionBlock& block);
    void collectionFailed(const QString& message);

private slots:
    void requestMetadata();
    void onConnectionChanged(bool connected, const QString& message);
    void onControllerResetCompleted();
    void onCollectionControlWritten(quint64 generation, bool enabled);
    void onMetadataRead(quint64 generation, const AcsCollectionMetadata& metadata);
    void onBlockRead(quint64 generation, const AcquisitionBlock& block);
    void onBlockRejected(quint64 generation, int blockIndex,
                         int expectedSequence,
                         const QString& message);
    void onCollectionCommandFailed(quint64 generation, const QString& message);

private:
    enum class Phase {
        Idle,
        CheckingIdle,
        Enabling,
        AwaitingSession,
        Collecting,
        StoppingAwaitingSession,
        Disabling,
        Draining,
        Fault
    };

    enum class PendingRead { None, Metadata, Block };
    enum class PendingControl { None, Enable, Disable };

    DataAcquisitionService();
    ~DataAcquisitionService() override;

    DataAcquisitionService(const DataAcquisitionService&) = delete;
    DataAcquisitionService& operator=(const DataAcquisitionService&) = delete;

    void setPhase(Phase phase);
    [[nodiscard]] static const char* phaseName(Phase phase);
    void checkIdleBeforeStart(const AcsCollectionMetadata& metadata);
    void confirmSession(const AcsCollectionMetadata& metadata);
    void consumePublishedBlocks(const AcsCollectionMetadata& metadata);
    void invalidateRequests();
    void finishCollection();
    void failCollection(const QString& message);
    void requestCollectionControl(bool enabled);
    void requestBlock(int blockIndex, int expectedSequence);
    [[nodiscard]] bool selectNextBlock(const AcsCollectionMetadata& metadata,
                                       int* blockIndex,
                                       int* sequence,
                                       QString* errorMessage) const;

    AcsClient* client_ = nullptr;
    QTimer* pollTimer_ = nullptr;
    Phase phase_ = Phase::Idle;
    bool initialized_ = false;
    bool connected_ = false;
    int lastConsumedSequence_ = 0;
    quint64 generation_ = 0;
    int controllerSession_ = 0;
    PendingRead pendingRead_ = PendingRead::None;
    PendingControl pendingControl_ = PendingControl::None;
    QElapsedTimer startingTimer_;
    QElapsedTimer stoppingTimer_;
};
