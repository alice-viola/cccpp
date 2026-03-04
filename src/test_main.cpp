// Integration test for Pipeline/Orchestrator infrastructure
// Tests the data layer (profiles, sessions, templates) without requiring
// full ChatPanel + UI stack.

#include "core/PersonalityProfile.h"
#include "core/SessionManager.h"
#include "core/PipelineEngine.h"
#include <QCoreApplication>
#include <QDebug>

int main(int argc, char *argv[]) {
    QCoreApplication app(argc, argv);

    // ─── Specialist Profiles ───
    auto &pm = ProfileManager::instance();

    auto architect = pm.profile("specialist-architect");
    Q_ASSERT(!architect.id.isEmpty());
    Q_ASSERT(architect.enforcedMode == "agent");
    Q_ASSERT(architect.isSpecialistRole);
    qDebug() << "[PASS] Architect profile: mode=agent";

    auto implementer = pm.profile("specialist-implementer");
    Q_ASSERT(implementer.enforcedMode == "agent");
    qDebug() << "[PASS] Implementer profile: mode=agent";

    auto reviewer = pm.profile("specialist-reviewer");
    Q_ASSERT(reviewer.enforcedMode == "ask");
    qDebug() << "[PASS] Reviewer profile: mode=ask";

    auto tester = pm.profile("specialist-tester");
    Q_ASSERT(tester.enforcedMode == "agent");
    qDebug() << "[PASS] Tester profile: mode=agent";

    auto orch = pm.profile("specialist-orchestrator");
    Q_ASSERT(!orch.id.isEmpty());
    Q_ASSERT(orch.isSpecialistRole);
    qDebug() << "[PASS] Orchestrator profile exists";

    // ─── System Prompt Enforcement ───
    QString prompt = pm.buildSystemPrompt("/tmp", {"specialist-reviewer"});
    Q_ASSERT(prompt.contains("operating in ask mode"));
    qDebug() << "[PASS] System prompt includes mode enforcement";

    prompt = pm.buildSystemPrompt("/tmp", {"specialist-architect"});
    Q_ASSERT(prompt.contains("operating in agent mode"));
    qDebug() << "[PASS] Architect prompt enforces agent mode";

    // ─── Session Hierarchy ───
    SessionManager sm;
    QString parentId = sm.createSession("/tmp", "agent");
    Q_ASSERT(!parentId.isEmpty());

    QString childId = sm.createChildSession(parentId, "/tmp", "plan", "Design the API");
    Q_ASSERT(!childId.isEmpty());

    auto childInfo = sm.sessionInfo(childId);
    Q_ASSERT(childInfo.parentSessionId == parentId);
    Q_ASSERT(childInfo.delegationTask == "Design the API");
    Q_ASSERT(childInfo.delegationStatus == SessionInfo::Pending);
    qDebug() << "[PASS] Child session created with hierarchy";

    sm.setDelegationStatus(childId, SessionInfo::Running);
    Q_ASSERT(sm.sessionInfo(childId).delegationStatus == SessionInfo::Running);
    qDebug() << "[PASS] Delegation status updates work";

    auto children = sm.childSessions(parentId);
    Q_ASSERT(children.size() == 1);
    Q_ASSERT(children[0].sessionId == childId);
    qDebug() << "[PASS] childSessions() returns correct children";

    // Create grandchild
    QString grandchildId = sm.createChildSession(childId, "/tmp", "agent", "Implement module A");
    auto grandchild = sm.sessionInfo(grandchildId);
    Q_ASSERT(grandchild.parentSessionId == childId);
    qDebug() << "[PASS] Grandchild (depth 2) session works";

    // ─── Pipeline Templates ───
    PipelineEngine engine;
    auto templates = engine.allTemplates();
    Q_ASSERT(templates.size() >= 2);
    qDebug() << "[PASS]" << templates.size() << "built-in pipeline templates";

    auto refactor = engine.pipelineTemplate("builtin-refactor");
    Q_ASSERT(!refactor.pipelineId.isEmpty());
    Q_ASSERT(refactor.nodes.size() == 4);
    qDebug() << "[PASS] Refactor pipeline: 4 nodes";

    // Verify node structure
    Q_ASSERT(refactor.nodes[0].nodeId == "architect");
    Q_ASSERT(refactor.nodes[0].dependsOn.isEmpty());
    Q_ASSERT(refactor.nodes[1].nodeId == "implementer");
    Q_ASSERT(refactor.nodes[1].dependsOn.contains("architect"));
    Q_ASSERT(!refactor.nodes[1].validationCommand.isEmpty());
    Q_ASSERT(refactor.nodes[1].maxRetries == 3);
    Q_ASSERT(refactor.nodes[2].nodeId == "reviewer");
    Q_ASSERT(refactor.nodes[2].dependsOn.contains("implementer"));
    Q_ASSERT(refactor.nodes[3].nodeId == "tester");
    Q_ASSERT(refactor.nodes[3].dependsOn.contains("implementer"));
    qDebug() << "[PASS] Node dependencies: architect->implementer->[reviewer,tester]";
    qDebug() << "[PASS] Implementer has validation:" << refactor.nodes[1].validationCommand;

    auto review = engine.pipelineTemplate("builtin-review");
    Q_ASSERT(review.nodes.size() == 2);
    qDebug() << "[PASS] Review pipeline: 2 nodes";

    // ─── DAG Dependency Resolution ───
    PipelineExecution exec;
    exec.pipelineId = "builtin-refactor";
    exec.nodeStates["architect"] = PipelineExecution::Waiting;
    exec.nodeStates["implementer"] = PipelineExecution::Waiting;
    exec.nodeStates["reviewer"] = PipelineExecution::Waiting;
    exec.nodeStates["tester"] = PipelineExecution::Waiting;

    // architect has no deps -> ready
    Q_ASSERT(engine.isNodeReady(exec, "architect"));
    // implementer depends on architect (Waiting) -> not ready
    Q_ASSERT(!engine.isNodeReady(exec, "implementer"));
    // reviewer depends on implementer (Waiting) -> not ready
    Q_ASSERT(!engine.isNodeReady(exec, "reviewer"));
    qDebug() << "[PASS] Initial: only architect is ready";

    exec.nodeStates["architect"] = PipelineExecution::Completed;
    Q_ASSERT(engine.isNodeReady(exec, "implementer"));
    Q_ASSERT(!engine.isNodeReady(exec, "reviewer"));
    qDebug() << "[PASS] After architect completes: implementer becomes ready";

    exec.nodeStates["implementer"] = PipelineExecution::Completed;
    Q_ASSERT(engine.isNodeReady(exec, "reviewer"));
    Q_ASSERT(engine.isNodeReady(exec, "tester"));
    qDebug() << "[PASS] After implementer completes: reviewer AND tester both ready (parallel)";

    qDebug() << "\n=== ALL 19 TESTS PASSED ===";
    return 0;
}
