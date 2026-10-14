#pragma once

#include "AcquisitionTypes.h"

#include <QElapsedTimer>
#include <QObject>
#include <QPointF>
#include <QVector>

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
    void forcePositionSamplesReady(const QVector<QPointF>& samples);
    void collectionFailed(const QString& message);

private slots:
    void requestMetadata();
    void onConnectionChanged(bool connected, const QString& message);
    void onCollectionControlWritten(quint64 generation, bool enabled);
    void onMetadataRead(quint64 generation, const AcsCollectionMetadata& metadata);
    void onBlockRead(quint64 generation, const AcquisitionBlock& block);
    void onBlockRejected(quint64 generation, int blockIndex,
                         int expectedSequence,
                         const QString& message);
    void onCollectionCommandFailed(quint64 generation, const QString& message);

private:
    DataAcquisitionService();
    ~DataAcquisitionService() override;

    DataAcquisitionService(const DataAcquisitionService&) = delete;
    DataAcquisitionService& operator=(const DataAcquisitionService&) = delete;

    void setState(AcquisitionState state);
    void invalidateRequests();
    void finishCollection();
    void failCollection(const QString& message);
    void requestCollectionControl(bool enabled);
    void requestBlock(int blockIndex, int expectedSequence);
    [[nodiscard]] bool selectNextBlock(const AcsCollectionMetadata& metadata,
                                       int* blockIndex,
                                       int* sequence,
                                       QString* errorMessage) const;
    void publishForcePositionSamples(const AcquisitionBlock& block);

    AcsClient* client_ = nullptr;
    QTimer* pollTimer_ = nullptr;
    AcquisitionState state_ = AcquisitionState::Idle;
    bool initialized_ = false;
    bool connected_ = false;
    int lastConsumedSequence_ = 0;
    quint64 generation_ = 0;
    int controllerSession_ = 0;
    bool metadataInFlight_ = false;
    bool blockInFlight_ = false;
    bool controlInFlight_ = false;
    bool startCommandSent_ = false;
    bool sessionConfirmed_ = false;
    bool stopCommandSent_ = false;
    QElapsedTimer startingTimer_;
    QElapsedTimer stoppingTimer_;
};
