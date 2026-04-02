#pragma once
#include <string>
#include <vector>
#include <functional>

class Engine;

struct ProjectInfo {
    std::string name;
    std::string path;
    std::string version;
    std::string defaultChart;
    std::string shaderPath;
};

class ProjectHub {
public:
    using LaunchCallback = std::function<void(const ProjectInfo&)>;

    void setLaunchCallback(LaunchCallback cb) { m_launchCallback = cb; }
    void render(Engine* engine);
    bool hasSelectedProject() const { return m_projectSelected; }
    const ProjectInfo& getSelectedProject() const { return m_selectedProject; }

private:
    void scanProjects();
    void renderCreateDialog(Engine* engine);
    bool createProject(const std::string& name);

    std::vector<ProjectInfo> m_projects;
    ProjectInfo              m_selectedProject;
    LaunchCallback           m_launchCallback;
    bool                     m_scanned         = false;
    bool                     m_projectSelected = false;

    bool        m_showCreateDialog = false;
    char        m_newProjectName[128] = {};
    std::string m_createError;
};
