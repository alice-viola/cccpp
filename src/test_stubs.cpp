// Minimal stubs for test_pipeline linker — provides ChatPanel method symbols
// that PipelineEngine.cpp references but never calls in tests (m_chatPanel is null).

#include "ui/ChatPanel.h"

QString ChatPanel::delegateToChild(const QString &, const QString &,
                                    const QString &, const QString &,
                                    const QStringList &) { return {}; }
QString ChatPanel::sessionFinalOutput(const QString &) const { return {}; }
