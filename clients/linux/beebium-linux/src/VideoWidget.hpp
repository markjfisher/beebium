#pragma once

#include <QByteArray>
#include <QImage>
#include <QKeyEvent>
#include <QMutex>
#include <QOpenGLFunctions>
#include <QOpenGLShaderProgram>
#include <QOpenGLTexture>
#include <QOpenGLWidget>
#include <QTimer>

#include "FrameGeometry.hpp"

class VideoWidget final : public QOpenGLWidget, protected QOpenGLFunctions {
    Q_OBJECT

public:
    explicit VideoWidget(QWidget *parent = nullptr);
    ~VideoWidget() override;

    void setKeyboardClient(class GrpcKeyboardClient *keyboardClient);
    void setBbcCapsLockState(bool enabled);

public slots:
    void presentFrame(const QByteArray &bgraPixels, const FrameGeometry &geometry, quint64 frameNumber);

protected:
    void initializeGL() override;
    void resizeGL(int width, int height) override;
    void paintGL() override;
    void keyPressEvent(QKeyEvent *event) override;
    void keyReleaseEvent(QKeyEvent *event) override;
    void focusInEvent(QFocusEvent *event) override;
    void focusOutEvent(QFocusEvent *event) override;
    bool focusNextPrevChild(bool next) override;

private:
    [[nodiscard]] bool ensureProgram();
    void recreateTextureIfNeeded(int width, int height);
    [[nodiscard]] bool handleNonTextKey(QKeyEvent *event, bool pressed);
    void releaseAllKeys();
    void pressShift();
    void releaseShift();
    void syncCapsLockState();
    void tapCapsLock();

    QMutex frameMutex_;
    QImage frameImage_;
    FrameGeometry geometry_;
    quint64 frameNumber_ = 0;
    float parScale_ = 0.96f;
    bool shaderReady_ = false;
    QString shaderError_;
    std::unique_ptr<QOpenGLShaderProgram> program_;
    std::unique_ptr<QOpenGLTexture> texture_;
    class GrpcKeyboardClient *keyboardClient_ = nullptr;
    QSet<int> pressedQtKeys_;
    QHash<int, quint32> pressedMappings_;
    QSet<int> syntheticShiftKeys_;
    QSet<int> suppressedShiftKeys_;
    bool hostShiftHeld_ = false;
    bool bbcShiftDown_ = false;
    bool bbcCapsLockOn_ = false;
    bool capsLockTapInFlight_ = false;
};
