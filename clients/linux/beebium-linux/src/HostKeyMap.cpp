#include "HostKeyMap.hpp"

namespace {

constexpr quint32 kIkShift = 0x00;
constexpr quint32 kIkCtrl = 0x01;
constexpr quint32 kIkCapsLock = 0x40;
constexpr quint32 kIkEscape = 0x70;
constexpr quint32 kIkTab = 0x60;
constexpr quint32 kIkReturn = 0x49;
constexpr quint32 kIkDelete = 0x59;
constexpr quint32 kIkLeft = 0x19;
constexpr quint32 kIkRight = 0x79;
constexpr quint32 kIkDown = 0x29;
constexpr quint32 kIkUp = 0x39;
constexpr quint32 kIkSpace = 0x62;
constexpr quint32 kIkCopy = 0x69;
constexpr quint32 kIkF0 = 0x71;
constexpr quint32 kIkF1 = 0x72;
constexpr quint32 kIkF2 = 0x73;
constexpr quint32 kIkF3 = 0x14;
constexpr quint32 kIkF4 = 0x15;
constexpr quint32 kIkF5 = 0x16;
constexpr quint32 kIkF6 = 0x17;
constexpr quint32 kIkF7 = 0x18;
constexpr quint32 kIkF8 = 0x26;
constexpr quint32 kIkF9 = 0x27;

} // namespace

std::optional<HostKeyAction> mapHostKey(int qtKey) {
    switch (qtKey) {
    case Qt::Key_Shift:
        return HostKeyAction{HostKeyAction::Kind::MatrixKey, kIkShift};
    case Qt::Key_Control:
        return HostKeyAction{HostKeyAction::Kind::MatrixKey, kIkCtrl};
    case Qt::Key_CapsLock:
        return HostKeyAction{HostKeyAction::Kind::MatrixKey, kIkCapsLock};
    case Qt::Key_Escape:
        return HostKeyAction{HostKeyAction::Kind::MatrixKey, kIkEscape};
    case Qt::Key_Tab:
        return HostKeyAction{HostKeyAction::Kind::MatrixKey, kIkTab};
    case Qt::Key_Return:
    case Qt::Key_Enter:
        return HostKeyAction{HostKeyAction::Kind::MatrixKey, kIkReturn};
    case Qt::Key_Backspace:
        return HostKeyAction{HostKeyAction::Kind::MatrixKey, kIkDelete};
    case Qt::Key_Left:
        return HostKeyAction{HostKeyAction::Kind::MatrixKey, kIkLeft};
    case Qt::Key_Right:
        return HostKeyAction{HostKeyAction::Kind::MatrixKey, kIkRight};
    case Qt::Key_Down:
        return HostKeyAction{HostKeyAction::Kind::MatrixKey, kIkDown};
    case Qt::Key_Up:
        return HostKeyAction{HostKeyAction::Kind::MatrixKey, kIkUp};
    case Qt::Key_Space:
        return HostKeyAction{HostKeyAction::Kind::MatrixKey, kIkSpace};
    case Qt::Key_Home:
        return HostKeyAction{HostKeyAction::Kind::MatrixKey, kIkCopy};
    case Qt::Key_F1:
        return HostKeyAction{HostKeyAction::Kind::MatrixKey, kIkF0};
    case Qt::Key_F2:
        return HostKeyAction{HostKeyAction::Kind::MatrixKey, kIkF1};
    case Qt::Key_F3:
        return HostKeyAction{HostKeyAction::Kind::MatrixKey, kIkF2};
    case Qt::Key_F4:
        return HostKeyAction{HostKeyAction::Kind::MatrixKey, kIkF3};
    case Qt::Key_F5:
        return HostKeyAction{HostKeyAction::Kind::MatrixKey, kIkF4};
    case Qt::Key_F6:
        return HostKeyAction{HostKeyAction::Kind::MatrixKey, kIkF5};
    case Qt::Key_F7:
        return HostKeyAction{HostKeyAction::Kind::MatrixKey, kIkF6};
    case Qt::Key_F8:
        return HostKeyAction{HostKeyAction::Kind::MatrixKey, kIkF7};
    case Qt::Key_F9:
        return HostKeyAction{HostKeyAction::Kind::MatrixKey, kIkF8};
    case Qt::Key_F10:
        return HostKeyAction{HostKeyAction::Kind::MatrixKey, kIkF9};
    case Qt::Key_F12:
        return HostKeyAction{HostKeyAction::Kind::BreakKey, 0};
    default:
        return std::nullopt;
    }
}
