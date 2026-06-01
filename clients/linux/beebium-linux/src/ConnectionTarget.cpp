#include "ConnectionTarget.hpp"

QString ConnectionTarget::address() const {
    return QStringLiteral("%1:%2").arg(host).arg(port);
}
