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
    void forceSamplesReady(const QVector<QPointF>& samples);
    void collectionFailed(const QString& message);

private slots:
    void requestMetadata();
    void onConnectionChanged(bool connected, const QString& message);
    void onCollectionControlWritten(bool enabled);
    void onMetadataRead(const AcsCollectionMetadata& metadata);
    void onBlockRead(const AcquisitionBlock& block);
    void onBlockRejected(int blockIndex,
                         int expectedSequence,
                         const QString& message);
    void onCollectionCommandFailed(const QString& message);

private:
    DataAcquisitionService();
    ~DataAcquisitionService() override;

    DataAcquisitionService(const DataAcquisitionService&) = delete;
    DataAcquisitionService& operator=(const DataAcquisitionService&) = delete;

    void setState(AcquisitionState state);
    void failCollection(const QString& message);
    void requestCollectionControl(bool enabled);
    void requestBlock(int blockIndex, int expectedSequence);
    [[nodiscard]] bool selectNextBlock(const AcsCollectionMetadata& metadata,
                                       int* blockIndex,
                                       int* sequence,
                                       QString* errorMessage) const;
    void publishForceSamples(const AcquisitionBlock& block);

    AcsClient* client_ = nullptr;
    QTimer* pollTimer_ = nullptr;
    AcquisitionState state_ = AcquisitionState::Idle;
    bool initialized_ = false;
    bool connected_ = false;
    int lastConsumedSequence_ = 0;
    double sessionStartSeconds_ = 0.0;
    qint64 sessionSampleOffset_ = 0;
    QElapsedTimer stoppingTimer_;
};
