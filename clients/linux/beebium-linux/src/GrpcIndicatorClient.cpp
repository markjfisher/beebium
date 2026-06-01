#include "GrpcIndicatorClient.hpp"

#include <algorithm>

#include <grpcpp/client_context.h>
#include <grpcpp/create_channel.h>
#include <grpcpp/security/credentials.h>

GrpcIndicatorClient::GrpcIndicatorClient(QObject *parent)
    : QObject(parent) {
    pollTimer_.setInterval(250);
    connect(&pollTimer_, &QTimer::timeout, this, &GrpcIndicatorClient::refresh);
}

void GrpcIndicatorClient::connectToTarget(const ConnectionTarget &target) {
    disconnectFromTarget();
    target_ = target;
    channel_ = grpc::CreateChannel(target.address().toStdString(), grpc::InsecureChannelCredentials());
    stub_ = beebium::IndicatorService::NewStub(channel_);
    fetchMetadata();
    refresh();
    pollTimer_.start();
}

void GrpcIndicatorClient::disconnectFromTarget() {
    pollTimer_.stop();
    indicators_.clear();
    sequence_ = 0;
    metadataLoaded_ = false;
    stub_.reset();
    channel_.reset();
    emit indicatorsChanged();
}

const QVector<IndicatorInfo> &GrpcIndicatorClient::indicators() const {
    return indicators_;
}

void GrpcIndicatorClient::fetchMetadata() {
    if (!stub_) {
        return;
    }

    grpc::ClientContext context;
    beebium::ListIndicatorsRequest request;
    beebium::ListIndicatorsResponse response;
    const grpc::Status status = stub_->ListIndicators(&context, request, &response);
    if (!status.ok()) {
        emit errorOccurred(tr("ListIndicators failed: %1").arg(QString::fromStdString(status.error_message())));
        return;
    }

    indicators_.clear();
    indicators_.reserve(response.indicators_size());
    for (const auto &indicator : response.indicators()) {
        IndicatorInfo info;
        info.name = QString::fromStdString(indicator.name());
        const auto &metadata = indicator.metadata();
        const auto labelIt = metadata.find("label");
        info.label = labelIt != metadata.end() ? QString::fromStdString(labelIt->second) : info.name;
        const auto colorIt = metadata.find("color");
        info.color = colorIt != metadata.end() ? QString::fromStdString(colorIt->second) : QString();
        const auto keyIt = metadata.find("related_key");
        info.relatedKey = keyIt != metadata.end() ? QString::fromStdString(keyIt->second) : QString();
        indicators_.push_back(info);
    }
    metadataLoaded_ = true;
}

void GrpcIndicatorClient::refresh() {
    if (!stub_) {
        return;
    }
    if (!metadataLoaded_) {
        fetchMetadata();
    }

    grpc::ClientContext context;
    beebium::GetIndicatorsRequest request;
    request.set_if_changed_since(sequence_);
    beebium::GetIndicatorsResponse response;
    const grpc::Status status = stub_->GetIndicators(&context, request, &response);
    if (!status.ok()) {
        emit errorOccurred(tr("GetIndicators failed: %1").arg(QString::fromStdString(status.error_message())));
        return;
    }

    if (!response.changed()) {
        return;
    }

    sequence_ = response.sequence();
    for (auto &indicator : indicators_) {
        const auto it = response.values().find(indicator.name.toStdString());
        if (it != response.values().end()) {
            indicator.value = it->second;
        }
    }
    std::sort(indicators_.begin(), indicators_.end(), [](const IndicatorInfo &lhs, const IndicatorInfo &rhs) {
        return lhs.label < rhs.label;
    });
    emit indicatorsChanged();
}
