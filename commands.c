#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <unistd.h>
#include <dirent.h>
#include <pwd.h>
#include <limits.h>
#include <errno.h>
#include "commands.h"

// Safe process execution helper (replaces vulnerable system() calls)
static int run_cmd(char *const argv[]) {
    pid_t pid = fork();
    if (pid < 0) {
        perror("[Dreams] Error: fork failed");
        return -1;
    }
    if (pid == 0) {
        execvp(argv[0], argv);
        perror("[Dreams] Error: execvp failed");
        _exit(127);
    }
    int status;
    if (waitpid(pid, &status, 0) < 0) {
        perror("[Dreams] Error: waitpid failed");
        return -1;
    }
    if (WIFEXITED(status)) {
        return WEXITSTATUS(status);
    }
    return -1;
}

// Helper function to get the real user's home directory and UID/GID (even under sudo)
static char* get_real_home(uid_t *out_uid, gid_t *out_gid) {
    uid_t uid = getuid();
    gid_t gid = getgid();
    const char *sudo_user = getenv("SUDO_USER");
    
    if (uid == 0 && sudo_user) {
        struct passwd *pw = getpwnam(sudo_user);
        if (pw && pw->pw_dir) {
            if (out_uid) *out_uid = pw->pw_uid;
            if (out_gid) *out_gid = pw->pw_gid;
            char *path = malloc(strlen(pw->pw_dir) + 1);
            if (path) strcpy(path, pw->pw_dir);
            return path;
        }
    }
    
    struct passwd *pw = getpwuid(uid);
    if (pw && pw->pw_dir) {
        if (out_uid) *out_uid = uid;
        if (out_gid) *out_gid = gid;
        char *path = malloc(strlen(pw->pw_dir) + 1);
        if (path) strcpy(path, pw->pw_dir);
        return path;
    }
    
    const char *home = getenv("HOME");
    if (home) {
        if (out_uid) *out_uid = uid;
        if (out_gid) *out_gid = gid;
        char *path = malloc(strlen(home) + 1);
        if (path) strcpy(path, home);
        return path;
    }
    
    if (out_uid) *out_uid = uid;
    if (out_gid) *out_gid = gid;
    char *path = malloc(5);
    if (path) strcpy(path, "/tmp");
    return path;
}

// Sanitize names to prevent path traversal
static int is_valid_name(const char *name) {
    if (!name || *name == '\0') return 0;
    if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) return 0;
    for (int i = 0; name[i] != '\0'; i++) {
        char c = name[i];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-' || c == '+')) {
            return 0;
        }
    }
    return 1;
}

// Recursive directory removal helper for safe cleanup
static void remove_directory(const char *path) {
    DIR *d = opendir(path);
    if (!d) return;
    struct dirent *p;
    while ((p = readdir(d)) != NULL) {
        if (!strcmp(p->d_name, ".") || !strcmp(p->d_name, "..")) continue;
        char buf[PATH_MAX];
        snprintf(buf, sizeof(buf), "%s/%s", path, p->d_name);
        struct stat statbuf;
        if (!lstat(buf, &statbuf)) {
            if (S_ISDIR(statbuf.st_mode)) remove_directory(buf);
            else unlink(buf);
        }
    }
    closedir(d);
    rmdir(path);
}

// Pull Command
int cmd_pull(int argc, char *argv[]) {
    if (argc < 3) {
        fprintf(stderr, "[Dreams] Usage: illus --pull <repository_url> [--dir <path>]\n");
        return 1;
    }

    const char *url = argv[2];
    printf("[Dreams] Pulling repository from: %s\n", url);

    uid_t uid;
    gid_t gid;
    char *home = get_real_home(&uid, &gid);
    if (!home) {
        fprintf(stderr, "[Dreams] Error: Could not determine home directory.\n");
        return 1;
    }

    // Extract repository name from Url safely
    const char *last_slash = strrchr(url, '/');
    if (!last_slash || *(last_slash + 1) == '\0') {
        fprintf(stderr, "[Dreams] Error: Invalid repository URL format.\n");
        free(home);
        return 1;
    }

    char repo_name[256];
    snprintf(repo_name, sizeof(repo_name), "%s", last_slash + 1);

    char *ext = strstr(repo_name, ".git");
    if (ext) {
        *ext = '\0';
    }

    if (!is_valid_name(repo_name)) {
        fprintf(stderr, "[Dreams] Error: Extracted repository name is invalid.\n");
        free(home);
        return 1;
    }

    // Check for --dir flag
    char target_path[PATH_MAX];
    int custom_dir_specified = 0;

    for (int i = 3; i < argc; i++) {
        if (strcmp(argv[i], "--dir") == 0 && i + 1 < argc) {
            if (!is_valid_name(argv[i + 1])) {
                fprintf(stderr, "[Dreams] Error: Invalid custom directory name.\n");
                free(home);
                return 1;
            }
            snprintf(target_path, sizeof(target_path), "%s/%s", home, argv[i + 1]);
            custom_dir_specified = 1;
            break;
        }
    }

    char dreams_dir[PATH_MAX];
    snprintf(dreams_dir, sizeof(dreams_dir), "%s/Dreams", home);
    mkdir(dreams_dir, 0755);

    if (!custom_dir_specified) {
        snprintf(target_path, sizeof(target_path), "%s/%s", dreams_dir, repo_name);
    }

    // Safely remove existing target folder if present
    struct stat st;
    if (stat(target_path, &st) == 0) {
        if (S_ISDIR(st.st_mode)) {
            remove_directory(target_path);
        } else {
            unlink(target_path);
        }
    }

    // Run git clone securely without shell injection risks
    printf("[Dreams] Cloning into %s...\n", target_path);
    char *const clone_argv[] = {"git", "clone", (char *)url, target_path, NULL};
    int status = run_cmd(clone_argv);
    if (status != 0) {
        fprintf(stderr, "[Dreams] Error: git clone failed.\n");
        free(home);
        return status;
    }

    // Fix ownership if run under sudo
    if (getuid() == 0) {
        chown(target_path, uid, gid);
    }

    printf("[Dreams] Successfully pulled and stored in %s\n", target_path);
    free(home);
    return 0;
}

// Cast Command
int cmd_cast(int store, int fresh) {
    uid_t uid;
    gid_t gid;
    char *home = get_real_home(&uid, &gid);
    if (!home) {
        fprintf(stderr, "[Dreams] Error: Could not determine home directory.\n");
        return 1;
    }

    char workspace[PATH_MAX];
    if (!getcwd(workspace, sizeof(workspace))) {
        fprintf(stderr, "[Dreams] Error: Could not determine current working directory.\n");
        free(home);
        return 1;
    }

    char cmake_path[PATH_MAX];
    char makefile_path[PATH_MAX];
    snprintf(cmake_path, sizeof(cmake_path), "%s/CMakeLists.txt", workspace);
    snprintf(makefile_path, sizeof(makefile_path), "%s/Makefile", workspace);

    int is_cmake = (access(cmake_path, F_OK) == 0);
    int is_makefile = (access(makefile_path, F_OK) == 0);

    if (!is_cmake && !is_makefile) {
        fprintf(stderr, "[Dreams] Error: No recognized build system found in current directory.\n");
        free(home);
        return 1;
    }

    char build_dir[PATH_MAX];
    snprintf(build_dir, sizeof(build_dir), "%s/build", workspace);

    // Execute Clean if requested
    if (fresh) {
        printf("[Dreams] Sweeping clean build artifacts (--fresh)...\n");
        remove_directory(build_dir);
    }

    // Execute Build & Install securely
    if (is_cmake) {
        printf("[Dreams] Detected CMake build system.\n");
        mkdir(build_dir, 0755);

        char prefix_arg[PATH_MAX];
        snprintf(prefix_arg, sizeof(prefix_arg), "-DCMAKE_INSTALL_PREFIX=%s/.local", home);

        char *const config_argv[] = {"cmake", "-B", build_dir, "-S", workspace, prefix_arg, NULL};
        if (run_cmd(config_argv) != 0) { free(home); return 1; }

        char *const build_argv[] = {"cmake", "--build", build_dir, NULL};
        if (run_cmd(build_argv) != 0) { free(home); return 1; }

        if (store) {
            printf("[Dreams] Storing binary to %s/.local/bin/ (--store)...\n", home);
            char *const install_argv[] = {"cmake", "--install", build_dir, NULL};
            if (run_cmd(install_argv) != 0) { free(home); return 1; }
        }
    } else {
        printf("[Dreams] Detected Make build system.\n");
        char *const make_argv[] = {"make", NULL};
        if (run_cmd(make_argv) != 0) { free(home); return 1; }

        if (store) {
            printf("[Dreams] Storing binary to %s/.local/bin/ (--store)...\n", home);
            char prefix_env[PATH_MAX];
            snprintf(prefix_env, sizeof(prefix_env), "PREFIX=%s/.local", home);
            char *const install_argv[] = {"make", "install", prefix_env, NULL};
            if (run_cmd(install_argv) != 0) { free(home); return 1; }
        }
    }

    // Fix ownership of built/installed files if run under sudo
    if (getuid() == 0 && store) {
        char local_bin[PATH_MAX];
        snprintf(local_bin, sizeof(local_bin), "%s/.local/bin", home);
        chown(local_bin, uid, gid);
    }

    // Write Manifest file for uninstall tracker
    if (store) {
        const char *repo_name = strrchr(workspace, '/');
        if (repo_name) {
            repo_name++;
            if (is_valid_name(repo_name)) {
                char manifest_dir[PATH_MAX];
                snprintf(manifest_dir, sizeof(manifest_dir), "%s/.local/share/illus", home);
                mkdir(manifest_dir, 0755);

                char manifest_file_path[PATH_MAX];
                snprintf(manifest_file_path, sizeof(manifest_file_path), "%s/%s.txt", manifest_dir, repo_name);

                FILE *mf = fopen(manifest_file_path, "w");
                if (mf) {
                    fprintf(mf, "%s/.local/bin/%s\n", home, repo_name);
                    fprintf(mf, "%s\n", manifest_file_path);
                    fclose(mf);
                    if (getuid() == 0) chown(manifest_file_path, uid, gid);
                    printf("[Dreams] Manifest created for package: %s\n", repo_name);
                }
            }
        }
    }

    printf("[Dreams] Successfully built and casted project!\n");
    free(home);
    return 0;
}

// Update Command
int cmd_update(void) {
    uid_t uid;
    gid_t gid;
    char *home = get_real_home(&uid, &gid);
    if (!home) {
        fprintf(stderr, "[Dreams] Error: Could not determine home directory.\n");
        return 1;
    }

    char dreams_path[PATH_MAX];
    snprintf(dreams_path, sizeof(dreams_path), "%s/Dreams", home);

    DIR *dir = opendir(dreams_path);
    if (!dir) {
        printf("Your Dreams are empty.\n");
        free(home);
        return 0;
    }

    struct dirent *entry;
    int found_repos = 0;
    int failures = 0;

    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_name[0] == '.') continue;

        char repo_path[PATH_MAX];
        snprintf(repo_path, sizeof(repo_path), "%s/%s", dreams_path, entry->d_name);

        char git_dir[PATH_MAX];
        snprintf(git_dir, sizeof(git_dir), "%s/.git", repo_path);

        if (access(git_dir, F_OK) == 0) {
            found_repos = 1;
            printf("[Dreams] Updating project: %s\n", entry->d_name);

            if (chdir(repo_path) == 0) {
                char *const pull_argv[] = {"git", "pull", NULL};
                if (run_cmd(pull_argv) != 0) {
                    printf("[Dreams] Error: 'git pull' failed for %s.\n", entry->d_name);
                    failures++;
                    continue;
                }

                // Re-cast / rebuild
                if (cmd_cast(1, 0) != 0) {
                    printf("[Dreams] Error: Rebuild/install failed for %s.\n", entry->d_name);
                    failures++;
                }
            }
        }
    }
    closedir(dir);

    if (!found_repos) {
        printf("Your Dreams are empty.\n");
    }
    free(home);
    return failures > 0 ? 1 : 0;
}

// Version Command
int cmd_version(void) {
    printf("\033[36m");
    printf("  ___ _ _            \n");
    printf(" |_ _| | |_   _ ___  \n");
    printf("  | || | | | | / __| \n");
    printf("  | || | | |_| \\__ \\ \n");
    printf(" |___|_|_|\\__,_|___/ \n\n");
    printf("\033[0m");
    printf(" ✦ Dreams Package Manager (illus) v1.0.0✦\n");
    return 0;
}

// Shed Command
int cmd_shed(const char *package_name) {
    if (!is_valid_name(package_name)) {
        fprintf(stderr, "[Dreams] Error: Invalid package name \"%s\"\n", package_name);
        return 1;
    }

    printf("[Dreams] Shedding (uninstalling) package: %s\n", package_name);

    uid_t uid;
    gid_t gid;
    char *home = get_real_home(&uid, &gid);
    if (!home) {
        fprintf(stderr, "[Dreams] Error: Could not determine home directory.\n");
        return 1;
    }

    char manifest_path[PATH_MAX];
    snprintf(manifest_path, sizeof(manifest_path), "%s/.local/share/illus/%s.txt", home, package_name);

    FILE *f = fopen(manifest_path, "r");
    if (!f) {
        char fallback_bin[PATH_MAX];
        snprintf(fallback_bin, sizeof(fallback_bin), "%s/.local/bin/%s", home, package_name);
        
        if (access(fallback_bin, F_OK) == 0) {
            printf("[Dreams] No manifest found, but found binary at %s. Removing...\n", fallback_bin);
            if (remove(fallback_bin) == 0) {
                printf("[Dreams] Successfully removed %s\n", fallback_bin);
                free(home);
                return 0;
            }
        }

        fprintf(stderr, "[Dreams] Error: Package '%s' is not installed or no manifest found.\n", package_name);
        free(home);
        return 1;
    }

    char line[PATH_MAX];
    while (fgets(line, sizeof(line), f)) {
        line[strcspn(line, "\n")] = 0;
        if (strlen(line) == 0) continue;

        printf("[Dreams] Removing file: %s\n", line);
        if (remove(line) != 0) {
            perror("[Dreams] Warning: Could not remove file");
        }
    }
    fclose(f);

    if (remove(manifest_path) == 0) {
        printf("[Dreams] Successfully shed package '%s'!\n", package_name);
        free(home);
        return 0;
    } else {
        perror("[Dreams] Warning: Could not remove manifest tracking file");
        free(home);
        return 1;
    }
}

int cmd_list(void) {
    uid_t uid;
    gid_t gid;
    char *home = get_real_home(&uid, &gid);
    if (!home) {
        fprintf(stderr, "[Dreams] Error: Could not determine home directory.\n");
        return 1;
    }

    char path[PATH_MAX];
    snprintf(path, sizeof(path), "%s/.local/bin", home);
    
    DIR *dir = opendir(path);
    if (!dir) {
        printf("[Dreams] Binary directory not found: %s\n", path);
        free(home);
        return 0;
    }

    printf("\n\033[36m=== Installed Binaries (~/.local/bin) ===\033[0m\n");
    
    struct dirent *entry;
    int count = 0;
    
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_name[0] == '.') continue;

        printf("  ✦ \033[35m%s\033[0m\n", entry->d_name);
        count++;
    }
    closedir(dir);

    if (count == 0) {
        printf("  (No binaries found)\n");
    }
    printf("\n");
    free(home);
    return 0;
}

void print_help(const char *program_name) {
    printf("\033[36m");
    printf("  ___ _ _            \n");
    printf(" |_ _| | |_   _ ___  \n");
    printf("  | || | | | | / __| \n");
    printf("  | || | | |_| \\__ \\ \n");
    printf(" |___|_|_|\\__,_|___/ \n\n");
    printf("\033[0m");
    printf(" ✦ Dreams Package Manager (illus) v1.0.0✦\n\n");
    printf("Usage:\n");
    printf("  %s --pull {Url}              Clone repository into ~/Dreams\n", program_name);
    printf("  %s --dir {path}              Specify custom directory for --pull (Used with --pull)\n", program_name);
    printf("  %s --cast                    Build the project\n", program_name);
    printf("  %s --cast --store            Build and install to ~/.local/bin\n", program_name);
    printf("  %s --cast --fresh            Clean build artifacts and build fresh\n", program_name);
    printf("  %s --shed [Package]          Uninstall package and its tracked files\n", program_name);
    printf("  %s --update                  Update all repositories in ~/Dreams\n", program_name);
    printf("  %s --list                    List all repositories in ~/Dreams\n", program_name);
    printf("  %s --version                 Show version information\n", program_name);
}