#pragma once

#include <QString>

struct ConnectionTarget {
    QString host;
    int port = 48875;

    [[nodiscard]] QString address() const;
};
