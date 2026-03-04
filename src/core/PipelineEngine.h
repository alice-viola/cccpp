#pragma once

#include <QObject>
#include <QString>
#include <QMap>
#include <QList>
#include <QStringList>
#include <QProcess>
#include <QUuid>

class ChatPanel;
class SessionManager;
class Database;

struct PipelineNode {
    QString nodeId;
    QString label;
    QString specialistProfileId;
    QString taskTemplate;       // prompt with {{user_input}}, {{nodeId_output}} placeholders
    QStringList dependsOn;

    // Validation gate
    QString validationCommand;  // e.g. "cmake --build build" — empty means no validation
    int maxRetries = 3;
    QString retryPromptTemplate; // "Previous attempt failed:\n{{error}}\n\nFix the issues."
};

struct PipelineTemplate {
    QString pipelineId;
    QString name;
    QString description;
    QList<PipelineNode> nodes;
    bool builtIn = false;
};

struct PipelineExecution {
    QString executionId;
    QString pipelineId;
    QString parentSessionId;
    QString workspace;
    QString userInput;

    QMap<QString, QString> nodeSessionMap;   // nodeId → sessionId
    enum NodeState { Waiting, Running, Validating, Completed, Failed };
    QMap<QString, NodeState> nodeStates;
    QMap<QString, QString> nodeOutputs;      // nodeId → output text
    QMap<QString, int> nodeAttempts;          // nodeId → current attempt
    QMap<QString, QString> nodeErrors;       // nodeId → last validation error
};

class PipelineEngine : public QObject {
    Q_OBJECT
public:
    explicit PipelineEngine(QObject *parent = nullptr);

    void setChatPanel(ChatPanel *panel);
    void setSessionManager(SessionManager *mgr);
    void setDatabase(Database *db);
    void setWorkspace(const QString &workspace);

    // Template management
    QList<PipelineTemplate> allTemplates() const;
    PipelineTemplate pipelineTemplate(const QString &id) const;

    // Execution
    QString startPipeline(const QString &pipelineId,
                          const QString &parentSessionId,
                          const QString &userInput);
    void cancelPipeline(const QString &executionId);

    // Query
    PipelineExecution execution(const QString &executionId) const;
    bool isNodeReady(const PipelineExecution &exec, const QString &nodeId) const;

public slots:
    void onChildSessionFinished(const QString &childSessionId);
    void onSessionIdChanged(const QString &oldId, const QString &newId);

signals:
    void pipelineStarted(const QString &executionId);
    void nodeStarted(const QString &executionId, const QString &nodeId,
                     const QString &sessionId);
    void nodeCompleted(const QString &executionId, const QString &nodeId,
                       const QString &output);
    void nodeRetrying(const QString &executionId, const QString &nodeId,
                      int attempt, const QString &error);
    void nodeFailed(const QString &executionId, const QString &nodeId,
                    const QString &error);
    void pipelineCompleted(const QString &executionId);
    void pipelineFailed(const QString &executionId, const QString &error);

private:
    void advanceExecution(const QString &executionId);
    void launchNode(PipelineExecution &exec, const PipelineNode &node);
    void runValidation(const QString &executionId, const QString &nodeId,
                       const QString &command);
    void retryNode(PipelineExecution &exec, const PipelineNode &node);
    QString buildNodePrompt(const PipelineExecution &exec,
                            const PipelineNode &node) const;
    void loadBuiltInTemplates();

    ChatPanel *m_chatPanel = nullptr;
    SessionManager *m_sessionMgr = nullptr;
    Database *m_database = nullptr;
    QString m_workspace;

    QList<PipelineTemplate> m_templates;
    QMap<QString, PipelineExecution> m_executions;

    // sessionId → (executionId, nodeId)
    QMap<QString, QPair<QString, QString>> m_sessionToNode;
};
