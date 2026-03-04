#include "core/PersonalityProfile.h"
#include "util/Config.h"
#include <nlohmann/json.hpp>
#include <QDebug>

ProfileManager &ProfileManager::instance()
{
    static ProfileManager mgr;
    return mgr;
}

ProfileManager::ProfileManager()
{
    loadDefaults();
    loadFromConfig();
}

void ProfileManager::loadDefaults()
{
    m_profiles = {
        {"ui-design-expert", "UI Design Expert",
         "You are a UI/UX design expert. Focus on design principles, layout composition, "
         "typography, color theory, spacing systems, accessibility (WCAG), responsive design, "
         "and design systems. Evaluate interfaces for visual hierarchy, consistency, and usability. "
         "Suggest improvements grounded in established design patterns.",
         QColor("#f5c2e7"), true},

        {"vue3-expert", "Vue3 Expert",
         "You are a Vue 3 expert. Focus on Composition API with <script setup>, reactive state "
         "management with ref/reactive/computed, Pinia stores, Vue Router 4, composables for "
         "reusable logic, provide/inject patterns, Suspense, Teleport, and TypeScript integration. "
         "Prefer single-file components and idiomatic Vue 3 patterns.",
         QColor("#a6e3a1"), true},

        {"backend-node-expert", "Backend (Node.js) Expert",
         "You are a Node.js backend expert. Focus on server architecture, Express/Fastify, "
         "async/await patterns, RESTful and GraphQL API design, middleware composition, "
         "authentication/authorization, error handling, input validation, database integration, "
         "caching strategies, and performance optimization. Write production-grade code.",
         QColor("#89b4fa"), true},

        {"postgres-expert", "PostgreSQL Expert",
         "You are a PostgreSQL expert. Focus on schema design and normalization, query optimization "
         "with EXPLAIN ANALYZE, indexing strategies (B-tree, GIN, GiST, partial), CTEs, window "
         "functions, JSONB operations, migrations, partitioning, connection pooling, and "
         "transaction isolation. Write efficient, correct SQL.",
         QColor("#f9e2af"), true},

        {"cccpp-expert", "CCCPP Expert (Qt+C++)",
         "You are an expert in this Qt6 C++ desktop application (CCCPP / C3P2 Agent Manager). "
         "Focus on Qt6 widgets, signals/slots, QSplitter layouts, QSS theming with Catppuccin "
         "palette tokens, C++17, CMake builds, QProcess for CLI integration, custom paintEvent "
         "rendering, and the project's Manager View / Editor View architecture. Follow existing "
         "patterns in the codebase.",
         QColor("#cba6f7"), true},

        // ─── Specialist Roles ──────────────────────────────────────────
        {"specialist-architect", "Architect",
         "You are a software architect. Your job is to analyze the codebase, design the "
         "solution structure, and produce detailed design documents.\n\n"
         "IMPORTANT RULES:\n"
         "- Do NOT write application code (no .cpp, .js, .ts, .py, etc.)\n"
         "- DO write design documents as .md files in the project workspace directory\n"
         "- Do NOT create plans using the plan tool — write .md files directly using Write\n"
         "- Read and explore the codebase to understand existing patterns\n\n"
         "Your output should include:\n"
         "1. Files to create or modify (with full paths)\n"
         "2. Description of changes needed in each file\n"
         "3. Interfaces and data flow between components\n"
         "4. Order of implementation steps\n"
         "Be specific and actionable.",
         QColor("#89b4fa"), true, "agent", true},

        {"specialist-implementer", "Implementer",
         "You are a code implementer. You receive a plan or task and your job is to "
         "write the actual code changes. Follow the plan precisely. Focus on clean, "
         "working code that matches existing patterns in the codebase. "
         "Make all necessary changes to get the code compiling and working.",
         QColor("#a6e3a1"), true, "agent", true},

        {"specialist-reviewer", "Reviewer",
         "You are a code reviewer. Examine the recent changes in the codebase and provide "
         "a thorough review. Check for:\n"
         "- Bugs and logic errors\n"
         "- Missing edge cases\n"
         "- Style and consistency issues\n"
         "- Architectural concerns\n"
         "Be specific with file paths and line references. Do NOT make changes yourself.",
         QColor("#f9e2af"), true, "ask", true},

        {"specialist-tester", "Tester",
         "You are a test engineer. Your job is to write and run tests for recent changes. "
         "Create test files if needed, run existing test suites, and verify the implementation "
         "works correctly. Report pass/fail status with details on any failures.",
         QColor("#f38ba8"), true, "agent", true},

        {"specialist-orchestrator", "Orchestrator",
         "You are an autonomous project orchestrator. You receive a high-level goal and must "
         "break it down into steps, delegate work to specialist agents, validate results, "
         "and iterate until the goal is fully achieved.\n\n"
         "# How You Act\n\n"
         "You act ONLY by calling the provided tools. Call exactly ONE tool per turn.\n\n"
         "- delegate(role, task) — Assign work to a specialist agent.\n"
         "  Roles: architect (analyzes codebase, writes design docs), "
         "implementer (writes code), reviewer (reviews code), tester (writes/runs tests).\n"
         "- validate(command) — Run a shell command to verify work (build, test, lint).\n"
         "- done(summary) — Report the goal is fully achieved.\n"
         "- fail(reason) — Report unrecoverable failure.\n\n"
         "# Workflow\n\n"
         "1. Delegate to architect to analyze requirements and design the solution.\n"
         "2. Delegate to implementer to write the actual code.\n"
         "3. Validate with build/test commands.\n"
         "4. If validation fails, delegate fixes to implementer and re-validate.\n"
         "5. Delegate to reviewer for code review.\n"
         "6. If reviewer finds critical issues, delegate fixes and re-validate.\n"
         "7. Call done() only after: implement → validate passes → review passes.\n\n"
         "# Rules\n\n"
         "- Call exactly ONE tool per turn. You will receive the result, then call your next tool.\n"
         "- Write detailed, specific task descriptions for specialists.\n"
         "- The architect writes design documents (.md files) to the project directory.\n"
         "- The implementer writes actual application code.\n"
         "- Always validate after implementation.\n"
         "- Think step by step. Explain your reasoning briefly before each tool call.",
         QColor("#cba6f7"), true, "orchestrator", true},
    };
}

void ProfileManager::loadFromConfig()
{
    auto &cfg = Config::instance();
    auto &data = cfg.rawData();

    // Load user profiles (merge with defaults)
    if (data.contains("profiles") && data["profiles"].is_array()) {
        for (const auto &j : data["profiles"]) {
            QString id = QString::fromStdString(j.value("id", ""));
            if (id.isEmpty()) continue;

            PersonalityProfile p;
            p.id = id;
            p.name = QString::fromStdString(j.value("name", ""));
            p.promptText = QString::fromStdString(j.value("prompt_text", ""));
            p.color = QColor(QString::fromStdString(j.value("color", "#cdd6f4")));
            p.builtIn = j.value("built_in", false);
            p.enforcedMode = QString::fromStdString(j.value("enforced_mode", ""));
            p.isSpecialistRole = j.value("is_specialist_role", false);

            // For built-ins, update prompt text only (user may have customized it)
            bool found = false;
            for (auto &existing : m_profiles) {
                if (existing.id == id) {
                    existing.promptText = p.promptText;
                    if (!p.name.isEmpty()) existing.name = p.name;
                    existing.color = p.color;
                    found = true;
                    break;
                }
            }
            if (!found)
                m_profiles.append(p);
        }
    }

    // Load workspace specs
    if (data.contains("workspace_specs") && data["workspace_specs"].is_object()) {
        for (auto it = data["workspace_specs"].begin(); it != data["workspace_specs"].end(); ++it) {
            WorkspaceSpec spec;
            spec.workspace = QString::fromStdString(it.key());
            spec.name = QString::fromStdString(it.value().value("name", ""));
            spec.specText = QString::fromStdString(it.value().value("spec_text", ""));
            m_workspaceSpecs[spec.workspace] = spec;
        }
    }
}

void ProfileManager::saveToConfig()
{
    auto &cfg = Config::instance();
    auto &data = cfg.rawData();

    // Save profiles
    auto arr = nlohmann::json::array();
    for (const auto &p : m_profiles) {
        arr.push_back({
            {"id", p.id.toStdString()},
            {"name", p.name.toStdString()},
            {"prompt_text", p.promptText.toStdString()},
            {"color", p.color.name().toStdString()},
            {"built_in", p.builtIn},
            {"enforced_mode", p.enforcedMode.toStdString()},
            {"is_specialist_role", p.isSpecialistRole}
        });
    }
    data["profiles"] = arr;

    // Save workspace specs
    auto obj = nlohmann::json::object();
    for (auto it = m_workspaceSpecs.constBegin(); it != m_workspaceSpecs.constEnd(); ++it) {
        obj[it.key().toStdString()] = {
            {"name", it->name.toStdString()},
            {"spec_text", it->specText.toStdString()}
        };
    }
    data["workspace_specs"] = obj;

    cfg.save();
}

QList<PersonalityProfile> ProfileManager::allProfiles() const
{
    return m_profiles;
}

PersonalityProfile ProfileManager::profile(const QString &id) const
{
    for (const auto &p : m_profiles) {
        if (p.id == id) return p;
    }
    return {};
}

void ProfileManager::addProfile(const PersonalityProfile &p)
{
    m_profiles.append(p);
    saveToConfig();
    emit profilesChanged();
}

void ProfileManager::updateProfile(const PersonalityProfile &p)
{
    for (auto &existing : m_profiles) {
        if (existing.id == p.id) {
            existing.name = p.name;
            existing.promptText = p.promptText;
            existing.color = p.color;
            break;
        }
    }
    saveToConfig();
    emit profilesChanged();
}

void ProfileManager::removeProfile(const QString &id)
{
    for (int i = 0; i < m_profiles.size(); ++i) {
        if (m_profiles[i].id == id && !m_profiles[i].builtIn) {
            m_profiles.removeAt(i);
            break;
        }
    }
    saveToConfig();
    emit profilesChanged();
}

WorkspaceSpec ProfileManager::workspaceSpec(const QString &workspace) const
{
    return m_workspaceSpecs.value(workspace);
}

void ProfileManager::setWorkspaceSpec(const WorkspaceSpec &spec)
{
    m_workspaceSpecs[spec.workspace] = spec;
    saveToConfig();
    emit workspaceSpecChanged(spec.workspace);
}

void ProfileManager::removeWorkspaceSpec(const QString &workspace)
{
    m_workspaceSpecs.remove(workspace);
    saveToConfig();
    emit workspaceSpecChanged(workspace);
}

bool ProfileManager::hasWorkspaceSpec(const QString &workspace) const
{
    return m_workspaceSpecs.contains(workspace) &&
           !m_workspaceSpecs[workspace].specText.trimmed().isEmpty();
}

QString ProfileManager::buildSystemPrompt(const QString &workspace,
                                           const QStringList &profileIds) const
{
    QStringList sections;

    // Workspace spec (if exists for this workspace)
    if (hasWorkspaceSpec(workspace)) {
        const auto &spec = m_workspaceSpecs[workspace];
        QString header = spec.name.isEmpty() ? workspace : spec.name;
        sections << QStringLiteral("=== Workspace: %1 ===\n%2").arg(header, spec.specText);
    }

    // Selected profiles
    for (const auto &id : profileIds) {
        auto p = profile(id);
        if (!p.id.isEmpty() && !p.promptText.trimmed().isEmpty()) {
            QString section = QStringLiteral("=== %1 ===\n%2").arg(p.name, p.promptText);
            if (!p.enforcedMode.isEmpty())
                section += QStringLiteral("\n\nIMPORTANT: You are operating in %1 mode.").arg(p.enforcedMode);
            sections << section;
        }
    }

    if (sections.isEmpty())
        return {};

    return sections.join("\n\n");
}
