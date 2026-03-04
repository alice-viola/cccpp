#include "core/PipelineEngine.h"
#include "ui/ChatPanel.h"
#include "core/SessionManager.h"
#include "core/Database.h"
#include <QDebug>

PipelineEngine::PipelineEngine(QObject *parent)
    : QObject(parent)
{
    loadBuiltInTemplates();
}

void PipelineEngine::setChatPanel(ChatPanel *panel) { m_chatPanel = panel; }
void PipelineEngine::setSessionManager(SessionManager *mgr) { m_sessionMgr = mgr; }
void PipelineEngine::setDatabase(Database *db) { m_database = db; }
void PipelineEngine::setWorkspace(const QString &workspace) { m_workspace = workspace; }

QList<PipelineTemplate> PipelineEngine::allTemplates() const { return m_templates; }

PipelineTemplate PipelineEngine::pipelineTemplate(const QString &id) const
{
    for (const auto &t : m_templates) {
        if (t.pipelineId == id) return t;
    }
    return {};
}

void PipelineEngine::loadBuiltInTemplates()
{
    // Refactor Pipeline: architect → implementer (with build validation) → [reviewer, tester]
    PipelineTemplate refactor;
    refactor.pipelineId = "builtin-refactor";
    refactor.name = "Refactor Pipeline";
    refactor.description = "Architect → Implement (with build validation) → Review + Test";
    refactor.builtIn = true;
    refactor.nodes = {
        {"architect", "Architect", "specialist-architect",
         "Analyze the following task and produce a detailed implementation plan:\n\n{{user_input}}",
         {}, "", 0, ""},
        {"implementer", "Implementer", "specialist-implementer",
         "Implement the following plan:\n\n{{architect_output}}",
         {"architect"},
         "cmake --build build", 3,
         "The build failed with the following errors:\n\n{{error}}\n\nFix all build errors."},
        {"reviewer", "Reviewer", "specialist-reviewer",
         "Review the changes made by the implementer. The original task was:\n\n"
         "{{user_input}}\n\nThe architect's plan was:\n\n{{architect_output}}",
         {"implementer"}, "", 0, ""},
        {"tester", "Tester", "specialist-tester",
         "Write and run tests for the recent changes. The original task was:\n\n{{user_input}}",
         {"implementer"}, "", 0, ""},
    };
    m_templates.append(refactor);

    // Review Pipeline: reviewer → implementer (fixes)
    PipelineTemplate review;
    review.pipelineId = "builtin-review";
    review.name = "Review Pipeline";
    review.description = "Review → Fix issues found";
    review.builtIn = true;
    review.nodes = {
        {"reviewer", "Reviewer", "specialist-reviewer",
         "Review the current state of the codebase for:\n\n{{user_input}}",
         {}, "", 0, ""},
        {"implementer", "Fixer", "specialist-implementer",
         "Fix the issues found by the reviewer:\n\n{{reviewer_output}}",
         {"reviewer"},
         "cmake --build build", 3,
         "The build failed after your fixes:\n\n{{error}}\n\nFix all build errors."},
    };
    m_templates.append(review);
}

QString PipelineEngine::startPipeline(const QString &pipelineId,
                                       const QString &parentSessionId,
                                       const QString &userInput)
{
    auto tmpl = pipelineTemplate(pipelineId);
    if (tmpl.pipelineId.isEmpty()) {
        // Try name prefix match
        for (const auto &t : m_templates) {
            if (t.name.toLower().startsWith(pipelineId.toLower())) {
                tmpl = t;
                break;
            }
        }
    }
    if (tmpl.pipelineId.isEmpty()) {
        qWarning() << "[PipelineEngine] No template found for:" << pipelineId;
        return {};
    }

    PipelineExecution exec;
    exec.executionId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    exec.pipelineId = tmpl.pipelineId;
    exec.parentSessionId = parentSessionId;
    exec.workspace = m_workspace;
    exec.userInput = userInput;

    for (const auto &node : tmpl.nodes) {
        exec.nodeStates[node.nodeId] = PipelineExecution::Waiting;
        exec.nodeAttempts[node.nodeId] = 0;
    }

    m_executions[exec.executionId] = exec;
    emit pipelineStarted(exec.executionId);

    qDebug() << "[PipelineEngine] Started pipeline:" << tmpl.name
             << "execution:" << exec.executionId;

    advanceExecution(exec.executionId);
    return exec.executionId;
}

void PipelineEngine::cancelPipeline(const QString &executionId)
{
    m_executions.remove(executionId);
    // Remove session→node mappings for this execution
    QStringList toRemove;
    for (auto it = m_sessionToNode.constBegin(); it != m_sessionToNode.constEnd(); ++it) {
        if (it.value().first == executionId)
            toRemove.append(it.key());
    }
    for (const auto &k : toRemove)
        m_sessionToNode.remove(k);
}

PipelineExecution PipelineEngine::execution(const QString &executionId) const
{
    return m_executions.value(executionId);
}

bool PipelineEngine::isNodeReady(const PipelineExecution &exec, const QString &nodeId) const
{
    auto tmpl = pipelineTemplate(exec.pipelineId);
    for (const auto &node : tmpl.nodes) {
        if (node.nodeId != nodeId) continue;
        for (const auto &dep : node.dependsOn) {
            if (exec.nodeStates.value(dep) != PipelineExecution::Completed)
                return false;
        }
        return true;
    }
    return false;
}

void PipelineEngine::advanceExecution(const QString &executionId)
{
    if (!m_executions.contains(executionId)) return;
    auto &exec = m_executions[executionId];
    auto tmpl = pipelineTemplate(exec.pipelineId);

    bool anyRunning = false;
    bool allDone = true;
    bool anyFailed = false;

    for (const auto &node : tmpl.nodes) {
        auto state = exec.nodeStates.value(node.nodeId, PipelineExecution::Waiting);

        if (state == PipelineExecution::Running || state == PipelineExecution::Validating) {
            anyRunning = true;
            allDone = false;
            continue;
        }
        if (state == PipelineExecution::Waiting) {
            allDone = false;
            if (isNodeReady(exec, node.nodeId)) {
                launchNode(exec, node);
                anyRunning = true;
            }
        }
        if (state == PipelineExecution::Failed) {
            anyFailed = true;
        }
    }

    if (anyFailed && !anyRunning) {
        emit pipelineFailed(executionId, "One or more nodes failed");
        return;
    }

    if (allDone) {
        qDebug() << "[PipelineEngine] Pipeline completed:" << executionId;
        emit pipelineCompleted(executionId);
    }
}

void PipelineEngine::launchNode(PipelineExecution &exec, const PipelineNode &node)
{
    if (!m_chatPanel) return;

    exec.nodeStates[node.nodeId] = PipelineExecution::Running;
    exec.nodeAttempts[node.nodeId] = exec.nodeAttempts.value(node.nodeId, 0) + 1;

    QString prompt = buildNodePrompt(exec, node);

    qDebug() << "[PipelineEngine] Launching node:" << node.label
             << "attempt:" << exec.nodeAttempts[node.nodeId];

    QString childSessionId = m_chatPanel->delegateToChild(
        exec.parentSessionId,
        prompt,
        QString(),  // context already baked into prompt via placeholders
        node.specialistProfileId);

    exec.nodeSessionMap[node.nodeId] = childSessionId;
    m_sessionToNode[childSessionId] = {exec.executionId, node.nodeId};

    emit nodeStarted(exec.executionId, node.nodeId, childSessionId);
}

void PipelineEngine::onSessionIdChanged(const QString &oldId, const QString &newId)
{
    // Update session→node mapping when Claude assigns a new session ID
    if (m_sessionToNode.contains(oldId)) {
        m_sessionToNode[newId] = m_sessionToNode.take(oldId);
        qDebug() << "[PipelineEngine] Session ID remapped:" << oldId << "->" << newId;
    }
    // Update nodeSessionMap in executions
    for (auto &exec : m_executions) {
        for (auto it = exec.nodeSessionMap.begin(); it != exec.nodeSessionMap.end(); ++it) {
            if (it.value() == oldId)
                it.value() = newId;
        }
        if (exec.parentSessionId == oldId)
            exec.parentSessionId = newId;
    }
}

void PipelineEngine::onChildSessionFinished(const QString &childSessionId)
{
    if (!m_sessionToNode.contains(childSessionId)) return;

    auto [executionId, nodeId] = m_sessionToNode[childSessionId];
    if (!m_executions.contains(executionId)) return;

    auto &exec = m_executions[executionId];
    auto tmpl = pipelineTemplate(exec.pipelineId);

    // Find the node definition
    PipelineNode nodeDef;
    for (const auto &n : tmpl.nodes) {
        if (n.nodeId == nodeId) { nodeDef = n; break; }
    }

    QString output = m_chatPanel->sessionFinalOutput(childSessionId);
    m_sessionToNode.remove(childSessionId);

    // If node has validation, run it
    if (!nodeDef.validationCommand.isEmpty()) {
        exec.nodeStates[nodeId] = PipelineExecution::Validating;
        exec.nodeOutputs[nodeId] = output;
        runValidation(executionId, nodeId, nodeDef.validationCommand);
        return;
    }

    // No validation — mark completed
    exec.nodeStates[nodeId] = PipelineExecution::Completed;
    exec.nodeOutputs[nodeId] = output;

    qDebug() << "[PipelineEngine] Node completed:" << nodeDef.label;
    emit nodeCompleted(executionId, nodeId, output);

    advanceExecution(executionId);
}

void PipelineEngine::runValidation(const QString &executionId, const QString &nodeId,
                                    const QString &command)
{
    auto *proc = new QProcess(this);
    proc->setWorkingDirectory(m_workspace);
    proc->setProcessChannelMode(QProcess::MergedChannels);

    connect(proc, &QProcess::finished, this,
            [this, proc, executionId, nodeId](int exitCode) {
        proc->deleteLater();

        if (!m_executions.contains(executionId)) return;
        auto &exec = m_executions[executionId];
        auto tmpl = pipelineTemplate(exec.pipelineId);

        PipelineNode nodeDef;
        for (const auto &n : tmpl.nodes) {
            if (n.nodeId == nodeId) { nodeDef = n; break; }
        }

        if (exitCode == 0) {
            // Validation passed
            exec.nodeStates[nodeId] = PipelineExecution::Completed;
            qDebug() << "[PipelineEngine] Validation passed for:" << nodeDef.label;
            emit nodeCompleted(executionId, nodeId, exec.nodeOutputs[nodeId]);
            advanceExecution(executionId);
        } else {
            // Validation failed
            QString errorOutput = QString::fromUtf8(proc->readAll());
            if (errorOutput.length() > 3000)
                errorOutput = errorOutput.left(3000) + "\n[...truncated...]";

            exec.nodeErrors[nodeId] = errorOutput;
            int attempts = exec.nodeAttempts.value(nodeId, 0);

            qDebug() << "[PipelineEngine] Validation failed for:" << nodeDef.label
                     << "attempt:" << attempts << "/" << nodeDef.maxRetries;

            if (attempts < nodeDef.maxRetries) {
                emit nodeRetrying(executionId, nodeId, attempts + 1, errorOutput);
                retryNode(exec, nodeDef);
            } else {
                exec.nodeStates[nodeId] = PipelineExecution::Failed;
                emit nodeFailed(executionId, nodeId,
                    QStringLiteral("Validation failed after %1 attempts").arg(attempts));
                advanceExecution(executionId);
            }
        }
    });

    qDebug() << "[PipelineEngine] Running validation:" << command;
    proc->start("/bin/sh", {"-c", command});

    // Safety timeout: 120 seconds
    QTimer::singleShot(120000, proc, [proc]() {
        if (proc->state() != QProcess::NotRunning) {
            qWarning() << "[PipelineEngine] Validation timeout, killing process";
            proc->kill();
        }
    });
}

void PipelineEngine::retryNode(PipelineExecution &exec, const PipelineNode &node)
{
    if (!m_chatPanel) return;

    // Build retry prompt with error context
    QString retryPrompt = node.retryPromptTemplate;
    if (retryPrompt.isEmpty())
        retryPrompt = "The previous attempt failed:\n\n{{error}}\n\nFix all issues.";

    retryPrompt.replace("{{error}}", exec.nodeErrors.value(node.nodeId));
    retryPrompt.replace("{{user_input}}", exec.userInput);

    // Replace any {{nodeId_output}} placeholders
    for (auto it = exec.nodeOutputs.constBegin(); it != exec.nodeOutputs.constEnd(); ++it)
        retryPrompt.replace(QStringLiteral("{{%1_output}}").arg(it.key()), it.value());

    exec.nodeStates[node.nodeId] = PipelineExecution::Running;
    exec.nodeAttempts[node.nodeId] = exec.nodeAttempts.value(node.nodeId, 0) + 1;

    QString childSessionId = m_chatPanel->delegateToChild(
        exec.parentSessionId,
        retryPrompt,
        QString(),
        node.specialistProfileId);

    exec.nodeSessionMap[node.nodeId] = childSessionId;
    m_sessionToNode[childSessionId] = {exec.executionId, node.nodeId};

    emit nodeStarted(exec.executionId, node.nodeId, childSessionId);
}

QString PipelineEngine::buildNodePrompt(const PipelineExecution &exec,
                                         const PipelineNode &node) const
{
    QString prompt = node.taskTemplate;
    prompt.replace("{{user_input}}", exec.userInput);

    for (auto it = exec.nodeOutputs.constBegin(); it != exec.nodeOutputs.constEnd(); ++it)
        prompt.replace(QStringLiteral("{{%1_output}}").arg(it.key()), it.value());

    return prompt;
}
