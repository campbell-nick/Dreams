#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <unistd.h>
#include <dirent.h>
#include <pwd.h>
#include "commands.h"

// Helper function to get the real user's home directory (even under sudo)
static char* get_real_home() {
    const char *sudo_user = getenv("SUDO_USER");
    if (sudo_user) {
        struct passwd *pw = getpwnam(sudo_user);
        if (pw && pw->pw_dir) {
            char *path = malloc(strlen(pw->pw_dir) + 1);
            if (path) strcpy(path, pw->pw_dir);
            return path;
        }
    }
    const char *home = getenv("HOME");
    if (home) {
        char *path = malloc(strlen(home) + 1);
        if (path) strcpy(path, home);
        return path;
    }
    struct passwd *pw = getpwuid(getuid());
    if (pw && pw->pw_dir) {
        char *path = malloc(strlen(pw->pw_dir) + 1);
        if (path) strcpy(path, pw->pw_dir);
        return path;
    }
    return NULL;
}
// Pull Command
void cmd_pull(int argc, char *argv[]) {
    if (argc < 3) {
        fprintf(stderr, "[Dreams] Usage: illus --pull <repository_url> [--dir <path>]\n");
        return;
    }

    const char *url = argv[2];
    printf("[Dreams] Pulling repository from: %s\n", url);

    char *home = get_real_home();
    if (!home) {
        fprintf(stderr, "[Dreams] Error: Could not determine home directory.\n");
        return;
    }

    // Extract repository name from Url
    const char *last_slash = strrchr(url, '/');
    if (!last_slash) {
        fprintf(stderr, "[Dreams] Error: Invalid repository URL format.\n");
        free(home);
        return;
    }

    char repo_name[256];
    snprintf(repo_name, sizeof(repo_name), "%s", last_slash + 1);

    char *ext = strstr(repo_name, ".git");
    if (ext) {
        *ext = '\0';
    }

    // Check for --dir flag
    char target_path[2048];
    int custom_dir_specified = 0;

    for (int i = 3; i < argc; i++) {
        if (strcmp(argv[i], "--dir") == 0 && i + 1 < argc) {
            snprintf(target_path, sizeof(target_path), "%s", argv[i + 1]);
            custom_dir_specified = 1;
            break;
        }
    }

    // If no custom directory is specified, default to ~/Dreams/repo_name
    if (!custom_dir_specified) {
        char dreams_dir[1024];
        snprintf(dreams_dir, sizeof(dreams_dir), "%s/Dreams", home);

        char mkdir_dreams_cmd[1150];
        snprintf(mkdir_dreams_cmd, sizeof(mkdir_dreams_cmd), "mkdir -p \"%s\"", dreams_dir);
        system(mkdir_dreams_cmd);

        snprintf(target_path, sizeof(target_path), "%s/%s", dreams_dir, repo_name);
    } else {
        // Ensure the custom target directory path exists
        char mkdir_custom_cmd[2300];
        snprintf(mkdir_custom_cmd, sizeof(mkdir_custom_cmd), "mkdir -p \"%s\"", target_path);
        system(mkdir_custom_cmd);
    }

    // Clean up older folder if it already exists
    char rm_old_cmd[2150];
    snprintf(rm_old_cmd, sizeof(rm_old_cmd), "rm -rf \"%s\"", target_path);
    system(rm_old_cmd);

    // Run git clone
    char clone_cmd[2300];
    snprintf(clone_cmd, sizeof(clone_cmd), "git clone \"%s\" \"%s\"", url, target_path);

    int status = system(clone_cmd);
    if (status != 0) {
        fprintf(stderr, "[Dreams] Error: git clone failed.\n");
        free(home);
        return;
    }

    printf("[Dreams] Successfully pulled and stored in %s\n", target_path);
    free(home);
}

// Cast Command
void cmd_cast(int store, int fresh) {
    char *home = get_real_home();
    if (!home) {
        fprintf(stderr, "[Dreams] Error: Could not determine home directory.\n");
        return;
    }

    // Use current working directory directly
    char workspace[2048];
    if (!getcwd(workspace, sizeof(workspace))) {
        fprintf(stderr, "[Dreams] Error: Could not determine current working directory.\n");
        free(home);
        return;
    }

    // Check build system type in current directory
    char cmake_path[2150];
    char makefile_path[2150];
    snprintf(cmake_path, sizeof(cmake_path), "%s/CMakeLists.txt", workspace);
    snprintf(makefile_path, sizeof(makefile_path), "%s/Makefile", workspace);

    int is_cmake = (access(cmake_path, F_OK) == 0);
    int is_makefile = (access(makefile_path, F_OK) == 0);

    if (!is_cmake && !is_makefile) {
        fprintf(stderr, "[Dreams] Error: No recognized build system (CMakeLists.txt or Makefile) found in current directory.\n");
        free(home);
        return;
    }

    char build_cmd[4096];
    char clean_cmd[4096];
    char install_cmd[4096];

    if (is_cmake) {
        printf("[Dreams] Detected CMake build system.\n");
        snprintf(build_cmd, sizeof(build_cmd), 
            "mkdir -p \"%s/build\" && cd \"%s/build\" && cmake -DCMAKE_INSTALL_PREFIX=\"%s/.local\" .. && cmake --build .", 
            workspace, workspace, home);
        snprintf(clean_cmd, sizeof(clean_cmd), "rm -rf \"%s/build\"", workspace);
        snprintf(install_cmd, sizeof(install_cmd), "cd \"%s/build\" && cmake --install .", workspace);
    } else {
        printf("[Dreams] Detected Make build system.\n");
        snprintf(build_cmd, sizeof(build_cmd), "cd \"%s\" && make", workspace);
        snprintf(clean_cmd, sizeof(clean_cmd), "cd \"%s\" && make clean", workspace);
        snprintf(install_cmd, sizeof(install_cmd), "cd \"%s\" && make PREFIX=\"%s/.local\" install", workspace, home);
    }

    // Execute Clean if requested
    if (fresh) {
        printf("[Dreams] Sweeping clean build artifacts (--fresh)...\n");
        system(clean_cmd);
    }

    // Execute Build
    printf("[Dreams] Casting spell... building project...\n");
    int build_status = system(build_cmd);
    if (build_status != 0) {
        fprintf(stderr, "[Dreams] Error: Build failed.\n");
        free(home);
        return;
    }
    printf("[Dreams] Build successful!\n");

    // Execute Install if requested
    if (store) {
        printf("[Dreams] Storing binary to %s/.local/bin/ (--store)...\n", home);
        int install_status = system(install_cmd);
        if (install_status != 0) {
            fprintf(stderr, "[Dreams] Error: Installation failed.\n");
            free(home);
            return;
        }
        printf("[Dreams] Successfully stored and installed package!\n");

        // Write Manifest file for uninstall tracker
        char *repo_name = strrchr(workspace, '/');
        if (repo_name) {
            repo_name++;
            char manifest_dir[1024];
            snprintf(manifest_dir, sizeof(manifest_dir), "%s/.local/share/illus", home);
            
            char mkdir_manifest_cmd[1150];
            snprintf(mkdir_manifest_cmd, sizeof(mkdir_manifest_cmd), "mkdir -p \"%s\"", manifest_dir);
            system(mkdir_manifest_cmd);

            char manifest_file_path[1150];
            snprintf(manifest_file_path, sizeof(manifest_file_path), "%s/%s.txt", manifest_dir, repo_name);

            FILE *mf = fopen(manifest_file_path, "w");
            if (mf) {
                fprintf(mf, "%s/.local/bin/%s\n", home, repo_name);
                fprintf(mf, "%s\n", manifest_file_path);
                fclose(mf);
                printf("[Dreams] Manifest created for package: %s\n", repo_name);
            }
        }
    }
    free(home);
}

void cmd_update(void) {
    char *home = get_real_home();
    if (!home) {
        fprintf(stderr, "[Dreams] Error: Could not determine home directory.\n");
        return;
    }

    char dreams_path[512];
    snprintf(dreams_path, sizeof(dreams_path), "%s/Dreams", home);

    DIR *dir = opendir(dreams_path);
    if (!dir) {
        printf("Your Dreams are empty.\n");
        free(home);
        return;
    }

    struct dirent *entry;
    int found_repos = 0;

    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }

        char repo_path[1024];
        snprintf(repo_path, sizeof(repo_path), "%s/%s", dreams_path, entry->d_name);

        // Check if this subdirectory contains a .git folder
        char git_dir[1150];
        snprintf(git_dir, sizeof(git_dir), "%s/.git", repo_path);

        if (access(git_dir, F_OK) == 0) {
            found_repos = 1;
            printf("[Dreams] Updating project: %s\n", entry->d_name);

            char cmd[4096];
            // 1. Git pull
            snprintf(cmd, sizeof(cmd), "git -C \"%s\" pull", repo_path);
            if (system(cmd) != 0) {
                printf("[Dreams] Error: 'git pull' failed for %s.\n", entry->d_name);
                continue;
            }

            // 2. Check build systems
            char makefile_path[1150], makefile_lower[1150], cmake_path[1150];
            snprintf(makefile_path, sizeof(makefile_path), "%s/Makefile", repo_path);
            snprintf(makefile_lower, sizeof(makefile_lower), "%s/makefile", repo_path);
            snprintf(cmake_path, sizeof(cmake_path), "%s/CMakeLists.txt", repo_path);

            int is_cmake = (access(cmake_path, F_OK) == 0);
            int is_make = (access(makefile_path, F_OK) == 0 || access(makefile_lower, F_OK) == 0);

            if (is_cmake) {
                snprintf(cmd, sizeof(cmd), 
                    "mkdir -p \"%s/build\" && cd \"%s/build\" && cmake -DCMAKE_INSTALL_PREFIX=\"%s/.local\" .. && cmake --build . && cmake --install .", 
                    repo_path, repo_path, home);
                if (system(cmd) != 0) {
                    printf("[Dreams] Error: CMake build or installation failed for %s.\n", entry->d_name);
                    continue;
                }
                printf("[Dreams] Successfully updated and rebuilt %s!\n\n", entry->d_name);
            } else if (is_make) {
                snprintf(cmd, sizeof(cmd), "cd \"%s\" && make && make PREFIX=\"%s/.local\" install", repo_path, home);
                if (system(cmd) != 0) {
                    printf("[Dreams] Error: Make build or installation failed for %s.\n", entry->d_name);
                    continue;
                }
                printf("[Dreams] Successfully updated and rebuilt %s!\n\n", entry->d_name);
            } else {
                printf("[Dreams] Pulled latest changes for %s (no Makefile/CMakeLists.txt found, skipping build).\n\n", entry->d_name);
            }
        }
    }
    closedir(dir);

    if (!found_repos) {
        printf("Your Dreams are empty.\n");
    }
    free(home);
}

// Version Command
void cmd_version() {
    printf("\033[36m");
    printf("  ___ _ _            \n");
    printf(" |_ _| | |_   _ ___  \n");
    printf("  | || | | | | / __| \n");
    printf("  | || | | |_| \\__ \\ \n");
    printf(" |___|_|_|\\__,_|___/ \n\n");
    printf("\033[0m");
    printf(" ✦ Dreams Package Manager (illus) v1.0.0✦\n");
}

// Shed Command
void cmd_shed(const char *package_name) {
    printf("[Dreams] Shedding (uninstalling) package: %s\n", package_name);

    char *home = get_real_home();
    if (!home) {
        fprintf(stderr, "[Dreams] Error: Could not determine home directory.\n");
        return;
    }

    char manifest_path[1150];
    snprintf(manifest_path, sizeof(manifest_path), "%s/.local/share/illus/%s.txt", home, package_name);

    FILE *f = fopen(manifest_path, "r");
    if (!f) {
        // Fallback: Try deleting binary directly if manifest is missing
        char fallback_bin[1150];
        snprintf(fallback_bin, sizeof(fallback_bin), "%s/.local/bin/%s", home, package_name);
        
        if (access(fallback_bin, F_OK) == 0) {
            printf("[Dreams] No manifest found, but found binary at %s. Removing...\n", fallback_bin);
            if (remove(fallback_bin) == 0) {
                printf("[Dreams] Successfully removed %s\n", fallback_bin);
                free(home);
                return;
            }
        }

        fprintf(stderr, "[Dreams] Error: Package '%s' is not installed or no manifest found.\n", package_name);
        free(home);
        return;
    }

    char line[1024];
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
    } else {
        perror("[Dreams] Warning: Could not remove manifest tracking file");
    }
    free(home);
}

void cmd_list() {
    char *home = get_real_home();
    if (!home) {
        fprintf(stderr, "[Dreams] Error: Could not determine home directory.\n");
        return;
    }

    char path[512];
    snprintf(path, sizeof(path), "%s/.local/bin", home);
    
    DIR *dir = opendir(path);
    if (!dir) {
        printf("[Dreams] Binary directory not found: %s\n", path);
        free(home);
        return;
    }

    printf("\n\033[36m=== Installed Binaries (~/.local/bin) ===\033[0m\n");
    
    struct dirent *entry;
    int count = 0;
    
    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }

        printf("  ✦ \033[35m%s\033[0m\n", entry->d_name);
        count++;
    }
    closedir(dir);

    if (count == 0) {
        printf("  (No binaries found)\n");
    }
    printf("\n");
    free(home);
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
