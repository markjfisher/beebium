#include "GrpcKeyboardClient.hpp"

#include <grpcpp/client_context.h>
#include <grpcpp/create_channel.h>
#include <grpcpp/security/credentials.h>

GrpcKeyboardClient::GrpcKeyboardClient(QObject *parent)
    : QObject(parent) {
}

void GrpcKeyboardClient::connectToTarget(const ConnectionTarget &target) {
    clearMappings();
    channel_ = grpc::CreateChannel(target.address().toStdString(), grpc::InsecureChannelCredentials());
    stub_ = beebium::KeyboardService::NewStub(channel_);
}

void GrpcKeyboardClient::disconnectFromTarget() {
    clearMappings();
    stub_.reset();
    channel_.reset();
}

bool GrpcKeyboardClient::keyDown(quint32 ikNumber) {
    if (!stub_) {
        return false;
    }

    grpc::ClientContext context;
    beebium::KeyRequest request;
    beebium::KeyResponse response;
    request.set_ik_number(ikNumber);
    const grpc::Status status = stub_->KeyDown(&context, request, &response);
    if (!status.ok()) {
        emit errorOccurred(tr("KeyDown failed: %1").arg(QString::fromStdString(status.error_message())));
        return false;
    }
    return response.accepted();
}

bool GrpcKeyboardClient::keyUp(quint32 ikNumber) {
    if (!stub_) {
        return false;
    }

    grpc::ClientContext context;
    beebium::KeyRequest request;
    beebium::KeyResponse response;
    request.set_ik_number(ikNumber);
    const grpc::Status status = stub_->KeyUp(&context, request, &response);
    if (!status.ok()) {
        emit errorOccurred(tr("KeyUp failed: %1").arg(QString::fromStdString(status.error_message())));
        return false;
    }
    return response.accepted();
}

bool GrpcKeyboardClient::breakDown() {
    if (!stub_) {
        return false;
    }

    grpc::ClientContext context;
    beebium::BreakDownRequest request;
    beebium::BreakDownResponse response;
    const grpc::Status status = stub_->BreakDown(&context, request, &response);
    if (!status.ok()) {
        emit errorOccurred(tr("BreakDown failed: %1").arg(QString::fromStdString(status.error_message())));
        return false;
    }
    return response.success();
}

bool GrpcKeyboardClient::breakUp() {
    if (!stub_) {
        return false;
    }

    grpc::ClientContext context;
    beebium::BreakUpRequest request;
    beebium::BreakUpResponse response;
    const grpc::Status status = stub_->BreakUp(&context, request, &response);
    if (!status.ok()) {
        emit errorOccurred(tr("BreakUp failed: %1").arg(QString::fromStdString(status.error_message())));
        return false;
    }
    return response.success();
}

bool GrpcKeyboardClient::typeText(const QString &text) {
    if (!stub_ || text.isEmpty()) {
        return false;
    }

    grpc::ClientContext context;
    beebium::TypeQuicklyRequest request;
    beebium::TypeQuicklyResponse response;
    request.set_text(text.toStdString());
    const grpc::Status status = stub_->TypeQuickly(&context, request, &response);
    if (!status.ok()) {
        emit errorOccurred(tr("TypeQuickly failed: %1").arg(QString::fromStdString(status.error_message())));
        return false;
    }
    if (!response.accepted()) {
        emit errorOccurred(tr("TypeQuickly rejected: %1").arg(QString::fromStdString(response.error())));
        return false;
    }
    return true;
}

std::optional<GrpcKeyboardClient::CharacterMapping> GrpcKeyboardClient::getCharacterMapping(const QString &text) {
    if (!stub_ || text.isEmpty()) {
        return std::nullopt;
    }

    const QString key = text.left(1);
    const auto cached = characterMappings_.find(key);
    if (cached != characterMappings_.end()) {
        return cached.value();
    }

    grpc::ClientContext context;
    beebium::GetKeyMappingRequest request;
    beebium::KeyMappingEntry response;
    request.set_character(key.toStdString());
    const grpc::Status status = stub_->GetKeyMapping(&context, request, &response);
    if (!status.ok()) {
        emit errorOccurred(tr("GetKeyMapping failed: %1").arg(QString::fromStdString(status.error_message())));
        return std::nullopt;
    }
    if (!response.found()) {
        return std::nullopt;
    }

    CharacterMapping mapping;
    mapping.ikNumber = response.ik_number();
    mapping.needsShift = response.needs_shift();
    mapping.name = QString::fromStdString(response.name());
    characterMappings_.insert(key, mapping);
    return mapping;
}

void GrpcKeyboardClient::clearMappings() {
    characterMappings_.clear();
}
