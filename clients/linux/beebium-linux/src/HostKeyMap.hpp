#pragma once

#include <optional>

#include <Qt>

struct HostKeyAction {
    enum class Kind {
        MatrixKey,
        BreakKey,
    };

    Kind kind = Kind::MatrixKey;
    quint32 ikNumber = 0;
};

[[nodiscard]] std::optional<HostKeyAction> mapHostKey(int qtKey);
