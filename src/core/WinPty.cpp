#ifdef _WIN32

#include "core/WinPty.h"
#include <QThread>
#include <QDebug>

WinPty::WinPty(QObject *parent)
    : PtyProcess(parent)
{
}

WinPty::~WinPty()
{
    terminate();
}

bool WinPty::start(const QString &program, const QStringList &args,
                   const QString &workingDir, const QStringList &env)
{
    COORD size;
    size.X = 80;
    size.Y = 24;

    // Create pipes for the pseudoconsole
    HANDLE inRead = INVALID_HANDLE_VALUE, inWrite = INVALID_HANDLE_VALUE;
    HANDLE outRead = INVALID_HANDLE_VALUE, outWrite = INVALID_HANDLE_VALUE;

    if (!CreatePipe(&inRead, &inWrite, nullptr, 0) ||
        !CreatePipe(&outRead, &outWrite, nullptr, 0)) {
        qWarning() << "[WinPty] Failed to create pipes";
        return false;
    }

    HRESULT hr = CreatePseudoConsole(size, inRead, outWrite, 0, &m_hPC);
    // Close handles that are now owned by the pseudoconsole
    CloseHandle(inRead);
    CloseHandle(outWrite);

    if (FAILED(hr)) {
        qWarning() << "[WinPty] CreatePseudoConsole failed:" << hr;
        CloseHandle(inWrite);
        CloseHandle(outRead);
        return false;
    }

    m_pipeIn = inWrite;
    m_pipeOut = outRead;

    // Set up process attribute list with the pseudoconsole
    SIZE_T attrListSize = 0;
    InitializeProcThreadAttributeList(nullptr, 1, 0, &attrListSize);
    auto attrList = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(
        HeapAlloc(GetProcessHeap(), 0, attrListSize));
    if (!attrList) {
        terminate();
        return false;
    }

    InitializeProcThreadAttributeList(attrList, 1, 0, &attrListSize);
    UpdateProcThreadAttribute(attrList, 0, PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE,
                              m_hPC, sizeof(HPCON), nullptr, nullptr);

    // Build command line
    QString cmdLine = program;
    for (const QString &arg : args)
        cmdLine += QStringLiteral(" \"%1\"").arg(arg);

    STARTUPINFOEXW si = {};
    si.StartupInfo.cb = sizeof(si);
    si.lpAttributeList = attrList;

    PROCESS_INFORMATION pi = {};
    std::wstring cmdLineW = cmdLine.toStdWString();
    std::wstring workDirW = workingDir.isEmpty() ? std::wstring() : workingDir.toStdWString();

    BOOL ok = CreateProcessW(
        nullptr,
        cmdLineW.data(),
        nullptr, nullptr, FALSE,
        EXTENDED_STARTUPINFO_PRESENT,
        nullptr,
        workDirW.empty() ? nullptr : workDirW.c_str(),
        &si.StartupInfo,
        &pi);

    DeleteProcThreadAttributeList(attrList);
    HeapFree(GetProcessHeap(), 0, attrList);

    if (!ok) {
        qWarning() << "[WinPty] CreateProcessW failed:" << GetLastError();
        terminate();
        return false;
    }

    m_hProcess = pi.hProcess;
    CloseHandle(pi.hThread);
    m_running = true;

    // Start reader thread
    m_readerThread = QThread::create([this] { readerThreadFunc(); });
    m_readerThread->start();

    return true;
}

void WinPty::write(const QByteArray &data)
{
    if (m_pipeIn == INVALID_HANDLE_VALUE)
        return;

    DWORD written = 0;
    WriteFile(m_pipeIn, data.constData(), static_cast<DWORD>(data.size()), &written, nullptr);
}

void WinPty::resize(int rows, int cols)
{
    if (m_hPC == INVALID_HANDLE_VALUE)
        return;

    COORD size;
    size.X = static_cast<SHORT>(cols);
    size.Y = static_cast<SHORT>(rows);
    ResizePseudoConsole(m_hPC, size);
}

void WinPty::terminate()
{
    m_running = false;

    if (m_hPC != INVALID_HANDLE_VALUE) {
        ClosePseudoConsole(m_hPC);
        m_hPC = INVALID_HANDLE_VALUE;
    }

    if (m_pipeIn != INVALID_HANDLE_VALUE) {
        CloseHandle(m_pipeIn);
        m_pipeIn = INVALID_HANDLE_VALUE;
    }
    if (m_pipeOut != INVALID_HANDLE_VALUE) {
        CloseHandle(m_pipeOut);
        m_pipeOut = INVALID_HANDLE_VALUE;
    }

    if (m_readerThread) {
        m_readerThread->wait(2000);
        delete m_readerThread;
        m_readerThread = nullptr;
    }

    if (m_hProcess != INVALID_HANDLE_VALUE) {
        TerminateProcess(m_hProcess, 0);
        CloseHandle(m_hProcess);
        m_hProcess = INVALID_HANDLE_VALUE;
    }
}

bool WinPty::isRunning() const
{
    return m_running;
}

void WinPty::readerThreadFunc()
{
    char buf[4096];
    while (m_running) {
        DWORD bytesRead = 0;
        BOOL ok = ReadFile(m_pipeOut, buf, sizeof(buf), &bytesRead, nullptr);
        if (ok && bytesRead > 0) {
            QByteArray data(buf, static_cast<int>(bytesRead));
            QMetaObject::invokeMethod(this, [this, data] {
                emit dataReceived(data);
            }, Qt::QueuedConnection);
        } else {
            break;
        }
    }

    m_running = false;

    DWORD exitCode = 0;
    if (m_hProcess != INVALID_HANDLE_VALUE)
        GetExitCodeProcess(m_hProcess, &exitCode);

    QMetaObject::invokeMethod(this, [this, exitCode] {
        emit finished(static_cast<int>(exitCode));
    }, Qt::QueuedConnection);
}

#endif // _WIN32
