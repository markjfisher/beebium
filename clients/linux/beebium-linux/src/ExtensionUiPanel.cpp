#include "ExtensionUiPanel.hpp"

#include <functional>

#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFrame>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

#include <grpcpp/create_channel.h>
#include <grpcpp/security/credentials.h>

namespace {

QString indicatorColor(beebium::Indicator_State state) {
    switch (state) {
    case beebium::Indicator_State_OK:
        return QStringLiteral("#2e7d32");
    case beebium::Indicator_State_WARN:
        return QStringLiteral("#ed6c02");
    case beebium::Indicator_State_ERROR:
        return QStringLiteral("#d32f2f");
    default:
        return QStringLiteral("#6b7280");
    }
}

QString commitButtonText(beebium::ModalEditor_CommitRole role) {
    return role == beebium::ModalEditor_CommitRole_ADD ? QObject::tr("Add") : QObject::tr("Save");
}

} // namespace

ExtensionUiPanel::ExtensionUiPanel(QString extensionId, QString label, QWidget *parent)
    : QWidget(parent)
    , extensionId_(std::move(extensionId))
    , label_(std::move(label))
    , layout_(new QVBoxLayout()) {
    layout_->setContentsMargins(0, 0, 0, 0);
    layout_->setSpacing(8);
    setLayout(layout_);
}

ExtensionUiPanel::~ExtensionUiPanel() {
    disconnectFromTarget();
}

const QString &ExtensionUiPanel::extensionId() const {
    return extensionId_;
}

const QString &ExtensionUiPanel::label() const {
    return label_;
}

void ExtensionUiPanel::connectToTarget(const ConnectionTarget &target) {
    disconnectFromTarget();
    running_.store(true);
    channel_ = grpc::CreateChannel(target.address().toStdString(), grpc::InsecureChannelCredentials());
    stub_ = beebium::ExtensionUiService::NewStub(channel_);
    worker_ = std::thread([this, target] { streamLoop(target); });
}

void ExtensionUiPanel::disconnectFromTarget() {
    running_.store(false);
    if (streamContext_) {
        streamContext_->TryCancel();
    }
    if (worker_.joinable()) {
        worker_.join();
    }
    streamContext_.reset();
    stub_.reset();
    channel_.reset();
    currentView_.Clear();
    rebuildUi();
}

void ExtensionUiPanel::streamLoop(ConnectionTarget) {
    streamContext_ = std::make_unique<grpc::ClientContext>();
    beebium::SubscribeViewRequest request;
    request.set_extension_id(extensionId_.toStdString());
    std::unique_ptr<grpc::ClientReader<beebium::View>> reader(
        stub_->SubscribeView(streamContext_.get(), request));

    beebium::View view;
    while (running_.load() && reader->Read(&view)) {
        QMetaObject::invokeMethod(this, [this, view]() mutable {
            applyView(std::move(view));
        }, Qt::QueuedConnection);
    }

    const grpc::Status status = reader->Finish();
    if (running_.load() && !status.ok()) {
        QMetaObject::invokeMethod(this, [this, message = QString::fromStdString(status.error_message())] {
            emit errorOccurred(tr("Extension UI stream for '%1' failed: %2").arg(label_, message));
        }, Qt::QueuedConnection);
    }
}

void ExtensionUiPanel::applyView(beebium::View view) {
    currentView_ = std::move(view);
    rebuildUi();
}

void ExtensionUiPanel::rebuildUi() {
    while (QLayoutItem *item = layout_->takeAt(0)) {
        if (QWidget *widget = item->widget()) {
            widget->deleteLater();
        }
        delete item;
    }

    if (!currentView_.has_root()) {
        auto *placeholder = new QLabel(tr("No extension UI available."), this);
        placeholder->setWordWrap(true);
        layout_->addWidget(placeholder);
        layout_->addStretch(1);
        return;
    }

    layout_->addWidget(buildControlWidget(currentView_.root(), RenderMode::LiveDispatch));
    layout_->addStretch(1);
}

QWidget *ExtensionUiPanel::buildControlWidget(const beebium::Control &control, RenderMode mode, QVector<EditorFieldBinding> *editorBindings) {
    QWidget *widget = nullptr;
    switch (control.control_case()) {
    case beebium::Control::kLabel:
        widget = buildLabelWidget(control);
        break;
    case beebium::Control::kIndicator:
        widget = buildIndicatorWidget(control);
        break;
    case beebium::Control::kToggle:
        widget = buildToggleWidget(control, mode, editorBindings);
        break;
    case beebium::Control::kButton:
        widget = buildButtonWidget(control, mode);
        break;
    case beebium::Control::kChoice:
        widget = buildChoiceWidget(control, mode, editorBindings);
        break;
    case beebium::Control::kTextInput:
        widget = buildTextInputWidget(control, mode, editorBindings);
        break;
    case beebium::Control::kEditableChoice:
        widget = buildEditableChoiceWidget(control, mode, editorBindings);
        break;
    case beebium::Control::kGroup:
        widget = buildGroupWidget(control, mode, editorBindings);
        break;
    case beebium::Control::kModalEditor:
        widget = mode == RenderMode::LiveDispatch ? buildModalEditorWidget(control) : buildControlWidget(control.modal_editor().anchor(), RenderMode::ReadOnly, editorBindings);
        break;
    case beebium::Control::CONTROL_NOT_SET:
    default:
        widget = new QLabel(tr("Unsupported control"), this);
        break;
    }
    if (!control.tooltip().empty() && widget) {
        widget->setToolTip(QString::fromStdString(control.tooltip()));
    }
    return widget;
}

QWidget *ExtensionUiPanel::buildGroupWidget(const beebium::Control &control, RenderMode mode, QVector<EditorFieldBinding> *editorBindings) {
    QWidget *container = nullptr;
    QVBoxLayout *groupLayout = nullptr;
    if (control.group().has_label()) {
        auto *box = new QGroupBox(QString::fromStdString(control.group().label()), this);
        groupLayout = new QVBoxLayout(box);
        container = box;
    } else {
        auto *widget = new QWidget(this);
        groupLayout = new QVBoxLayout(widget);
        container = widget;
    }
    groupLayout->setContentsMargins(0, 0, 0, 0);
    groupLayout->setSpacing(8);
    for (const auto &child : control.group().controls()) {
        groupLayout->addWidget(buildControlWidget(child, mode, editorBindings));
    }
    return container;
}

QWidget *ExtensionUiPanel::buildLabelWidget(const beebium::Control &control) {
    auto *widget = new QWidget(this);
    auto *row = new QHBoxLayout(widget);
    row->setContentsMargins(0, 0, 0, 0);
    auto *primary = new QLabel(sanitizeText(control.label().text()), widget);
    primary->setWordWrap(true);
    row->addWidget(primary, 1);
    if (!control.label().secondary_text().empty()) {
        auto *secondary = new QLabel(sanitizeText(control.label().secondary_text()), widget);
        secondary->setStyleSheet(QStringLiteral("color: palette(mid);") );
        row->addWidget(secondary, 0, Qt::AlignRight);
    }
    return widget;
}

QWidget *ExtensionUiPanel::buildIndicatorWidget(const beebium::Control &control) {
    auto *widget = new QWidget(this);
    auto *row = new QHBoxLayout(widget);
    row->setContentsMargins(0, 0, 0, 0);
    auto *dot = new QFrame(widget);
    dot->setFixedSize(10, 10);
    dot->setStyleSheet(QStringLiteral("border-radius: 5px; background: %1;").arg(indicatorColor(control.indicator().state())));
    row->addWidget(dot, 0, Qt::AlignTop);
    auto *label = new QLabel(sanitizeText(control.indicator().text()), widget);
    label->setWordWrap(true);
    row->addWidget(label, 1);
    return widget;
}

QWidget *ExtensionUiPanel::buildToggleWidget(const beebium::Control &control, RenderMode mode, QVector<EditorFieldBinding> *editorBindings) {
    auto *checkBox = new QCheckBox(sanitizeText(control.toggle().label()), this);
    checkBox->setChecked(control.toggle().value());
    if (!control.tooltip().empty()) {
        checkBox->setToolTip(QString::fromStdString(control.tooltip()));
    }
    if (mode == RenderMode::LiveDispatch) {
        connect(checkBox, &QCheckBox::toggled, this, [this, control](bool checked) {
            dispatchControl(QString::fromStdString(control.id()), [checked](beebium::DispatchRequest &request) {
                request.set_bool_value(checked);
            });
        });
    } else if (mode == RenderMode::LocalEditor && editorBindings) {
        editorBindings->push_back({QString::fromStdString(control.id()), EditorFieldBinding::Kind::Bool, checkBox});
    } else {
        checkBox->setEnabled(false);
    }
    return checkBox;
}

QWidget *ExtensionUiPanel::buildButtonWidget(const beebium::Control &control, RenderMode mode) {
    auto *button = new QPushButton(sanitizeText(control.button().label()), this);
    button->setEnabled(mode == RenderMode::LiveDispatch && control.button().enabled());
    if (mode == RenderMode::LiveDispatch) {
        connect(button, &QPushButton::clicked, this, [this, control]() {
            dispatchControl(QString::fromStdString(control.id()), [](beebium::DispatchRequest &) {});
        });
    }
    return button;
}

QWidget *ExtensionUiPanel::buildChoiceWidget(const beebium::Control &control, RenderMode mode, QVector<EditorFieldBinding> *editorBindings) {
    auto *widget = new QWidget(this);
    auto *row = new QHBoxLayout(widget);
    row->setContentsMargins(0, 0, 0, 0);
    auto *label = new QLabel(sanitizeText(control.choice().label()), widget);
    auto *combo = new QComboBox(widget);
    for (const auto &option : control.choice().options()) {
        combo->addItem(QString::fromStdString(option));
    }
    combo->setCurrentIndex(std::min<int>(combo->count() - 1, static_cast<int>(control.choice().selected_index())));
    row->addWidget(label);
    row->addWidget(combo, 1);
    if (mode == RenderMode::LiveDispatch) {
        connect(combo, qOverload<int>(&QComboBox::currentIndexChanged), this, [this, control](int index) {
            if (index < 0) {
                return;
            }
            dispatchControl(QString::fromStdString(control.id()), [index](beebium::DispatchRequest &request) {
                request.set_index_value(static_cast<quint32>(index));
            });
        });
    } else if (mode == RenderMode::LocalEditor && editorBindings) {
        editorBindings->push_back({QString::fromStdString(control.id()), EditorFieldBinding::Kind::Index, combo});
    } else {
        combo->setEnabled(false);
    }
    return widget;
}

QWidget *ExtensionUiPanel::buildTextInputWidget(const beebium::Control &control, RenderMode mode, QVector<EditorFieldBinding> *editorBindings) {
    auto *widget = new QWidget(this);
    auto *row = new QHBoxLayout(widget);
    row->setContentsMargins(0, 0, 0, 0);
    auto *label = new QLabel(sanitizeText(control.text_input().label()), widget);
    auto *lineEdit = new QLineEdit(QString::fromStdString(control.text_input().value()), widget);
    lineEdit->setPlaceholderText(QString::fromStdString(control.text_input().placeholder()));
    row->addWidget(label);
    row->addWidget(lineEdit, 1);
    if (mode == RenderMode::LiveDispatch) {
        connect(lineEdit, &QLineEdit::editingFinished, this, [this, control, lineEdit]() {
            dispatchControl(QString::fromStdString(control.id()), [value = lineEdit->text()](beebium::DispatchRequest &request) {
                request.set_string_value(value.toStdString());
            });
        });
    } else if (mode == RenderMode::LocalEditor && editorBindings) {
        editorBindings->push_back({QString::fromStdString(control.id()), EditorFieldBinding::Kind::String, lineEdit});
    } else {
        lineEdit->setReadOnly(true);
    }
    return widget;
}

QWidget *ExtensionUiPanel::buildEditableChoiceWidget(const beebium::Control &control, RenderMode mode, QVector<EditorFieldBinding> *editorBindings) {
    auto *widget = new QWidget(this);
    auto *row = new QHBoxLayout(widget);
    row->setContentsMargins(0, 0, 0, 0);
    auto *label = new QLabel(sanitizeText(control.editable_choice().label()), widget);
    auto *combo = new QComboBox(widget);
    combo->setEditable(true);
    combo->setInsertPolicy(QComboBox::NoInsert);
    for (const auto &option : control.editable_choice().options()) {
        combo->addItem(QString::fromStdString(option));
    }
    combo->setCurrentText(QString::fromStdString(control.editable_choice().value()));
    combo->lineEdit()->setPlaceholderText(QString::fromStdString(control.editable_choice().placeholder()));
    row->addWidget(label);
    row->addWidget(combo, 1);
    if (mode == RenderMode::LiveDispatch) {
        connect(combo->lineEdit(), &QLineEdit::editingFinished, this, [this, control, combo]() {
            dispatchControl(QString::fromStdString(control.id()), [value = combo->currentText()](beebium::DispatchRequest &request) {
                request.set_string_value(value.toStdString());
            });
        });
        connect(combo, qOverload<int>(&QComboBox::activated), this, [this, control, combo](int) {
            dispatchControl(QString::fromStdString(control.id()), [value = combo->currentText()](beebium::DispatchRequest &request) {
                request.set_string_value(value.toStdString());
            });
        });
    } else if (mode == RenderMode::LocalEditor && editorBindings) {
        editorBindings->push_back({QString::fromStdString(control.id()), EditorFieldBinding::Kind::String, combo});
    } else {
        combo->setEnabled(false);
    }
    return widget;
}

QWidget *ExtensionUiPanel::buildModalEditorWidget(const beebium::Control &control) {
    auto *widget = new QWidget(this);
    auto *row = new QHBoxLayout(widget);
    row->setContentsMargins(0, 0, 0, 0);
    row->addWidget(buildControlWidget(control.modal_editor().anchor(), RenderMode::ReadOnly), 1);
    if (control.modal_editor().editable()) {
        auto *button = new QPushButton(tr("Edit"), widget);
        connect(button, &QPushButton::clicked, this, [this, control]() {
            showModalEditor(control);
        });
        row->addWidget(button, 0);
    }
    return widget;
}

bool ExtensionUiPanel::dispatchControl(const QString &controlId, const std::function<void(beebium::DispatchRequest &)> &payloadWriter) {
    if (!stub_ || !currentView_.has_root()) {
        return false;
    }

    grpc::ClientContext context;
    beebium::DispatchRequest request;
    request.set_extension_id(extensionId_.toStdString());
    request.set_control_id(controlId.toStdString());
    request.set_view_revision(currentView_.view_revision());
    payloadWriter(request);

    beebium::DispatchResponse response;
    const grpc::Status status = stub_->Dispatch(&context, request, &response);
    if (!status.ok()) {
        emit errorOccurred(tr("Dispatch failed for '%1': %2").arg(label_, QString::fromStdString(status.error_message())));
        return false;
    }
    if (!response.accepted()) {
        emit errorOccurred(tr("Dispatch rejected for '%1': %2").arg(label_, QString::fromStdString(response.error())));
        return false;
    }
    return true;
}

bool ExtensionUiPanel::showModalEditor(const beebium::Control &control) {
    QDialog dialog(this);
    dialog.setWindowTitle(label_);
    auto *dialogLayout = new QVBoxLayout(&dialog);
    QVector<EditorFieldBinding> bindings;
    dialogLayout->addWidget(buildControlWidget(control.modal_editor().editor(), RenderMode::LocalEditor, &bindings));

    auto *buttonBox = new QDialogButtonBox(QDialogButtonBox::Cancel, &dialog);
    auto *commitButton = buttonBox->addButton(commitButtonText(control.modal_editor().commit_role()), QDialogButtonBox::AcceptRole);
    commitButton->setDefault(true);
    if (!control.modal_editor().show_cancel()) {
        buttonBox->button(QDialogButtonBox::Cancel)->setText(tr("Close"));
    }
    connect(buttonBox, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttonBox, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    dialogLayout->addWidget(buttonBox);

    if (dialog.exec() != QDialog::Accepted) {
        return false;
    }

    return dispatchControl(QString::fromStdString(control.id()), [&bindings](beebium::DispatchRequest &request) {
        auto *commit = request.mutable_editor_commit();
        for (const EditorFieldBinding &binding : bindings) {
            auto *field = commit->add_fields();
            field->set_field_id(binding.fieldId.toStdString());
            switch (binding.kind) {
            case EditorFieldBinding::Kind::Bool:
                field->set_bool_value(qobject_cast<QCheckBox *>(binding.object)->isChecked());
                break;
            case EditorFieldBinding::Kind::String:
                if (auto *lineEdit = qobject_cast<QLineEdit *>(binding.object)) {
                    field->set_string_value(lineEdit->text().toStdString());
                } else if (auto *combo = qobject_cast<QComboBox *>(binding.object)) {
                    field->set_string_value(combo->currentText().toStdString());
                }
                break;
            case EditorFieldBinding::Kind::Index:
                field->set_index_value(static_cast<quint32>(qobject_cast<QComboBox *>(binding.object)->currentIndex()));
                break;
            }
        }
    });
}

QString ExtensionUiPanel::sanitizeText(const std::string &text) {
    return QString::fromStdString(text);
}
