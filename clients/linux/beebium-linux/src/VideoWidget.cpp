#include "VideoWidget.hpp"

#include "GrpcKeyboardClient.hpp"
#include "HostKeyMap.hpp"

#include <QFocusEvent>
#include <QHash>
#include <QPainter>
#include <QRectF>
#include <QTimer>

#include <cmath>

namespace {

constexpr quint32 kIkShift = 0x00;
constexpr quint32 kIkCapsLock = 0x40;
constexpr float kDisplayMargin = 0.94f;

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

void VideoWidget::setAspectMode(AspectMode mode) {
    if (aspectMode_ == mode) {
        return;
    }
    aspectMode_ = mode;
    update();
}

VideoWidget::AspectMode VideoWidget::aspectMode() const {
    return aspectMode_;
}

void VideoWidget::setTextureFilter(TextureFilter filter) {
    if (textureFilter_ == filter) {
        return;
    }
    textureFilter_ = filter;
    if (texture_) {
        applyTextureSampling();
    }
    update();
}

VideoWidget::TextureFilter VideoWidget::textureFilter() const {
    return textureFilter_;
}

void VideoWidget::setIntegerScalingEnabled(bool enabled) {
    if (integerScalingEnabled_ == enabled) {
        return;
    }
    integerScalingEnabled_ = enabled;
    update();
}

bool VideoWidget::integerScalingEnabled() const {
    return integerScalingEnabled_;
}

void VideoWidget::setIntegerFitEnabled(bool enabled) {
    if (integerFitEnabled_ == enabled) {
        return;
    }
    integerFitEnabled_ = enabled;
    update();
}

bool VideoWidget::integerFitEnabled() const {
    return integerFitEnabled_;
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

    const QRectF target = targetRectForFrame(geometry, frameImage.size());
    const float xScale = width() > 0 ? static_cast<float>(target.width()) / static_cast<float>(width()) : 1.0f;
    const float yScale = height() > 0 ? static_cast<float>(target.height()) / static_cast<float>(height()) : 1.0f;
    const float xOffset = width() > 0
        ? static_cast<float>((target.x() + target.width() * 0.5) / width()) * 2.0f - 1.0f
        : 0.0f;
    const float yOffset = height() > 0
        ? 1.0f - static_cast<float>((target.y() + target.height() * 0.5) / height()) * 2.0f
        : 0.0f;

    if (shaderReady_ && !frameImage.isNull() && geometry.width > 0 && geometry.height > 0) {
        recreateTextureIfNeeded(frameImage.width(), frameImage.height());

        if (texture_ && program_) {
            texture_->setData(QOpenGLTexture::RGBA, QOpenGLTexture::UInt8, frameImage.constBits());

            const GLfloat vertices[] = {
                xOffset - xScale, yOffset - yScale, 0.0f, 1.0f,
                xOffset + xScale, yOffset - yScale, 1.0f, 1.0f,
                xOffset - xScale, yOffset + yScale, 0.0f, 0.0f,
                xOffset + xScale, yOffset - yScale, 1.0f, 1.0f,
                xOffset + xScale, yOffset + yScale, 1.0f, 0.0f,
                xOffset - xScale, yOffset + yScale, 0.0f, 0.0f,
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

QRectF VideoWidget::targetRectForFrame(const FrameGeometry &geometry, const QSize &frameSize) const {
    float contentAspect = geometry.contentAspectRatio(parScale_);
    if (aspectMode_ == AspectMode::FourThree) {
        contentAspect = 4.0f / 3.0f;
    } else if (aspectMode_ == AspectMode::SquarePixels) {
        const int widthPixels = std::max(1, geometry.totalWidth());
        int heightPixels = std::max(1, geometry.totalHeight());
        if (!geometry.interlaced) {
            heightPixels *= 2;
        }
        contentAspect = static_cast<float>(widthPixels) / static_cast<float>(heightPixels);
    }

    const qreal maxWidth = width() * kDisplayMargin;
    const qreal maxHeight = height() * kDisplayMargin;
    qreal drawWidth = maxWidth;
    qreal drawHeight = maxHeight;
    if (maxHeight > 0.0 && maxWidth / maxHeight > contentAspect) {
        drawWidth = maxHeight * contentAspect;
    } else {
        drawHeight = maxWidth / std::max(0.01f, contentAspect);
    }

    const int sourceWidth = std::max(1, frameSize.width());
    const int sourceHeight = std::max(1, frameSize.height());
    const qreal integerScale = std::min(drawWidth / sourceWidth, drawHeight / sourceHeight);
    if (integerScalingEnabled_) {
        const int scale = std::max(1, static_cast<int>(std::floor(integerScale)));
        drawWidth = sourceWidth * scale;
        drawHeight = sourceHeight * scale;
    } else if (integerFitEnabled_ && integerScale >= 1.0) {
        const int scale = std::max(1, static_cast<int>(std::floor(integerScale)));
        drawWidth = std::min(drawWidth, static_cast<qreal>(sourceWidth * scale));
        drawHeight = std::min(drawHeight, static_cast<qreal>(sourceHeight * scale));
    }

    return QRectF((width() - drawWidth) / 2.0,
                  (height() - drawHeight) / 2.0,
                  drawWidth,
                  drawHeight);
}

void VideoWidget::applyTextureSampling() {
    if (!texture_) {
        return;
    }
    const auto filter = textureFilter_ == TextureFilter::Nearest
        ? QOpenGLTexture::Nearest
        : QOpenGLTexture::Linear;
    texture_->setMinificationFilter(filter);
    texture_->setMagnificationFilter(filter);
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
        applyTextureSampling();
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
        QTimer::singleShot(35, this, [this]() {
            capsLockTapInFlight_ = false;
            if (!keyboardClient_) {
                return;
            }
            keyboardClient_->keyUp(kIkCapsLock);
        });
    }
}
