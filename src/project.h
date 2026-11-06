/**
 * project.h – JetBrains-style Project System for cgui
 *
 * Supports:
 *   - Auto-detection of project type & build system (Meson, CMake, Make, Cargo, Go, Zig, Node, Python)
 *   - Run / Build configurations engine with targets (Build, Run, Test, Clean)
 *   - Per-project manifest configuration via .cgui/project.json or cgui.json
 *   - Execution directly inside the integrated ANSI terminal
 *   - Project scaffolding templates for rapid project creation
 */

#ifndef CGUI_PROJECT_H
#define CGUI_PROJECT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define PROJECT_MAX_TARGETS 16
#define PROJECT_NAME_MAX 128
#define PROJECT_CMD_MAX 256

typedef enum ProjectType {
    PROJECT_TYPE_NONE = 0,
    PROJECT_TYPE_MESON,
    PROJECT_TYPE_CMAKE,
    PROJECT_TYPE_MAKE,
    PROJECT_TYPE_CARGO,
    PROJECT_TYPE_GO,
    PROJECT_TYPE_ZIG,
    PROJECT_TYPE_NODE,
    PROJECT_TYPE_PYTHON,
    PROJECT_TYPE_CUSTOM,
} ProjectType;

typedef struct ProjectTarget {
    char name[64];       /* e.g. "Build", "Run", "Test", "Clean" */
    char cmd[PROJECT_CMD_MAX]; /* Shell command, e.g. "meson compile -C build" */
    char shortcut[32];  /* e.g. "Ctrl+B", "Shift+F10" */
    bool is_build;      /* true if target is a compilation task */
} ProjectTarget;

typedef struct ProjectConfig {
    ProjectType   type;
    char          type_name[32];    /* e.g. "Meson", "CMake", "Cargo" */
    char          display_name[PROJECT_NAME_MAX];
    char          toolchain[64];    /* e.g. "clang / gcc", "cargo / rustc" */
    ProjectTarget targets[PROJECT_MAX_TARGETS];
    int           target_count;
    int           active_target;
    char          last_executed[64];
    uint64_t      last_executed_ms;
    bool          is_detected;
} ProjectConfig;

struct App;

/** Initialize or refresh project configuration from directory */
void project_config_init(ProjectConfig *cfg);
void project_config_detect(ProjectConfig *cfg, const char *root_path);
bool project_config_load_manifest(ProjectConfig *cfg, const char *root_path);
bool project_config_save_manifest(const ProjectConfig *cfg, const char *root_path);

/** Execution */
bool project_config_run_target(struct App *app, int target_index);
bool project_config_run_active(struct App *app);
bool project_config_run_build(struct App *app);

/** Target navigation */
void project_config_next_target(ProjectConfig *cfg);
void project_config_prev_target(ProjectConfig *cfg);
const char *project_config_active_summary(const ProjectConfig *cfg, char *buf, size_t sz);

/** Project Template Scaffolding */
typedef enum ProjectTemplate {
    PROJECT_TEMPLATE_C_MESON = 0,
    PROJECT_TEMPLATE_C_MAKE,
    PROJECT_TEMPLATE_C_CMAKE,
    PROJECT_TEMPLATE_RUST_CARGO,
    PROJECT_TEMPLATE_GO,
    PROJECT_TEMPLATE_PYTHON,
    PROJECT_TEMPLATE_ZIG,
    PROJECT_TEMPLATE_CUSTOM,
    PROJECT_TEMPLATE_COUNT
} ProjectTemplate;

typedef struct ProjectTemplateInfo {
    ProjectTemplate tmpl;
    const char *name;          /* e.g. "C / C++ (Meson)" */
    const char *tag;           /* e.g. "Meson" */
    const char *toolchain;     /* e.g. "Meson + Ninja" */
    const char *standard;      /* e.g. "C23" */
    const char *desc;          /* e.g. "Fast modern C23 build system with automatic run targets" */
    const char *entry_file;    /* e.g. "src/main.c" */
    const char *default_name;  /* e.g. "my_c_app" */
} ProjectTemplateInfo;

const ProjectTemplateInfo *project_template_get_info(ProjectTemplate tmpl);
const char *project_template_entry_file(ProjectTemplate tmpl);
bool project_scaffold(const char *dest_dir, ProjectTemplate tmpl, const char *proj_name);

#endif /* CGUI_PROJECT_H */
