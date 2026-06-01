#include "FrameGeometry.hpp"

#include <algorithm>

int FrameGeometry::totalWidth() const {
    const int contentWidth = std::max(displayWidth, width);
    return leftBorder + contentWidth + rightBorder;
}

int FrameGeometry::totalHeight() const {
    const int contentHeight = std::max(displayHeight, height);
    return topBorder + contentHeight + bottomBorder;
}

float FrameGeometry::contentAspectRatio(float parScale) const {
    const int widthPixels = std::max(1, totalWidth());
    int heightPixels = std::max(1, totalHeight());
    if (!interlaced) {
        heightPixels *= 2;
    }
    return (static_cast<float>(widthPixels) * parScale) / static_cast<float>(heightPixels);
}
