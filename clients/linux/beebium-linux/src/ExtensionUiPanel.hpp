#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <thread>

#include <QObject>
#include <QVector>
#include <QWidget>

#include <grpcpp/channel.h>
#include <grpcpp/client_context.h>

#include "ConnectionTarget.hpp"
#include "extension_ui.grpc.pb.h"

class QVBoxLayout;

class ExtensionUiPanel final : public QWidget {
    Q_OBJECT

public:
    ExtensionUiPanel(QString extensionId, QString label, QWidget *parent = nullptr);
    ~ExtensionUiPanel() override;

    [[nodiscard]] const QString &extensionId() const;
    [[nodiscard]] const QString &label() const;

    void connectToTarget(const ConnectionTarget &target);
    void disconnectFromTarget();

signals:
    void errorOccurred(const QString &message);

private:
    enum class RenderMode {
        LiveDispatch,
        ReadOnly,
        LocalEditor,
    };

    struct EditorFieldBinding {
        enum class Kind {
            Bool,
            String,
            Index,
        };

        QString fieldId;
        Kind kind = Kind::String;
        QObject *object = nullptr;
    };

    void streamLoop(ConnectionTarget target);
    void applyView(beebium::View view);
    void rebuildUi();
    QWidget *buildControlWidget(const beebium::Control &control, RenderMode mode, QVector<EditorFieldBinding> *editorBindings = nullptr);
    QWidget *buildGroupWidget(const beebium::Control &control, RenderMode mode, QVector<EditorFieldBinding> *editorBindings = nullptr);
    QWidget *buildLabelWidget(const beebium::Control &control);
    QWidget *buildIndicatorWidget(const beebium::Control &control);
    QWidget *buildToggleWidget(const beebium::Control &control, RenderMode mode, QVector<EditorFieldBinding> *editorBindings = nullptr);
    QWidget *buildButtonWidget(const beebium::Control &control, RenderMode mode);
    QWidget *buildChoiceWidget(const beebium::Control &control, RenderMode mode, QVector<EditorFieldBinding> *editorBindings = nullptr);
    QWidget *buildTextInputWidget(const beebium::Control &control, RenderMode mode, QVector<EditorFieldBinding> *editorBindings = nullptr);
    QWidget *buildEditableChoiceWidget(const beebium::Control &control, RenderMode mode, QVector<EditorFieldBinding> *editorBindings = nullptr);
    QWidget *buildModalEditorWidget(const beebium::Control &control);
    bool dispatchControl(const QString &controlId, const std::function<void(beebium::DispatchRequest &)> &payloadWriter);
    bool showModalEditor(const beebium::Control &control);
    static QString sanitizeText(const std::string &text);

    QString extensionId_;
    QString label_;
    std::atomic_bool running_{false};
    std::thread worker_;
    std::shared_ptr<grpc::Channel> channel_;
    std::unique_ptr<beebium::ExtensionUiService::Stub> stub_;
    std::unique_ptr<grpc::ClientContext> streamContext_;
    QVBoxLayout *layout_ = nullptr;
    beebium::View currentView_;
};
