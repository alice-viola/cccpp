#include "core/PtyProcess.h"

#ifdef _WIN32
#include "core/WinPty.h"
#else
#include "core/UnixPty.h"
#endif

PtyProcess *PtyProcess::create(QObject *parent)
{
#ifdef _WIN32
    return new WinPty(parent);
#else
    return new UnixPty(parent);
#endif
}
