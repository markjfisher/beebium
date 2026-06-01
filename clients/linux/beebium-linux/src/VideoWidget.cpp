#include "VideoWidget.hpp"

#include "GrpcKeyboardClient.hpp"
#include "HostKeyMap.hpp"

#include <QFocusEvent>
#include <QGuiApplication>
#include <QHash>
#include <QLoggingCategory>
#include <QPainter>
#include <QRectF>
#include <QTimer>

namespace {

constexpr quint32 kIkShift = 0x00;
constexpr quint32 kIkCapsLock = 0x40;
constexpr float kDisplayMargin = 0.94f;
Q_LOGGING_CATEGORY(videoKeyboardLog, "beebium.linux.keyboard")

QString alphaKeyText(int qtKey) {
    if (qtKey >= Qt::Key_A && qtKey <= Qt::Key_Z) {
        return QString(QChar(u'a' + (qtKey - Qt::Key_A)));
    }
    return QString();
}

} // namespace

constexpr auto kVertexShader = R"(
#ifdef GL_ES
precision mediump float;
#endif

attribute vec2 position;
attribute vec2 texCoord;
varying vec2 vTexCoord;

void main() {
    vTexCoord = texCoord;
    gl_Position = vec4(position, 0.0, 1.0);
}
)";

constexpr auto kFragmentShader = R"(
#ifdef GL_ES
precision mediump float;
precision mediump sampler2D;
#endif

uniform sampler2D frameTexture;
varying vec2 vTexCoord;

void main() {
    gl_FragColor = texture2D(frameTexture, vTexCoord);
}
)";

VideoWidget::VideoWidget(QWidget *parent)
    : QOpenGLWidget(parent) {
    setMinimumSize(640, 480);
    setFocusPolicy(Qt::StrongFocus);
    setFocus();
}

VideoWidget::~VideoWidget() {
    releaseAllKeys();
    makeCurrent();
    texture_.reset();
    program_.reset();
    doneCurrent();
}

void VideoWidget::setKeyboardClient(GrpcKeyboardClient *keyboardClient) {
    keyboardClient_ = keyboardClient;
}

void VideoWidget::setBbcCapsLockState(bool enabled) {
    bbcCapsLockOn_ = enabled;
}

void VideoWidget::presentFrame(const QByteArray &bgraPixels,
                               const FrameGeometry &geometry,
                               quint64 frameNumber) {
    {
        QMutexLocker locker(&frameMutex_);
        geometry_ = geometry;
        frameNumber_ = frameNumber;

        if (geometry.width > 0 && geometry.height > 0
            && bgraPixels.size() >= geometry.width * geometry.height * 4) {
            QImage image(reinterpret_cast<const uchar *>(bgraPixels.constData()),
                         geometry.width,
                         geometry.height,
                         geometry.width * 4,
                         QImage::Format_ARGB32);
            frameImage_ = image.rgbSwapped().copy();
        } else {
            frameImage_ = QImage();
        }
    }
    update();
}

void VideoWidget::initializeGL() {
    initializeOpenGLFunctions();
    glClearColor(0.04f, 0.04f, 0.04f, 1.0f);
    shaderReady_ = ensureProgram();
}

void VideoWidget::resizeGL(int width, int height) {
    glViewport(0, 0, width, height);
}

void VideoWidget::paintGL() {
    QImage frameImage;
    FrameGeometry geometry;
    {
        QMutexLocker locker(&frameMutex_);
        frameImage = frameImage_;
        geometry = geometry_;
    }

    glClear(GL_COLOR_BUFFER_BIT);

    const float widgetAspect = height() > 0
        ? static_cast<float>(width()) / static_cast<float>(height())
        : 1.0f;
    const float contentAspect = geometry.contentAspectRatio(parScale_);

    float xScale = 1.0f;
    float yScale = 1.0f;
    if (widgetAspect > contentAspect) {
        xScale = contentAspect / widgetAspect;
    } else {
        yScale = widgetAspect / contentAspect;
    }

    xScale *= kDisplayMargin;
    yScale *= kDisplayMargin;

    if (shaderReady_ && !frameImage.isNull() && geometry.width > 0 && geometry.height > 0) {
        recreateTextureIfNeeded(frameImage.width(), frameImage.height());

        if (texture_ && program_) {
            texture_->setData(QOpenGLTexture::RGBA, QOpenGLTexture::UInt8, frameImage.constBits());

            const GLfloat vertices[] = {
                -xScale, -yScale, 0.0f, 1.0f,
                 xScale, -yScale, 1.0f, 1.0f,
                -xScale,  yScale, 0.0f, 0.0f,
                 xScale, -yScale, 1.0f, 1.0f,
                 xScale,  yScale, 1.0f, 0.0f,
                -xScale,  yScale, 0.0f, 0.0f,
            };

            program_->bind();
            texture_->bind(0);
            program_->setUniformValue("frameTexture", 0);
            program_->enableAttributeArray(0);
            program_->enableAttributeArray(1);
            program_->setAttributeArray(0, GL_FLOAT, vertices, 2, 4 * sizeof(GLfloat));
            program_->setAttributeArray(1, GL_FLOAT, vertices + 2, 2, 4 * sizeof(GLfloat));
            glDrawArrays(GL_TRIANGLES, 0, 6);
            program_->disableAttributeArray(0);
            program_->disableAttributeArray(1);
            texture_->release();
            program_->release();
        }
    }
    if (!shaderReady_ && !shaderError_.isEmpty()) {
        QPainter painter(this);
        painter.setPen(Qt::white);
        const QString overlay = tr("Renderer fallback: %1").arg(shaderError_);
        if (!frameImage.isNull() && geometry.width > 0 && geometry.height > 0) {
            const qreal drawWidth = width() * xScale;
            const qreal drawHeight = height() * yScale;
            const QRectF target((width() - drawWidth) / 2.0,
                                (height() - drawHeight) / 2.0,
                                 drawWidth,
                                 drawHeight);
            painter.drawImage(target, frameImage);
        }
        painter.drawText(rect().adjusted(8, 8, -8, -8),
                         Qt::AlignTop | Qt::AlignLeft,
                         overlay);
    }
}

void VideoWidget::keyPressEvent(QKeyEvent *event) {
    if (!keyboardClient_) {
        QOpenGLWidget::keyPressEvent(event);
        return;
    }

    if (event->isAutoRepeat()) {
        event->accept();
        return;
    }

    if (handleNonTextKey(event, true)) {
        event->accept();
        return;
    }

    const QString alphaText = alphaKeyText(event->key());
    if (!alphaText.isEmpty()) {
        const auto mapping = keyboardClient_->getCharacterMapping(alphaText);
        if (mapping.has_value()) {
            const int syntheticKey = 0x100000 + event->key();
            if (pressedQtKeys_.contains(syntheticKey)) {
                event->accept();
                return;
            }

            if (keyboardClient_->keyDown(mapping->ikNumber)) {
                pressedQtKeys_.insert(syntheticKey);
                pressedMappings_.insert(syntheticKey, mapping->ikNumber);
            }
            qInfo(videoKeyboardLog).noquote() << QStringLiteral("[alpha] qt=") << event->key()
                                              << QStringLiteral("text=") << event->text()
                                              << QStringLiteral("mapped=") << alphaText
                                              << QStringLiteral("hostShift=") << hostShiftHeld_
                                              << QStringLiteral("bbcShift=") << bbcShiftDown_
                                              << QStringLiteral("bbcCaps=") << bbcCapsLockOn_;
            event->accept();
            return;
        }
    }

    const QString text = event->text().left(1);
    if (!text.isEmpty() && text.at(0).isPrint()) {
        const auto mapping = keyboardClient_->getCharacterMapping(text);
        if (mapping.has_value()) {
            const int syntheticKey = 0x100000 + event->key();
            if (pressedQtKeys_.contains(syntheticKey)) {
                event->accept();
                return;
            }

            bool ok = true;
            if (!mapping->needsShift && hostShiftHeld_ && bbcShiftDown_) {
                releaseShift();
                suppressedShiftKeys_.insert(syntheticKey);
            }
            if (mapping->needsShift && !bbcShiftDown_) {
                pressShift();
                syntheticShiftKeys_.insert(syntheticKey);
            }
            ok = keyboardClient_->keyDown(mapping->ikNumber) && ok;
            if (ok) {
                pressedQtKeys_.insert(syntheticKey);
                pressedMappings_.insert(syntheticKey, mapping->ikNumber);
            } else {
                syntheticShiftKeys_.remove(syntheticKey);
                suppressedShiftKeys_.remove(syntheticKey);
            }
            event->accept();
            return;
        }
    }

    QOpenGLWidget::keyPressEvent(event);
}

void VideoWidget::keyReleaseEvent(QKeyEvent *event) {
    if (!keyboardClient_) {
        QOpenGLWidget::keyReleaseEvent(event);
        return;
    }

    if (event->isAutoRepeat()) {
        event->accept();
        return;
    }

    if (handleNonTextKey(event, false)) {
        event->accept();
        return;
    }

    const QString alphaText = alphaKeyText(event->key());
    if (!alphaText.isEmpty()) {
        const int syntheticKey = 0x100000 + event->key();
        const auto it = pressedMappings_.find(syntheticKey);
        if (it != pressedMappings_.end()) {
            keyboardClient_->keyUp(it.value());
            pressedMappings_.erase(it);
            pressedQtKeys_.remove(syntheticKey);
            event->accept();
            return;
        }
    }

    const QString text = event->text().left(1);
    if (!text.isEmpty() && text.at(0).isPrint()) {
        const int syntheticKey = 0x100000 + event->key();
        const auto it = pressedMappings_.find(syntheticKey);
        if (it != pressedMappings_.end()) {
            keyboardClient_->keyUp(it.value());
            pressedMappings_.erase(it);
            if (syntheticShiftKeys_.contains(syntheticKey)) {
                releaseShift();
                syntheticShiftKeys_.remove(syntheticKey);
            }
            if (suppressedShiftKeys_.contains(syntheticKey)) {
                if (hostShiftHeld_) {
                    pressShift();
                }
                suppressedShiftKeys_.remove(syntheticKey);
            }
            pressedQtKeys_.remove(syntheticKey);
            event->accept();
            return;
        }
    }

    QOpenGLWidget::keyReleaseEvent(event);
}

void VideoWidget::focusInEvent(QFocusEvent *event) {
    syncCapsLockState();
    QOpenGLWidget::focusInEvent(event);
}

void VideoWidget::focusOutEvent(QFocusEvent *event) {
    releaseAllKeys();
    QOpenGLWidget::focusOutEvent(event);
}

bool VideoWidget::focusNextPrevChild(bool) {
    return false;
}

bool VideoWidget::ensureProgram() {
    program_ = std::make_unique<QOpenGLShaderProgram>();

    if (!program_->addShaderFromSourceCode(QOpenGLShader::Vertex, kVertexShader)) {
        shaderError_ = program_->log();
        program_.reset();
        return false;
    }
    if (!program_->addShaderFromSourceCode(QOpenGLShader::Fragment, kFragmentShader)) {
        shaderError_ = program_->log();
        program_.reset();
        return false;
    }

    program_->bindAttributeLocation("position", 0);
    program_->bindAttributeLocation("texCoord", 1);
    if (!program_->link()) {
        shaderError_ = program_->log();
        program_.reset();
        return false;
    }

    shaderError_.clear();
    return true;
}

void VideoWidget::recreateTextureIfNeeded(int width, int height) {
    if (width <= 0 || height <= 0) {
        return;
    }

    if (!texture_ || texture_->width() != width || texture_->height() != height) {
        texture_.reset();
        texture_ = std::make_unique<QOpenGLTexture>(QOpenGLTexture::Target2D);
        texture_->setFormat(QOpenGLTexture::RGBA8_UNorm);
        texture_->setSize(width, height);
        texture_->setMipLevels(1);
        texture_->allocateStorage(QOpenGLTexture::RGBA, QOpenGLTexture::UInt8);
        texture_->setMinificationFilter(QOpenGLTexture::Nearest);
        texture_->setMagnificationFilter(QOpenGLTexture::Nearest);
        texture_->setWrapMode(QOpenGLTexture::ClampToEdge);
    }
}

bool VideoWidget::handleNonTextKey(QKeyEvent *event, bool pressed) {
    if (!keyboardClient_) {
        return false;
    }

    if (event->key() == Qt::Key_CapsLock) {
        if (pressed) {
            tapCapsLock();
        }
        return true;
    }

    const auto action = mapHostKey(event->key());
    if (!action.has_value()) {
        return false;
    }

    if (event->key() == Qt::Key_Shift) {
        hostShiftHeld_ = pressed;
    }

    if (action->kind == HostKeyAction::Kind::BreakKey) {
        if (pressed) {
            keyboardClient_->breakDown();
        } else {
            keyboardClient_->breakUp();
        }
        return true;
    }

    const quint32 ikNumber = action->ikNumber;

    if (pressed) {
        if (pressedQtKeys_.contains(event->key())) {
            return true;
        }
        if (keyboardClient_->keyDown(ikNumber)) {
            pressedQtKeys_.insert(event->key());
            pressedMappings_.insert(event->key(), ikNumber);
            if (ikNumber == kIkShift) {
                bbcShiftDown_ = true;
            }
        }
    } else {
        const auto it = pressedMappings_.find(event->key());
        if (it != pressedMappings_.end()) {
            keyboardClient_->keyUp(it.value());
            if (it.value() == kIkShift) {
                bbcShiftDown_ = false;
            }
            pressedMappings_.erase(it);
            pressedQtKeys_.remove(event->key());
        }
    }

    return true;
}

void VideoWidget::releaseAllKeys() {
    if (!keyboardClient_) {
        pressedQtKeys_.clear();
        pressedMappings_.clear();
        syntheticShiftKeys_.clear();
        suppressedShiftKeys_.clear();
        hostShiftHeld_ = false;
        bbcShiftDown_ = false;
        return;
    }

    for (auto it = pressedMappings_.begin(); it != pressedMappings_.end(); ++it) {
        keyboardClient_->keyUp(it.value());
    }
    if (bbcShiftDown_) {
        keyboardClient_->keyUp(kIkShift);
    }
    pressedQtKeys_.clear();
    pressedMappings_.clear();
    syntheticShiftKeys_.clear();
    suppressedShiftKeys_.clear();
    hostShiftHeld_ = false;
    bbcShiftDown_ = false;
}

void VideoWidget::pressShift() {
    if (!keyboardClient_ || bbcShiftDown_) {
        return;
    }
    if (keyboardClient_->keyDown(kIkShift)) {
        bbcShiftDown_ = true;
    }
}

void VideoWidget::releaseShift() {
    if (!keyboardClient_ || !bbcShiftDown_) {
        return;
    }
    if (keyboardClient_->keyUp(kIkShift)) {
        bbcShiftDown_ = false;
    }
}

void VideoWidget::syncCapsLockState() {
    qInfo(videoKeyboardLog).noquote() << QStringLiteral("[caps] focus sync disabled, bbc=")
                                      << bbcCapsLockOn_;
}

void VideoWidget::tapCapsLock() {
    if (!keyboardClient_) {
        return;
    }
    if (capsLockTapInFlight_) {
        return;
    }
    if (keyboardClient_->keyDown(kIkCapsLock)) {
        capsLockTapInFlight_ = true;
        qInfo(videoKeyboardLog).noquote() << QStringLiteral("[caps] tap down host/bbc before=")
                                          << bbcCapsLockOn_;
        QTimer::singleShot(35, this, [this]() {
            capsLockTapInFlight_ = false;
            if (!keyboardClient_) {
                return;
            }
            keyboardClient_->keyUp(kIkCapsLock);
            qInfo(videoKeyboardLog).noquote() << QStringLiteral("[caps] tap up, waiting for indicator update");
        });
    }
}
