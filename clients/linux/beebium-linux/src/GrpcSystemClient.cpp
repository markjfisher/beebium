#include "GrpcSystemClient.hpp"

#include <grpcpp/create_channel.h>
#include <grpcpp/security/credentials.h>

#include "system.grpc.pb.h"

GrpcSystemClient::GrpcSystemClient(QObject *parent)
    : QObject(parent) {
}

void GrpcSystemClient::fetchSystemInfo(const ConnectionTarget &target) {
    auto channel = grpc::CreateChannel(target.address().toStdString(), grpc::InsecureChannelCredentials());
    auto stub = beebium::SystemService::NewStub(channel);

    grpc::ClientContext context;
    beebium::GetSystemInfoRequest request;
    beebium::SystemInfo response;
    const grpc::Status status = stub->GetSystemInfo(&context, request, &response);
    if (!status.ok()) {
        emit errorOccurred(tr("GetSystemInfo failed: %1").arg(QString::fromStdString(status.error_message())));
        return;
    }

    QString modelName = tr("Unknown machine");
    if (response.has_identity() && !response.identity().model_name().empty()) {
        modelName = QString::fromStdString(response.identity().model_name());
    }
    emit machineSummaryChanged(modelName);
}
