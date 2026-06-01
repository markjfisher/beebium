#pragma once

struct FrameGeometry {
    int width = 0;
    int height = 0;
    int displayWidth = 0;
    int displayHeight = 0;
    int leftBorder = 0;
    int rightBorder = 0;
    int topBorder = 0;
    int bottomBorder = 0;
    bool interlaced = false;

    [[nodiscard]] int totalWidth() const;
    [[nodiscard]] int totalHeight() const;
    [[nodiscard]] float contentAspectRatio(float parScale) const;
};
