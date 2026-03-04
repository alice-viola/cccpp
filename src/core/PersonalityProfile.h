#pragma once

#include <QObject>
#include <QColor>
#include <QList>
#include <QMap>
#include <QString>
#include <QStringList>

struct PersonalityProfile {
    QString id;
    QString name;
    QString promptText;
    QColor  color;
    bool    builtIn = false;

    // Specialist role fields
    QString enforcedMode;          // "agent", "ask", "plan", or "" (user's choice)
    bool    isSpecialistRole = false;
};

struct WorkspaceSpec {
    QString workspace;   // absolute path
    QString name;        // display name
    QString specText;    // the spec/rules content
};

class ProfileManager : public QObject {
    Q_OBJECT
public:
    static ProfileManager &instance();

    // Profiles
    QList<PersonalityProfile> allProfiles() const;
    PersonalityProfile profile(const QString &id) const;
    void addProfile(const PersonalityProfile &p);
    void updateProfile(const PersonalityProfile &p);
    void removeProfile(const QString &id);

    // Workspace specs
    WorkspaceSpec workspaceSpec(const QString &workspace) const;
    void setWorkspaceSpec(const WorkspaceSpec &spec);
    void removeWorkspaceSpec(const QString &workspace);
    bool hasWorkspaceSpec(const QString &workspace) const;

    // Combined prompt builder
    QString buildSystemPrompt(const QString &workspace,
                              const QStringList &profileIds) const;

signals:
    void profilesChanged();
    void workspaceSpecChanged(const QString &workspace);

private:
    ProfileManager();
    void loadDefaults();
    void loadFromConfig();
    void saveToConfig();

    QList<PersonalityProfile> m_profiles;
    QMap<QString, WorkspaceSpec> m_workspaceSpecs;
};
