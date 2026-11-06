/**
 * project.c – JetBrains-style Project System for cgui
 */

#include "project.h"
#include "app.h"
#include "terminal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include <SDL3/SDL.h>
#include <SDL3/SDL_filesystem.h>

static bool file_exists_in(const char *dir, const char *filename)
{
    char path[1024];
    int n = snprintf(path, sizeof(path), "%s/%s", dir, filename);
    if (n < 0 || (size_t)n >= sizeof(path)) return false;

    SDL_PathInfo info;
    return SDL_GetPathInfo(path, &info) && info.type == SDL_PATHTYPE_FILE;
}

static void add_target(ProjectConfig *cfg, const char *name, const char *cmd,
                       const char *shortcut, bool is_build)
{
    if (cfg->target_count >= PROJECT_MAX_TARGETS) return;
    ProjectTarget *t = &cfg->targets[cfg->target_count++];
    snprintf(t->name, sizeof(t->name), "%s", name);
    snprintf(t->cmd, sizeof(t->cmd), "%s", cmd);
    snprintf(t->shortcut, sizeof(t->shortcut), "%s", shortcut ? shortcut : "");
    t->is_build = is_build;
}

void project_config_init(ProjectConfig *cfg)
{
    if (!cfg) return;
    memset(cfg, 0, sizeof(*cfg));
    cfg->type = PROJECT_TYPE_NONE;
    snprintf(cfg->type_name, sizeof(cfg->type_name), "Generic");
    cfg->active_target = 0;
}

void project_config_detect(ProjectConfig *cfg, const char *root_path)
{
    if (!cfg) return;
    project_config_init(cfg);
    if (!root_path || !root_path[0]) return;

    /* Extract root folder display name */
    const char *last_slash = strrchr(root_path, '/');
    const char *base = last_slash ? last_slash + 1 : root_path;
    snprintf(cfg->display_name, sizeof(cfg->display_name), "%s", base[0] ? base : "Project");

    /* 1. Meson Project */
    if (file_exists_in(root_path, "meson.build")) {
        cfg->type = PROJECT_TYPE_MESON;
        snprintf(cfg->type_name, sizeof(cfg->type_name), "Meson");
        snprintf(cfg->toolchain, sizeof(cfg->toolchain), "C/C++ (meson / ninja)");
        add_target(cfg, "Build", "meson compile -C build", "Ctrl+B", true);
        add_target(cfg, "Setup", "meson setup build", "", false);
        add_target(cfg, "Test", "meson test -C build", "", false);
        add_target(cfg, "Clean", "ninja -C build clean", "", false);
        cfg->is_detected = true;
    }
    /* 2. CMake Project */
    else if (file_exists_in(root_path, "CMakeLists.txt")) {
        cfg->type = PROJECT_TYPE_CMAKE;
        snprintf(cfg->type_name, sizeof(cfg->type_name), "CMake");
        snprintf(cfg->toolchain, sizeof(cfg->toolchain), "C/C++ (cmake / ninja)");
        add_target(cfg, "Build", "cmake --build build", "Ctrl+B", true);
        add_target(cfg, "Configure", "cmake -B build", "", false);
        add_target(cfg, "Test", "ctest --test-dir build", "", false);
        add_target(cfg, "Clean", "cmake --build build --target clean", "", false);
        cfg->is_detected = true;
    }
    /* 3. Makefile Project */
    else if (file_exists_in(root_path, "Makefile") || file_exists_in(root_path, "makefile")) {
        cfg->type = PROJECT_TYPE_MAKE;
        snprintf(cfg->type_name, sizeof(cfg->type_name), "Make");
        snprintf(cfg->toolchain, sizeof(cfg->toolchain), "Make (GNU / POSIX)");
        add_target(cfg, "Build", "make", "Ctrl+B", true);
        add_target(cfg, "Run", "make run", "Shift+F10", false);
        add_target(cfg, "Test", "make test", "", false);
        add_target(cfg, "Clean", "make clean", "", false);
        cfg->is_detected = true;
    }
    /* 4. Rust Cargo Project */
    else if (file_exists_in(root_path, "Cargo.toml")) {
        cfg->type = PROJECT_TYPE_CARGO;
        snprintf(cfg->type_name, sizeof(cfg->type_name), "Cargo");
        snprintf(cfg->toolchain, sizeof(cfg->toolchain), "Rust (cargo / rustc)");
        add_target(cfg, "Run", "cargo run", "Shift+F10", false);
        add_target(cfg, "Build", "cargo build", "Ctrl+B", true);
        add_target(cfg, "Test", "cargo test", "", false);
        add_target(cfg, "Check", "cargo check", "", false);
        cfg->is_detected = true;
    }
    /* 5. Go Project */
    else if (file_exists_in(root_path, "go.mod")) {
        cfg->type = PROJECT_TYPE_GO;
        snprintf(cfg->type_name, sizeof(cfg->type_name), "Go");
        snprintf(cfg->toolchain, sizeof(cfg->toolchain), "Go (go toolchain)");
        add_target(cfg, "Run", "go run .", "Shift+F10", false);
        add_target(cfg, "Build", "go build -v .", "Ctrl+B", true);
        add_target(cfg, "Test", "go test -v ./...", "", false);
        cfg->is_detected = true;
    }
    /* 6. Zig Project */
    else if (file_exists_in(root_path, "build.zig")) {
        cfg->type = PROJECT_TYPE_ZIG;
        snprintf(cfg->type_name, sizeof(cfg->type_name), "Zig");
        snprintf(cfg->toolchain, sizeof(cfg->toolchain), "Zig (zig build)");
        add_target(cfg, "Run", "zig build run", "Shift+F10", false);
        add_target(cfg, "Build", "zig build", "Ctrl+B", true);
        add_target(cfg, "Test", "zig build test", "", false);
        cfg->is_detected = true;
    }
    /* 7. Node / Web Project */
    else if (file_exists_in(root_path, "package.json")) {
        cfg->type = PROJECT_TYPE_NODE;
        snprintf(cfg->type_name, sizeof(cfg->type_name), "Node.js");
        snprintf(cfg->toolchain, sizeof(cfg->toolchain), "Node.js (npm / pnpm / bun)");
        add_target(cfg, "Start", "npm start", "Shift+F10", false);
        add_target(cfg, "Build", "npm run build", "Ctrl+B", true);
        add_target(cfg, "Test", "npm test", "", false);
        cfg->is_detected = true;
    }
    /* 8. Python Project */
    else if (file_exists_in(root_path, "pyproject.toml") ||
             file_exists_in(root_path, "requirements.txt") ||
             file_exists_in(root_path, "main.py")) {
        cfg->type = PROJECT_TYPE_PYTHON;
        snprintf(cfg->type_name, sizeof(cfg->type_name), "Python");
        snprintf(cfg->toolchain, sizeof(cfg->toolchain), "Python (python3)");
        add_target(cfg, "Run", "python3 main.py", "Shift+F10", false);
        add_target(cfg, "Test", "pytest", "", false);
        cfg->is_detected = true;
    }

    /* Check for custom .cgui/project.json or cgui.json manifest override */
    project_config_load_manifest(cfg, root_path);

    /* If no targets detected, provide generic fallback */
    if (cfg->target_count == 0) {
        cfg->type = PROJECT_TYPE_CUSTOM;
        snprintf(cfg->type_name, sizeof(cfg->type_name), "Workspace");
        add_target(cfg, "Shell", "$SHELL", "", false);
    }
}

bool project_config_load_manifest(ProjectConfig *cfg, const char *root_path)
{
    if (!cfg || !root_path) return false;

    char path[1024];
    snprintf(path, sizeof(path), "%s/.cgui/project.json", root_path);
    FILE *f = fopen(path, "rb");
    if (!f) {
        snprintf(path, sizeof(path), "%s/cgui.json", root_path);
        f = fopen(path, "rb");
    }
    if (!f) return false;

    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz <= 0 || sz > 65536) {
        fclose(f);
        return false;
    }

    char *buf = malloc((size_t)sz + 1);
    if (!buf) {
        fclose(f);
        return false;
    }
    size_t rd = fread(buf, 1, (size_t)sz, f);
    fclose(f);
    buf[rd] = 0;

    /* Parse custom manifest entries */
    char *name_key = strstr(buf, "\"name\":");
    if (name_key) {
        char *q1 = strchr(name_key + 7, '"');
        if (q1) {
            char *q2 = strchr(q1 + 1, '"');
            if (q2 && (q2 - q1 - 1) < (long)sizeof(cfg->display_name)) {
                snprintf(cfg->display_name, (size_t)(q2 - q1), "%s", q1 + 1);
            }
        }
    }

    /* Check for targets array */
    char *targets_arr = strstr(buf, "\"targets\":");
    if (targets_arr) {
        /* Clear auto-detected targets if custom targets specified */
        cfg->target_count = 0;
        char *cur = targets_arr;
        while ((cur = strstr(cur, "\"cmd\":")) != NULL) {
            char *cmd_q1 = strchr(cur + 6, '"');
            if (!cmd_q1) break;
            char *cmd_q2 = strchr(cmd_q1 + 1, '"');
            if (!cmd_q2) break;

            char cmd[PROJECT_CMD_MAX];
            size_t cmd_len = (size_t)(cmd_q2 - cmd_q1 - 1);
            if (cmd_len >= sizeof(cmd)) cmd_len = sizeof(cmd) - 1;
            memcpy(cmd, cmd_q1 + 1, cmd_len);
            cmd[cmd_len] = 0;

            char name[64] = "Task";
            char *name_ptr = strstr(cur - 40 > buf ? cur - 40 : buf, "\"name\":");
            if (name_ptr && name_ptr < cur) {
                char *nq1 = strchr(name_ptr + 7, '"');
                if (nq1) {
                    char *nq2 = strchr(nq1 + 1, '"');
                    if (nq2 && (nq2 - nq1 - 1) < (long)sizeof(name)) {
                        snprintf(name, (size_t)(nq2 - nq1), "%s", nq1 + 1);
                    }
                }
            }

            bool is_build = (strstr(cmd, "build") != NULL || strstr(cmd, "compile") != NULL);
            add_target(cfg, name, cmd, is_build ? "Ctrl+B" : NULL, is_build);
            cur = cmd_q2 + 1;
        }
        cfg->type = PROJECT_TYPE_CUSTOM;
    }

    free(buf);
    return true;
}

bool project_config_save_manifest(const ProjectConfig *cfg, const char *root_path)
{
    if (!cfg || !root_path || !root_path[0]) return false;

    char dir[1024];
    snprintf(dir, sizeof(dir), "%s/.cgui", root_path);
    (void)mkdir(dir, 0755);

    char path[1024];
    snprintf(path, sizeof(path), "%s/.cgui/project.json", root_path);
    FILE *f = fopen(path, "w");
    if (!f) return false;

    fprintf(f, "{\n  \"name\": \"%s\",\n  \"type\": \"%s\",\n  \"targets\": [\n",
            cfg->display_name, cfg->type_name);
    for (int i = 0; i < cfg->target_count; ++i) {
        const ProjectTarget *t = &cfg->targets[i];
        fprintf(f, "    {\n      \"name\": \"%s\",\n      \"cmd\": \"%s\"\n    }%s\n",
                t->name, t->cmd, (i + 1 < cfg->target_count) ? "," : "");
    }
    fprintf(f, "  ]\n}\n");
    fclose(f);
    return true;
}

bool project_config_run_target(App *app, int target_index)
{
    if (!app) return false;
    ProjectConfig *cfg = &app->project_cfg;
    if (target_index < 0 || target_index >= cfg->target_count) return false;

    ProjectTarget *t = &cfg->targets[target_index];
    cfg->active_target = target_index;
    snprintf(cfg->last_executed, sizeof(cfg->last_executed), "%s", t->name);
    cfg->last_executed_ms = SDL_GetTicks();

    /* 1. Ensure terminal is visible */
    if (!app->terminal_open) {
        app->terminal_open = true;
    }
    app->terminal.focused = true;

    /* 2. Execute command in integrated terminal */
    char full_cmd[PROJECT_CMD_MAX + 4];
    snprintf(full_cmd, sizeof(full_cmd), "%s\n", t->cmd);
    terminal_send_input(&app->terminal, full_cmd, strlen(full_cmd));

    return true;
}

bool project_config_run_active(App *app)
{
    if (!app) return false;
    return project_config_run_target(app, app->project_cfg.active_target);
}

bool project_config_run_build(App *app)
{
    if (!app) return false;
    ProjectConfig *cfg = &app->project_cfg;
    for (int i = 0; i < cfg->target_count; ++i) {
        if (cfg->targets[i].is_build) {
            return project_config_run_target(app, i);
        }
    }
    return project_config_run_active(app);
}

void project_config_next_target(ProjectConfig *cfg)
{
    if (!cfg || cfg->target_count == 0) return;
    cfg->active_target = (cfg->active_target + 1) % cfg->target_count;
}

void project_config_prev_target(ProjectConfig *cfg)
{
    if (!cfg || cfg->target_count == 0) return;
    cfg->active_target = (cfg->active_target - 1 + cfg->target_count) % cfg->target_count;
}

const char *project_config_active_summary(const ProjectConfig *cfg, char *buf, size_t sz)
{
    if (!cfg || !buf || sz == 0) return "";
    if (cfg->target_count == 0) {
        snprintf(buf, sz, "%s", cfg->type_name);
        return buf;
    }
    const ProjectTarget *t = &cfg->targets[cfg->active_target];
    snprintf(buf, sz, "%s: %s", cfg->type_name, t->name);
    return buf;
}

static const ProjectTemplateInfo g_templates[PROJECT_TEMPLATE_COUNT] = {
    [PROJECT_TEMPLATE_C_MESON] = {
        .tmpl = PROJECT_TEMPLATE_C_MESON,
        .name = "C / C++ (Meson)",
        .tag = "Meson",
        .toolchain = "Meson + Ninja",
        .standard = "C23",
        .desc = "Modern high-performance build system with instant compile times and ninja generation.",
        .entry_file = "src/main.c",
        .default_name = "meson_app",
    },
    [PROJECT_TEMPLATE_C_MAKE] = {
        .tmpl = PROJECT_TEMPLATE_C_MAKE,
        .name = "C / C++ (Makefile)",
        .tag = "Make",
        .toolchain = "GCC / Clang + GNU Make",
        .standard = "C23 / POSIX",
        .desc = "Classic Makefile with all, run, clean, and test rules.",
        .entry_file = "main.c",
        .default_name = "make_app",
    },
    [PROJECT_TEMPLATE_C_CMAKE] = {
        .tmpl = PROJECT_TEMPLATE_C_CMAKE,
        .name = "C / C++ (CMake)",
        .tag = "CMake",
        .toolchain = "CMake 3.20+ (Ninja / Make)",
        .standard = "C23",
        .desc = "Industry standard cross-platform build automation.",
        .entry_file = "src/main.c",
        .default_name = "cmake_app",
    },
    [PROJECT_TEMPLATE_RUST_CARGO] = {
        .tmpl = PROJECT_TEMPLATE_RUST_CARGO,
        .name = "Rust (Cargo)",
        .tag = "Cargo",
        .toolchain = "Rustc + Cargo",
        .standard = "Rust 2021/2024",
        .desc = "Safe, concurrent systems language with cargo run, build, and test.",
        .entry_file = "src/main.rs",
        .default_name = "rust_app",
    },
    [PROJECT_TEMPLATE_GO] = {
        .tmpl = PROJECT_TEMPLATE_GO,
        .name = "Go Module",
        .tag = "Go",
        .toolchain = "Go Toolchain",
        .standard = "Go 1.22+",
        .desc = "Simplicity and concurrency with standard go.mod and main package.",
        .entry_file = "main.go",
        .default_name = "go_app",
    },
    [PROJECT_TEMPLATE_PYTHON] = {
        .tmpl = PROJECT_TEMPLATE_PYTHON,
        .name = "Python Application",
        .tag = "Python",
        .toolchain = "Python 3 + pytest",
        .standard = "Python 3.10+",
        .desc = "Clean Python project with entry point script and dependencies manifest.",
        .entry_file = "main.py",
        .default_name = "py_app",
    },
    [PROJECT_TEMPLATE_ZIG] = {
        .tmpl = PROJECT_TEMPLATE_ZIG,
        .name = "Zig Application",
        .tag = "Zig",
        .toolchain = "Zig Compiler",
        .standard = "Zig 0.13+",
        .desc = "Robust, modern systems programming with build.zig.",
        .entry_file = "src/main.zig",
        .default_name = "zig_app",
    },
    [PROJECT_TEMPLATE_CUSTOM] = {
        .tmpl = PROJECT_TEMPLATE_CUSTOM,
        .name = "Custom / Empty Workspace",
        .tag = "Custom",
        .toolchain = "User Defined",
        .standard = "Custom",
        .desc = "Empty workspace scaffolded with .cgui/project.json configuration.",
        .entry_file = "README.md",
        .default_name = "workspace",
    },
};

const ProjectTemplateInfo *project_template_get_info(ProjectTemplate tmpl)
{
    if (tmpl < 0 || tmpl >= PROJECT_TEMPLATE_COUNT)
        return &g_templates[0];
    return &g_templates[tmpl];
}

const char *project_template_entry_file(ProjectTemplate tmpl)
{
    const ProjectTemplateInfo *info = project_template_get_info(tmpl);
    return info->entry_file ? info->entry_file : "src/main.c";
}

bool project_scaffold(const char *dest_dir, ProjectTemplate tmpl, const char *proj_name)
{
    if (!dest_dir || !dest_dir[0]) return false;
    const char *name = (proj_name && proj_name[0]) ? proj_name : "myapp";

    (void)mkdir(dest_dir, 0755);

    char path[1024];

    if (tmpl == PROJECT_TEMPLATE_C_MESON) {
        snprintf(path, sizeof(path), "%s/meson.build", dest_dir);
        FILE *f = fopen(path, "w");
        if (f) {
            fprintf(f, "project('%s', 'c', version: '0.1.0', default_options: ['warning_level=2', 'c_std=c23'])\n\n"
                       "src = files('src/main.c')\n\n"
                       "executable('%s', src, install: false)\n", name, name);
            fclose(f);
        }

        snprintf(path, sizeof(path), "%s/src", dest_dir);
        (void)mkdir(path, 0755);
        snprintf(path, sizeof(path), "%s/src/main.c", dest_dir);
        f = fopen(path, "w");
        if (f) {
            fprintf(f, "#include <stdio.h>\n\nint main(void) {\n    printf(\"Hello, %s!\\n\");\n    return 0;\n}\n", name);
            fclose(f);
        }
        return true;
    }
    else if (tmpl == PROJECT_TEMPLATE_C_MAKE) {
        snprintf(path, sizeof(path), "%s/Makefile", dest_dir);
        FILE *f = fopen(path, "w");
        if (f) {
            fprintf(f, "CC ?= gcc\nCFLAGS ?= -Wall -Wextra -O2\n\nall: %s\n\n%s: main.c\n\t$(CC) $(CFLAGS) -o $@ $<\n\nrun: %s\n\t./%s\n\nclean:\n\trm -f %s\n",
                    name, name, name, name, name);
            fclose(f);
        }
        snprintf(path, sizeof(path), "%s/main.c", dest_dir);
        f = fopen(path, "w");
        if (f) {
            fprintf(f, "#include <stdio.h>\n\nint main(void) {\n    printf(\"Hello from %s!\\n\");\n    return 0;\n}\n", name);
            fclose(f);
        }
        return true;
    }
    else if (tmpl == PROJECT_TEMPLATE_C_CMAKE) {
        snprintf(path, sizeof(path), "%s/CMakeLists.txt", dest_dir);
        FILE *f = fopen(path, "w");
        if (f) {
            fprintf(f, "cmake_minimum_required(VERSION 3.20)\n"
                       "project(%s C)\n\n"
                       "set(CMAKE_C_STANDARD 23)\n"
                       "set(CMAKE_C_STANDARD_REQUIRED ON)\n\n"
                       "add_executable(%s src/main.c)\n", name, name);
            fclose(f);
        }
        snprintf(path, sizeof(path), "%s/src", dest_dir);
        (void)mkdir(path, 0755);
        snprintf(path, sizeof(path), "%s/src/main.c", dest_dir);
        f = fopen(path, "w");
        if (f) {
            fprintf(f, "#include <stdio.h>\n\nint main(void) {\n    printf(\"Hello from %s (CMake)!\\n\");\n    return 0;\n}\n", name);
            fclose(f);
        }
        return true;
    }
    else if (tmpl == PROJECT_TEMPLATE_RUST_CARGO) {
        snprintf(path, sizeof(path), "%s/Cargo.toml", dest_dir);
        FILE *f = fopen(path, "w");
        if (f) {
            fprintf(f, "[package]\nname = \"%s\"\nversion = \"0.1.0\"\nedition = \"2021\"\n\n[dependencies]\n", name);
            fclose(f);
        }
        snprintf(path, sizeof(path), "%s/src", dest_dir);
        (void)mkdir(path, 0755);
        snprintf(path, sizeof(path), "%s/src/main.rs", dest_dir);
        f = fopen(path, "w");
        if (f) {
            fprintf(f, "fn main() {\n    println!(\"Hello from %s (Rust)!\");\n}\n", name);
            fclose(f);
        }
        return true;
    }
    else if (tmpl == PROJECT_TEMPLATE_GO) {
        snprintf(path, sizeof(path), "%s/go.mod", dest_dir);
        FILE *f = fopen(path, "w");
        if (f) {
            fprintf(f, "module %s\n\ngo 1.22\n", name);
            fclose(f);
        }
        snprintf(path, sizeof(path), "%s/main.go", dest_dir);
        f = fopen(path, "w");
        if (f) {
            fprintf(f, "package main\n\nimport \"fmt\"\n\nfunc main() {\n    fmt.Println(\"Hello from %s (Go)!\")\n}\n", name);
            fclose(f);
        }
        return true;
    }
    else if (tmpl == PROJECT_TEMPLATE_PYTHON) {
        snprintf(path, sizeof(path), "%s/main.py", dest_dir);
        FILE *f = fopen(path, "w");
        if (f) {
            fprintf(f, "def main():\n    print(\"Hello from %s!\")\n\nif __name__ == \"__main__\":\n    main()\n", name);
            fclose(f);
        }
        snprintf(path, sizeof(path), "%s/requirements.txt", dest_dir);
        f = fopen(path, "w");
        if (f) {
            fprintf(f, "# Project dependencies\n");
            fclose(f);
        }
        return true;
    }
    else if (tmpl == PROJECT_TEMPLATE_ZIG) {
        snprintf(path, sizeof(path), "%s/build.zig", dest_dir);
        FILE *f = fopen(path, "w");
        if (f) {
            fprintf(f, "const std = @import(\"std\");\n\n"
                       "pub fn build(b: *std.Build) void {\n"
                       "    const target = b.standardTargetOptions(.{});\n"
                       "    const optimize = b.standardOptimizeOption(.{});\n"
                       "    const exe = b.addExecutable(.{\n"
                       "        .name = \"%s\",\n"
                       "        .root_source_file = b.path(\"src/main.zig\"),\n"
                       "        .target = target,\n"
                       "        .optimize = optimize,\n"
                       "    });\n"
                       "    b.installArtifact(exe);\n"
                       "    const run_cmd = b.addRunArtifact(exe);\n"
                       "    const run_step = b.step(\"run\", \"Run the app\");\n"
                       "    run_step.dependOn(&run_cmd.step);\n"
                       "}\n", name);
            fclose(f);
        }
        snprintf(path, sizeof(path), "%s/src", dest_dir);
        (void)mkdir(path, 0755);
        snprintf(path, sizeof(path), "%s/src/main.zig", dest_dir);
        f = fopen(path, "w");
        if (f) {
            fprintf(f, "const std = @import(\"std\");\n\n"
                       "pub fn main() !void {\n"
                       "    std.debug.print(\"Hello from {s} (Zig)!\\n\", .{\"%s\"});\n"
                       "}\n", name);
            fclose(f);
        }
        return true;
    }
    else if (tmpl == PROJECT_TEMPLATE_CUSTOM) {
        snprintf(path, sizeof(path), "%s/README.md", dest_dir);
        FILE *f = fopen(path, "w");
        if (f) {
            fprintf(f, "# %s\n\nWelcome to your new project in cgui.\n", name);
            fclose(f);
        }
        snprintf(path, sizeof(path), "%s/.cgui", dest_dir);
        (void)mkdir(path, 0755);
        snprintf(path, sizeof(path), "%s/.cgui/project.json", dest_dir);
        f = fopen(path, "w");
        if (f) {
            fprintf(f, "{\n  \"name\": \"%s\",\n  \"type\": \"Workspace\",\n  \"targets\": [\n"
                       "    { \"name\": \"Build\", \"cmd\": \"echo 'Building %s...'\" },\n"
                       "    { \"name\": \"Run\", \"cmd\": \"echo 'Running %s...'\" }\n"
                       "  ]\n}\n", name, name, name);
            fclose(f);
        }
        return true;
    }

    return false;
}
