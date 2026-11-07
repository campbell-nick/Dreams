#define _XOPEN_SOURCE 700
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <dirent.h>
#include <pwd.h>
#include <limits.h>
#include <errno.h>
#include "commands.h"

#define ILLUS_VERSION "1.0.0"

/* ------------------------------------------------------------------------- */
/* Small helpers                                                             */
/* ------------------------------------------------------------------------- */

static int run_cmd(char *const argv[]) {
    pid_t pid;
    int status;

    fflush(NULL);
    pid = fork();
    if (pid < 0) {
        perror("[Dreams] Error: fork failed");
        return -1;
    }
    if (pid == 0) {
        execvp(argv[0], argv);
        perror("[Dreams] Error: execvp failed");
        _exit(127);
    }
    if (waitpid(pid, &status, 0) < 0) {
        perror("[Dreams] Error: waitpid failed");
        return -1;
    }
    if (WIFEXITED(status)) {
        return WEXITSTATUS(status);
    }
    return -1;
}

/* Run a command with DESTDIR set for the child only, restoring it afterwards. */
static int run_cmd_with_destdir(char *const argv[], const char *destdir) {
    const char *old = getenv("DESTDIR");
    char *saved = old ? strdup(old) : NULL;
    int rc;

    if (setenv("DESTDIR", destdir, 1) != 0) {
        free(saved);
        perror("[Dreams] Error: setenv failed");
        return -1;
    }
    rc = run_cmd(argv);
    if (saved) {
        setenv("DESTDIR", saved, 1);
        free(saved);
    } else {
        unsetenv("DESTDIR");
    }
    return rc;
}

/* Get the real user's home directory and UID/GID, even under sudo. */
static char *get_real_home(uid_t *out_uid, gid_t *out_gid) {
    uid_t uid = getuid();
    const char *sudo_user = getenv("SUDO_USER");
    struct passwd *pw = NULL;

    if (uid == 0 && sudo_user) {
        pw = getpwnam(sudo_user);
    }
    if (!pw) {
        pw = getpwuid(uid);
    }
    if (!pw || !pw->pw_dir) {
        return NULL;
    }

    if (out_uid) *out_uid = pw->pw_uid;
    if (out_gid) *out_gid = pw->pw_gid;
    return strdup(pw->pw_dir);
}

/* Strictly validate a single path component used as a repo/package name. */
static int is_valid_name(const char *name) {
    size_t i;

    if (!name || !*name) return 0;
    if (name[0] == '.' || name[0] == '-') return 0;
    if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) return 0;
    for (i = 0; name[i] != '\0'; i++) {
        char c = name[i];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '.' || c == '_' ||
              c == '-' || c == '+')) {
            return 0;
        }
    }
    return 1;
}

/* Join two path components, adding a slash only when needed. */
static int path_join(char *out, size_t size, const char *a, const char *b) {
    size_t la, lb, off;
    int slash;

    if (!out || size == 0 || !a || !b) return -1;
    la = strlen(a);
    lb = strlen(b);
    slash = (la > 0 && a[la - 1] != '/' && (lb == 0 || b[0] != '/'));
    if (la + (size_t)slash + lb + 1 > size) return -1;

    memcpy(out, a, la);
    off = la;
    if (slash) out[off++] = '/';
    memcpy(out + off, b, lb + 1);
    return 0;
}

/* Build "stem + suffix" without relying on printf truncation rules. */
static int make_filename(char *out, size_t size, const char *stem, const char *suffix) {
    size_t a = strlen(stem);
    size_t b = strlen(suffix);

    if (a + b + 1 > size) return -1;
    memcpy(out, stem, a);
    memcpy(out + a, suffix, b + 1);
    return 0;
}

/* Get the parent directory of an absolute path. */
static int parent_dir(const char *path, char *out, size_t size) {
    const char *slash = strrchr(path, '/');
    size_t len;

    if (!slash) {
        if (size < 2) return -1;
        out[0] = '.';
        out[1] = '\0';
        return 0;
    }
    len = (size_t)(slash - path);
    if (len == 0) len = 1;
    if (len + 1 > size) return -1;
    memcpy(out, path, len);
    out[len] = '\0';
    return 0;
}

/* True when path is root itself or is inside root. */
static int is_within(const char *path, const char *root) {
    size_t n = strlen(root);

    if (strncmp(path, root, n) != 0) return 0;
    return path[n] == '\0' || path[n] == '/';
}

/* Reject any path containing a ".." component. */
static int has_dotdot(const char *path) {
    const char *p = path;

    while (*p) {
        const char *start;
        size_t len;
        while (*p == '/') p++;
        start = p;
        while (*p && *p != '/') p++;
        len = (size_t)(p - start);
        if (len == 2 && start[0] == '.' && start[1] == '.') return 1;
    }
    return 0;
}

/* Expand "~", "~/..." or turn a relative path into an absolute one. */
static char *expand_path(const char *input, const char *home) {
    char buf[PATH_MAX];
    size_t len;

    if (!input || !*input) return NULL;
    if (strcmp(input, "~") == 0) return strdup(home);

    if (input[0] == '~') {
        if (input[1] != '/') return NULL;
        if (path_join(buf, sizeof buf, home, input + 2) != 0) return NULL;
    } else if (input[0] == '/') {
        if (strlen(input) >= sizeof buf) return NULL;
        strcpy(buf, input);
    } else {
        char cwd[PATH_MAX];
        if (!getcwd(cwd, sizeof cwd)) return NULL;
        if (path_join(buf, sizeof buf, cwd, input) != 0) return NULL;
    }

    len = strlen(buf);
    while (len > 1 && buf[len - 1] == '/') buf[--len] = '\0';
    return strdup(buf);
}

/* mkdir -p implementation. */
static int mkdir_p(const char *path, mode_t mode) {
    char tmp[PATH_MAX];
    size_t len;
    char *p;
    struct stat st;

    if (!path || !*path) return -1;
    len = strlen(path);
    if (len >= sizeof tmp) return -1;
    memcpy(tmp, path, len + 1);
    while (len > 1 && tmp[len - 1] == '/') tmp[--len] = '\0';

    if (strcmp(tmp, "/") == 0) return 0;
    for (p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            if (mkdir(tmp, mode) != 0 && errno != EEXIST) return -1;
            *p = '/';
        }
    }
    if (mkdir(tmp, mode) != 0 && errno != EEXIST) return -1;
    return (stat(tmp, &st) == 0 && S_ISDIR(st.st_mode)) ? 0 : -1;
}

/* Refuse a handful of destinations that would be catastrophic to replace. */
static int is_protected_target(const char *target, const char *home) {
    static const char *names[] = {
        "", ".local", ".local/bin", ".local/lib", ".local/share",
        ".ssh", ".config", "Dreams", NULL
    };
    static const char *roots[] = {
        "/", "/bin", "/boot", "/dev", "/etc", "/home", "/lib", "/lib64",
        "/media", "/mnt", "/opt", "/proc", "/root", "/run", "/sbin",
        "/srv", "/sys", "/tmp", "/usr", "/var", "/var/tmp", NULL
    };
    size_t i;

    for (i = 0; roots[i] != NULL; i++) {
        if (strcmp(target, roots[i]) == 0) return 1;
    }
    /* Reject home itself and any ancestor of home (e.g. /home or /). */
    if (is_within(home, target)) return 1;

    for (i = 0; names[i] != NULL; i++) {
        char p[PATH_MAX];
        if (names[i][0] == '\0') {
            if (strcmp(target, home) == 0) return 1;
        } else if (path_join(p, sizeof p, home, names[i]) == 0 &&
                   strcmp(target, p) == 0) {
            return 1;
        }
    }
    return 0;
}

/* Allow a --force replacement only inside home or the temp directories. */
static int replace_allowed(const char *target, const char *home) {
    if (strcmp(target, "/") == 0 || strcmp(target, home) == 0) return 0;
    if (is_within(target, home)) return 1;
    if (is_within(target, "/tmp") && strcmp(target, "/tmp") != 0) return 1;
    if (is_within(target, "/var/tmp") && strcmp(target, "/var/tmp") != 0) return 1;
    return 0;
}

/* System areas that should never receive a package clone. */
static int is_system_area(const char *path) {
    static const char *roots[] = {
        "/", "/bin", "/boot", "/dev", "/etc", "/home", "/lib", "/lib64",
        "/proc", "/root", "/run", "/sbin", "/sys", "/usr", NULL
    };
    size_t i;

    /* /var/tmp is a legitimate scratch destination; the rest of /var is not. */
    if (is_within(path, "/var/tmp")) return 0;
    if (is_within(path, "/var")) return 1;
    for (i = 0; roots[i] != NULL; i++) {
        if (is_within(path, roots[i])) return 1;
    }
    return 0;
}

/*
 * Walk up to the nearest existing ancestor of path, resolve it, and refuse if
 * it lands in a system area. This must run before mkdir_p so a symlinked
 * component cannot make us create directories inside /etc, /usr, etc.
 */
static int check_existing_ancestor(const char *path, const char *home) {
    char probe[PATH_MAX];
    char resolved[PATH_MAX];
    size_t len;
    char *slash;

    if (!path || !*path || strlen(path) >= sizeof probe) return -1;
    strcpy(probe, path);
    len = strlen(probe);
    while (len > 1 && probe[len - 1] == '/') probe[--len] = '\0';

    for (;;) {
        if (realpath(probe, resolved)) {
            if (is_within(resolved, home)) return 0;
            return is_system_area(resolved) ? -1 : 0;
        }
        slash = strrchr(probe, '/');
        if (!slash || slash == probe) return -1;
        *slash = '\0';
    }
}
/* ------------------------------------------------------------------------- */
/* Filesystem helpers                                                        */
/* ------------------------------------------------------------------------- */

/* Symlink-safe recursive directory removal. */
static void remove_directory(const char *path) {
    DIR *d = opendir(path);
    struct dirent *p;

    if (!d) return;
    while ((p = readdir(d)) != NULL) {
        char buf[PATH_MAX];
        struct stat st;

        if (!strcmp(p->d_name, ".") || !strcmp(p->d_name, "..")) continue;
        if (path_join(buf, sizeof buf, path, p->d_name) != 0) continue;
        if (lstat(buf, &st) != 0) continue;
        if (S_ISDIR(st.st_mode) && !S_ISLNK(st.st_mode)) {
            remove_directory(buf);
        } else if (unlink(buf) != 0) {
            /* best effort */
        }
    }
    closedir(d);
    if (rmdir(path) != 0) {
        /* best effort */
    }
}

/* Recursive chown that never follows symlinks. */
static void recursive_chown(const char *path, uid_t uid, gid_t gid) {
    struct stat st;
    DIR *d;
    struct dirent *p;

    if (lchown(path, uid, gid) != 0) {
        /* best effort */
    }
    if (lstat(path, &st) != 0 || !S_ISDIR(st.st_mode) || S_ISLNK(st.st_mode)) {
        return;
    }
    d = opendir(path);
    if (!d) return;
    while ((p = readdir(d)) != NULL) {
        char buf[PATH_MAX];
        if (!strcmp(p->d_name, ".") || !strcmp(p->d_name, "..")) continue;
        if (path_join(buf, sizeof buf, path, p->d_name) == 0) {
            recursive_chown(buf, uid, gid);
        }
    }
    closedir(d);
}

/* Chown a file and every parent directory up to (and including) stop. */
static void chown_with_parents(const char *path, const char *stop, uid_t uid, gid_t gid) {
    char tmp[PATH_MAX];
    size_t len;

    if (!is_within(path, stop)) return;
    if (lchown(path, uid, gid) != 0) {
        /* best effort */
    }

    len = strlen(path);
    if (len >= sizeof tmp) return;
    memcpy(tmp, path, len + 1);
    for (;;) {
        char *slash = strrchr(tmp, '/');
        if (!slash || slash == tmp) break;
        *slash = '\0';
        if (!is_within(tmp, stop)) break;
        if (lchown(tmp, uid, gid) != 0) {
            /* best effort */
        }
        if (strcmp(tmp, stop) == 0) break;
    }
}

/* A tiny string list used to collect installed files. */
typedef struct {
    char **items;
    size_t len;
    size_t cap;
} strlist;

static int strlist_add(strlist *l, const char *s) {
    size_t i;
    char *dup;

    if (!l || !s) return -1;
    for (i = 0; i < l->len; i++) {
        if (strcmp(l->items[i], s) == 0) return 0;
    }
    if (l->len == l->cap) {
        size_t ncap = l->cap ? l->cap * 2 : 16;
        char **n = realloc(l->items, ncap * sizeof(*n));
        if (!n) return -1;
        l->items = n;
        l->cap = ncap;
    }
    dup = strdup(s);
    if (!dup) return -1;
    l->items[l->len++] = dup;
    return 0;
}

static void strlist_free(strlist *l) {
    size_t i;

    if (!l) return;
    for (i = 0; i < l->len; i++) free(l->items[i]);
    free(l->items);
    l->items = NULL;
    l->len = 0;
    l->cap = 0;
}

/* Re-read an existing manifest so upgrades keep ownership of old files. */
static void load_manifest(strlist *files, const char *manifest,
                          const char *local_dir, const char *skip_dir) {
    FILE *f = fopen(manifest, "r");
    char line[PATH_MAX];

    if (!f) return;
    while (fgets(line, sizeof line, f)) {
        struct stat st;
        line[strcspn(line, "\r\n")] = '\0';
        if (!line[0]) continue;
        if (!is_within(line, local_dir)) continue;
        if (skip_dir && is_within(line, skip_dir)) continue;
        if (lstat(line, &st) != 0) continue;
        if (strlist_add(files, line) != 0) break;
    }
    fclose(f);
}

/* Move a staged install tree into place, recording every final file path. */
static int move_tree(const char *src_root, const char *dst_root, strlist *files,
                     uid_t uid, gid_t gid) {
    DIR *d;
    struct dirent *p;
    int rc = 0;

    d = opendir(src_root);
    if (!d) {
        perror("[Dreams] Warning: could not read staged install");
        return -1;
    }
    while ((p = readdir(d)) != NULL) {
        char src[PATH_MAX];
        char dst[PATH_MAX];
        struct stat st;
        struct stat dst_st;
        int dst_exists;

        if (!strcmp(p->d_name, ".") || !strcmp(p->d_name, "..")) continue;
        if (path_join(src, sizeof src, src_root, p->d_name) != 0) continue;
        if (path_join(dst, sizeof dst, dst_root, p->d_name) != 0) continue;
        if (lstat(src, &st) != 0) continue;

        dst_exists = (lstat(dst, &dst_st) == 0);

        if (S_ISDIR(st.st_mode) && !S_ISLNK(st.st_mode)) {
            if (dst_exists && !(S_ISDIR(dst_st.st_mode) && !S_ISLNK(dst_st.st_mode))) {
                if (S_ISDIR(dst_st.st_mode) && !S_ISLNK(dst_st.st_mode)) {
                    remove_directory(dst);
                } else if (unlink(dst) != 0) {
                    perror("[Dreams] Warning: could not replace destination");
                    rc = -1;
                    continue;
                }
            }
            if (mkdir_p(dst, st.st_mode & 0777) != 0) {
                perror("[Dreams] Warning: could not create installation directory");
                rc = -1;
                continue;
            }
            if (chmod(dst, st.st_mode & 07777) != 0) {
                /* best effort */
            }
            if (getuid() == 0 && lchown(dst, uid, gid) != 0) {
                /* best effort */
            }
            if (move_tree(src, dst, files, uid, gid) != 0) rc = -1;
            if (rmdir(src) != 0) {
                /* best effort */
            }
        } else {
            if (dst_exists) {
                if (S_ISDIR(dst_st.st_mode) && !S_ISLNK(dst_st.st_mode)) {
                    fprintf(stderr,
                            "[Dreams] Warning: refusing to replace directory with file: %s\n",
                            dst);
                    rc = -1;
                    continue;
                } else if (unlink(dst) != 0) {
                    perror("[Dreams] Warning: could not replace destination");
                    rc = -1;
                    continue;
                }
            }
            if (rename(src, dst) != 0) {
                perror("[Dreams] Warning: could not install file");
                rc = -1;
                continue;
            }
            if (getuid() == 0 && lchown(dst, uid, gid) != 0) {
                /* best effort */
            }
            if (strlist_add(files, dst) != 0) rc = -1;
        }
    }
    closedir(d);
    return rc;
}

/* Create a private staging directory under ~/.local for a DESTDIR install. */
static int make_stage_dir(const char *home, char *out, size_t out_size) {
    char local_dir[PATH_MAX];
    char template[PATH_MAX];

    if (path_join(local_dir, sizeof local_dir, home, ".local") != 0) return -1;
    if (mkdir_p(local_dir, 0755) != 0) return -1;
    if (path_join(template, sizeof template, local_dir,
                  ".illus-stage-XXXXXX") != 0) {
        return -1;
    }
    if (!mkdtemp(template)) {
        perror("[Dreams] Error: could not create staging directory");
        return -1;
    }
    if (strlen(template) >= out_size) {
        remove_directory(template);
        return -1;
    }
    strcpy(out, template);
    return 0;
}

/* Find a manifest that owns the given binary name. */
static int find_manifest_for_binary(const char *manifest_dir, const char *binary,
                                    char *out, size_t out_size) {
    DIR *d = opendir(manifest_dir);
    struct dirent *p;
    int matches = 0;

    if (!d) return 0;
    while ((p = readdir(d)) != NULL) {
        size_t len = strlen(p->d_name);
        char path[PATH_MAX];
        char line[PATH_MAX];
        FILE *f;
        int found = 0;

        if (len < 5 || strcmp(p->d_name + len - 4, ".txt") != 0) continue;
        if (path_join(path, sizeof path, manifest_dir, p->d_name) != 0) continue;
        f = fopen(path, "r");
        if (!f) continue;
        while (fgets(line, sizeof line, f)) {
            const char *base;
            line[strcspn(line, "\r\n")] = '\0';
            base = strrchr(line, '/');
            base = base ? base + 1 : line;
            if (strcmp(base, binary) == 0) {
                found = 1;
                break;
            }
        }
        fclose(f);
        if (found) {
            matches++;
            if (matches == 1) {
                if (strlen(path) >= out_size) {
                    matches = 0;
                    break;
                }
                strcpy(out, path);
            } else {
                break;
            }
        }
    }
    closedir(d);
    return matches > 1 ? -2 : matches;
}

/* Resolve a manifest entry for deletion without following the final symlink. */
static int resolve_for_delete(const char *line, char *out, size_t out_size) {
    const char *base;
    char parent[PATH_MAX];
    char resolved_parent[PATH_MAX];

    if (parent_dir(line, parent, sizeof parent) != 0) return -1;
    if (!realpath(parent, resolved_parent)) return -1;
    base = strrchr(line, '/');
    if (!base) return -1;
    return path_join(out, out_size, resolved_parent, base + 1);
}

/* Remove now-empty parent directories, stopping before stop itself. */
static void remove_empty_parents(const char *path, const char *stop) {
    char tmp[PATH_MAX];
    size_t len = strlen(path);

    if (len >= sizeof tmp) return;
    memcpy(tmp, path, len + 1);
    for (;;) {
        char *slash = strrchr(tmp, '/');
        if (!slash || slash == tmp) break;
        *slash = '\0';
        if (strcmp(tmp, stop) == 0) break;
        if (!is_within(tmp, stop)) break;
        if (rmdir(tmp) != 0) break;
    }
}

/* Move a DESTDIR stage into ~/.local and write the package manifest. */
static void track_install(const char *home, const char *pkg_name, const char *stage,
                          uid_t uid, gid_t gid) {
    char local_dir[PATH_MAX];
    char staged_local[PATH_MAX];
    char manifest_dir[PATH_MAX];
    char filename[PATH_MAX];
    char manifest_file[PATH_MAX];
    struct stat st;
    strlist files;
    FILE *mf;
    size_t i;

    memset(&files, 0, sizeof files);
    if (path_join(local_dir, sizeof local_dir, home, ".local") != 0) return;
    if (path_join(staged_local, sizeof staged_local, stage, local_dir) != 0) return;

    if (lstat(staged_local, &st) != 0 || !S_ISDIR(st.st_mode)) {
        fprintf(stderr,
                "[Dreams] Warning: build system ignored DESTDIR; no manifest written.\n");
        return;
    }

    if (!pkg_name || !*pkg_name) {
        if (move_tree(staged_local, local_dir, &files, uid, gid) != 0) {
            fprintf(stderr,
                    "[Dreams] Warning: some staged files could not be installed.\n");
        }
        if (getuid() == 0) {
            chown_with_parents(local_dir, home, uid, gid);
        }
        strlist_free(&files);
        return;
    }

    if (path_join(manifest_dir, sizeof manifest_dir, home,
                  ".local/share/illus") != 0) {
        return;
    }
    if (mkdir_p(manifest_dir, 0755) != 0) {
        perror("[Dreams] Warning: could not create manifest directory");
        return;
    }
    if (make_filename(filename, sizeof filename, pkg_name, ".txt") != 0) return;
    if (path_join(manifest_file, sizeof manifest_file, manifest_dir, filename) != 0) {
        return;
    }

    load_manifest(&files, manifest_file, local_dir, manifest_dir);

    if (move_tree(staged_local, local_dir, &files, uid, gid) != 0) {
        fprintf(stderr,
                "[Dreams] Warning: some staged files could not be installed.\n");
    }

    mf = fopen(manifest_file, "w");
    if (!mf) {
        perror("[Dreams] Warning: could not write manifest");
        strlist_free(&files);
        return;
    }
    for (i = 0; i < files.len; i++) {
        fprintf(mf, "%s\n", files.items[i]);
    }
    fclose(mf);

    if (getuid() == 0) {
        chown_with_parents(manifest_file, local_dir, uid, gid);
    }

    printf("[Dreams] Manifest created for package: %s (%zu files)\n",
           pkg_name, files.len);
    strlist_free(&files);
}
/* ------------------------------------------------------------------------- */
/* Pull command                                                              */
/* ------------------------------------------------------------------------- */

int cmd_pull(int argc, char *argv[]) {
    const char *url;
    const char *dir_arg = NULL;
    int force = 0;
    int i;
    uid_t uid = 0;
    gid_t gid = 0;
    char *home;
    char repo_name[256];
    size_t name_len;
    char *target = NULL;

    if (argc < 3) {
        fprintf(stderr, "[Dreams] Usage: illus --pull <url> [--dir <path>] [--force]\n");
        return 1;
    }

    url = argv[2];
    for (i = 3; i < argc; i++) {
        if (strcmp(argv[i], "--force") == 0) {
            force = 1;
        } else if (strcmp(argv[i], "--dir") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "[Dreams] Error: --dir requires a path.\n");
                return 1;
            }
            dir_arg = argv[++i];
        } else {
            fprintf(stderr, "[Dreams] Warning: Unknown flag for --pull: %s\n", argv[i]);
        }
    }

    home = get_real_home(&uid, &gid);
    if (!home) {
        fprintf(stderr, "[Dreams] Error: Could not determine home directory.\n");
        return 1;
    }
    printf("[Dreams] Pulling repository from: %s\n", url);

    {
        size_t url_len = strlen(url);
        const char *name_end;
        const char *name_start;

        while (url_len > 0 && url[url_len - 1] == '/') url_len--;
        name_end = url + url_len;
        name_start = name_end;
        while (name_start > url && name_start[-1] != '/') name_start--;
        if (name_start == url) {
            fprintf(stderr, "[Dreams] Error: Invalid repository URL format.\n");
            free(home);
            return 1;
        }
        name_len = (size_t)(name_end - name_start);
        if (name_len == 0 || name_len >= sizeof repo_name) {
            fprintf(stderr, "[Dreams] Error: Repository name is too long.\n");
            free(home);
            return 1;
        }
        memcpy(repo_name, name_start, name_len);
        repo_name[name_len] = '\0';
        if (name_len > 4 && strcmp(repo_name + name_len - 4, ".git") == 0) {
            repo_name[name_len - 4] = '\0';
        }
    }
    if (!is_valid_name(repo_name)) {
        fprintf(stderr, "[Dreams] Error: Extracted repository name is invalid.\n");
        free(home);
        return 1;
    }

    if (dir_arg) {
        target = expand_path(dir_arg, home);
        if (!target) {
            fprintf(stderr, "[Dreams] Error: Invalid --dir path.\n");
            free(home);
            return 1;
        }
        if (has_dotdot(target)) {
            fprintf(stderr, "[Dreams] Error: --dir must not contain '..'.\n");
            free(target);
            free(home);
            return 1;
        }
        if (is_protected_target(target, home)) {
            fprintf(stderr, "[Dreams] Error: refusing to use protected directory: %s\n",
                    target);
            free(target);
            free(home);
            return 1;
        }
        {
            char cwd[PATH_MAX];
            if (getcwd(cwd, sizeof cwd) && strcmp(target, cwd) == 0) {
                fprintf(stderr, "[Dreams] Error: refusing to use the current directory as a destination.\n");
                free(target);
                free(home);
                return 1;
            }
        }
        {
            char parent[PATH_MAX];
            if (parent_dir(target, parent, sizeof parent) != 0) {
                fprintf(stderr, "[Dreams] Error: destination path is too long.\n");
                free(target);
                free(home);
                return 1;
            }
            if (check_existing_ancestor(parent, home) != 0) {
                fprintf(stderr,
                        "[Dreams] Error: refusing to create destination under system path: %s\n",
                        parent);
                free(target);
                free(home);
                return 1;
            }
            if (mkdir_p(parent, 0755) != 0) {
                perror("[Dreams] Error: could not create destination parent");
                free(target);
                free(home);
                return 1;
            }
            {
                char resolved_parent[PATH_MAX];
                char resolved_home[PATH_MAX];
                const char *safe_home = home;

                if (realpath(home, resolved_home)) safe_home = resolved_home;
                if (!realpath(parent, resolved_parent)) {
                    perror("[Dreams] Error: could not resolve destination parent");
                    free(target);
                    free(home);
                    return 1;
                }
                if (is_system_area(resolved_parent) &&
                    !is_within(resolved_parent, safe_home)) {
                    fprintf(stderr,
                            "[Dreams] Error: refusing to clone into system directory: %s\n",
                            resolved_parent);
                    free(target);
                    free(home);
                    return 1;
                }
            }
        }
    } else {
        char dreams_dir[PATH_MAX];
        char target_buf[PATH_MAX];
        struct stat st;

        if (path_join(dreams_dir, sizeof dreams_dir, home, "Dreams") != 0 ||
            path_join(target_buf, sizeof target_buf, dreams_dir, repo_name) != 0) {
            fprintf(stderr, "[Dreams] Error: destination path is too long.\n");
            free(home);
            return 1;
        }
        if (lstat(dreams_dir, &st) == 0) {
            if (S_ISLNK(st.st_mode)) {
                fprintf(stderr, "[Dreams] Error: ~/Dreams is a symlink; refusing to continue.\n");
                free(home);
                return 1;
            }
            if (!S_ISDIR(st.st_mode)) {
                fprintf(stderr, "[Dreams] Error: ~/Dreams exists but is not a directory.\n");
                free(home);
                return 1;
            }
        } else if (mkdir_p(dreams_dir, 0755) != 0) {
            perror("[Dreams] Error: could not create ~/Dreams");
            free(home);
            return 1;
        }
        target = strdup(target_buf);
        if (!target) {
            fprintf(stderr, "[Dreams] Error: out of memory.\n");
            free(home);
            return 1;
        }
    }

    {
        struct stat st;
        if (lstat(target, &st) == 0) {
            if (!force) {
                fprintf(stderr, "[Dreams] Error: destination already exists: %s\n", target);
                fprintf(stderr, "[Dreams] Use --force to replace it.\n");
                free(target);
                free(home);
                return 1;
            }
            if (S_ISDIR(st.st_mode) && !S_ISLNK(st.st_mode)) {
                char resolved_target[PATH_MAX];
                char resolved_home[PATH_MAX];
                const char *safe_home = home;

                if (realpath(home, resolved_home)) safe_home = resolved_home;
                if (!realpath(target, resolved_target)) {
                    perror("[Dreams] Error: could not resolve existing destination");
                    free(target);
                    free(home);
                    return 1;
                }
                if (!replace_allowed(resolved_target, safe_home)) {
                    fprintf(stderr,
                            "[Dreams] Error: refusing to recursively remove %s\n",
                            resolved_target);
                    fprintf(stderr,
                            "[Dreams] Choose a destination inside your home directory or /tmp.\n");
                    free(target);
                    free(home);
                    return 1;
                }
                remove_directory(resolved_target);
            } else if (unlink(target) != 0) {
                perror("[Dreams] Error: could not remove existing destination");
                free(target);
                free(home);
                return 1;
            }
        }
    }

    printf("[Dreams] Cloning into %s...\n", target);
    {
        char *const clone_argv[] = {"git", "clone", (char *)url, target, NULL};
        int status = run_cmd(clone_argv);
        if (status != 0) {
            fprintf(stderr, "[Dreams] Error: git clone failed.\n");
            free(target);
            free(home);
            return status;
        }
    }

    if (getuid() == 0) {
        recursive_chown(target, uid, gid);
        chown_with_parents(target, home, uid, gid);
    }

    printf("[Dreams] Successfully pulled and stored in %s\n", target);
    free(target);
    free(home);
    return 0;
}

/* ------------------------------------------------------------------------- */
/* Cast command                                                              */
/* ------------------------------------------------------------------------- */

int cmd_cast(int store, int fresh) {
    uid_t uid = 0;
    gid_t gid = 0;
    char *home = get_real_home(&uid, &gid);
    char workspace[PATH_MAX];
    char local_dir[PATH_MAX];
    char stage[PATH_MAX];
    char cmake_path[PATH_MAX];
    char makefile_path[PATH_MAX];
    char build_dir[PATH_MAX];
    char pkg_name[256];
    const char *base;
    size_t base_len;
    int can_track;
    int is_cmake;
    int is_makefile;

    if (!home) {
        fprintf(stderr, "[Dreams] Error: Could not determine home directory.\n");
        return 1;
    }
    if (!getcwd(workspace, sizeof workspace)) {
        fprintf(stderr, "[Dreams] Error: Could not determine current working directory.\n");
        free(home);
        return 1;
    }
    if (path_join(local_dir, sizeof local_dir, home, ".local") != 0 ||
        path_join(cmake_path, sizeof cmake_path, workspace, "CMakeLists.txt") != 0 ||
        path_join(makefile_path, sizeof makefile_path, workspace, "Makefile") != 0 ||
        path_join(build_dir, sizeof build_dir, workspace, "build") != 0) {
        fprintf(stderr, "[Dreams] Error: path is too long.\n");
        free(home);
        return 1;
    }

    is_cmake = (access(cmake_path, F_OK) == 0);
    is_makefile = (access(makefile_path, F_OK) == 0);
    if (!is_cmake && !is_makefile) {
        fprintf(stderr, "[Dreams] Error: No recognized build system found in current directory.\n");
        free(home);
        return 1;
    }

    base = strrchr(workspace, '/');
    base = base ? base + 1 : workspace;
    base_len = strlen(base);
    can_track = (base_len > 0 && base_len + 4 < sizeof pkg_name &&
                 is_valid_name(base));
    if (can_track) {
        memcpy(pkg_name, base, base_len + 1);
    } else {
        pkg_name[0] = '\0';
    }

    if (fresh) {
        struct stat st;
        printf("[Dreams] Sweeping clean build artifacts (--fresh)...\n");
        if (lstat(build_dir, &st) == 0) {
            if (S_ISDIR(st.st_mode) && !S_ISLNK(st.st_mode)) {
                remove_directory(build_dir);
            } else if (unlink(build_dir) != 0) {
                perror("[Dreams] Warning: could not remove build path");
            }
        }
        if (is_makefile) {
            char *const clean_argv[] = {"make", "clean", NULL};
            if (run_cmd(clean_argv) != 0) {
                fprintf(stderr, "[Dreams] Warning: 'make clean' failed.\n");
            }
        }
    }

    if (is_cmake) {
        char prefix_arg[PATH_MAX];
        const char *opt = "-DCMAKE_INSTALL_PREFIX=";
        size_t opt_len = strlen(opt);
        size_t dir_len = strlen(local_dir);

        printf("[Dreams] Detected CMake build system.\n");
        if (mkdir_p(build_dir, 0755) != 0) {
            perror("[Dreams] Error: could not create build directory");
            free(home);
            return 1;
        }
        if (opt_len + dir_len + 1 > sizeof prefix_arg) {
            fprintf(stderr, "[Dreams] Error: install prefix is too long.\n");
            free(home);
            return 1;
        }
        memcpy(prefix_arg, opt, opt_len);
        memcpy(prefix_arg + opt_len, local_dir, dir_len + 1);

        {
            char *const config_argv[] = {
                "cmake", "-B", build_dir, "-S", workspace, prefix_arg, NULL
            };
            if (run_cmd(config_argv) != 0) {
                fprintf(stderr, "[Dreams] Error: CMake configuration failed.\n");
                free(home);
                return 1;
            }
        }
        {
            char *const build_argv[] = {"cmake", "--build", build_dir, NULL};
            if (run_cmd(build_argv) != 0) {
                fprintf(stderr, "[Dreams] Error: build failed.\n");
                free(home);
                return 1;
            }
        }
        if (store) {
            char *const install_argv[] = {"cmake", "--install", build_dir, NULL};

            if (make_stage_dir(home, stage, sizeof stage) != 0) {
                fprintf(stderr, "[Dreams] Error: could not create staging directory.\n");
                free(home);
                return 1;
            }
            printf("[Dreams] Storing package into %s (--store)...\n", local_dir);
            if (run_cmd_with_destdir(install_argv, stage) != 0) {
                fprintf(stderr, "[Dreams] Error: installation failed.\n");
                remove_directory(stage);
                free(home);
                return 1;
            }
            if (can_track) {
                track_install(home, pkg_name, stage, uid, gid);
            } else {
                fprintf(stderr, "[Dreams] Warning: invalid package name; installing without a manifest.\n");
                track_install(home, NULL, stage, uid, gid);
            }
            remove_directory(stage);
        }
    } else {
        printf("[Dreams] Detected Make build system.\n");
        {
            char *const make_argv[] = {"make", NULL};
            if (run_cmd(make_argv) != 0) {
                fprintf(stderr, "[Dreams] Error: build failed.\n");
                free(home);
                return 1;
            }
        }
        if (store) {
            char prefix_env[PATH_MAX];
            char destdir_env[PATH_MAX];
            const char *opt = "PREFIX=";
            const char *dopt = "DESTDIR=";
            size_t opt_len = strlen(opt);
            size_t dopt_len = strlen(dopt);
            size_t dir_len = strlen(local_dir);
            size_t stage_len;
            char *install_argv[5];

            if (make_stage_dir(home, stage, sizeof stage) != 0) {
                fprintf(stderr, "[Dreams] Error: could not create staging directory.\n");
                free(home);
                return 1;
            }
            stage_len = strlen(stage);

            if (opt_len + dir_len + 1 > sizeof prefix_env ||
                dopt_len + stage_len + 1 > sizeof destdir_env) {
                fprintf(stderr, "[Dreams] Error: install prefix is too long.\n");
                remove_directory(stage);
                free(home);
                return 1;
            }
            memcpy(prefix_env, opt, opt_len);
            memcpy(prefix_env + opt_len, local_dir, dir_len + 1);
            memcpy(destdir_env, dopt, dopt_len);
            memcpy(destdir_env + dopt_len, stage, stage_len + 1);

            install_argv[0] = "make";
            install_argv[1] = "install";
            install_argv[2] = prefix_env;
            install_argv[3] = destdir_env;
            install_argv[4] = NULL;

            printf("[Dreams] Storing package into %s (--store)...\n", local_dir);
            if (run_cmd_with_destdir(install_argv, stage) != 0) {
                fprintf(stderr, "[Dreams] Error: installation failed.\n");
                remove_directory(stage);
                free(home);
                return 1;
            }
            if (can_track) {
                track_install(home, pkg_name, stage, uid, gid);
            } else {
                fprintf(stderr, "[Dreams] Warning: invalid package name; installing without a manifest.\n");
                track_install(home, NULL, stage, uid, gid);
            }
            remove_directory(stage);
        }
    }

    if (getuid() == 0 && strlen(workspace) > strlen(home) + 1 &&
        is_within(workspace, home)) {
        recursive_chown(workspace, uid, gid);
    }

    printf("[Dreams] Successfully built%s project!\n",
           store ? " and installed" : "");
    free(home);
    return 0;
}
/* ------------------------------------------------------------------------- */
/* Update command                                                            */
/* ------------------------------------------------------------------------- */

int cmd_update(void) {
    char *home = get_real_home(NULL, NULL);
    char dreams_path[PATH_MAX];
    char cwd_buf[PATH_MAX];
    DIR *dir;
    struct dirent *entry;
    int found_repos = 0;
    int failures = 0;

    if (!home) {
        fprintf(stderr, "[Dreams] Error: Could not determine home directory.\n");
        return 1;
    }
    if (path_join(dreams_path, sizeof dreams_path, home, "Dreams") != 0) {
        fprintf(stderr, "[Dreams] Error: path is too long.\n");
        free(home);
        return 1;
    }
    if (!getcwd(cwd_buf, sizeof cwd_buf)) {
        fprintf(stderr, "[Dreams] Error: could not determine current directory.\n");
        free(home);
        return 1;
    }

    dir = opendir(dreams_path);
    if (!dir) {
        int saved = errno;

        if (saved == ENOENT) {
            printf("Your Dreams are empty.\n");
            free(home);
            return 0;
        }
        errno = saved;
        perror("[Dreams] Error: could not open ~/Dreams");
        free(home);
        return 1;
    }

    while ((entry = readdir(dir)) != NULL) {
        char repo_path[PATH_MAX];
        char git_dir[PATH_MAX];
        char cmake_path[PATH_MAX];
        char makefile_path[PATH_MAX];

        if (entry->d_name[0] == '.') continue;
        if (path_join(repo_path, sizeof repo_path, dreams_path, entry->d_name) != 0) {
            continue;
        }
        if (path_join(git_dir, sizeof git_dir, repo_path, ".git") != 0) continue;
        if (access(git_dir, F_OK) != 0) continue;

        found_repos = 1;
        printf("[Dreams] Updating project: %s\n", entry->d_name);

        if (chdir(repo_path) != 0) {
            perror("[Dreams] Error: could not enter project directory");
            failures++;
            continue;
        }

        {
            char *const pull_argv[] = {"git", "pull", NULL};
            if (run_cmd(pull_argv) != 0) {
                printf("[Dreams] Error: 'git pull' failed for %s.\n", entry->d_name);
                failures++;
                if (chdir(cwd_buf) != 0) {
                    perror("[Dreams] Warning: could not restore working directory");
                }
                continue;
            }
        }

        if (path_join(cmake_path, sizeof cmake_path, repo_path, "CMakeLists.txt") != 0 ||
            path_join(makefile_path, sizeof makefile_path, repo_path, "Makefile") != 0) {
            fprintf(stderr, "[Dreams] Warning: path too long for %s.\n", entry->d_name);
            failures++;
            if (chdir(cwd_buf) != 0) {
                perror("[Dreams] Warning: could not restore working directory");
            }
            continue;
        }

        if (access(cmake_path, F_OK) == 0 || access(makefile_path, F_OK) == 0) {
            if (cmd_cast(1, 0) != 0) {
                printf("[Dreams] Error: rebuild/install failed for %s.\n", entry->d_name);
                failures++;
            } else {
                printf("[Dreams] Successfully updated and rebuilt %s!\n\n", entry->d_name);
            }
        } else {
            printf("[Dreams] Pulled latest changes for %s (no build system found, skipping build).\n\n",
                   entry->d_name);
        }

        if (chdir(cwd_buf) != 0) {
            perror("[Dreams] Warning: could not restore working directory");
            failures++;
        }
    }
    closedir(dir);

    if (!found_repos) {
        printf("Your Dreams are empty.\n");
    }
    free(home);
    return failures > 0 ? 1 : 0;
}

/* ------------------------------------------------------------------------- */
/* Version command                                                           */
/* ------------------------------------------------------------------------- */

int cmd_version(void) {
    printf("\033[36m");
    printf("  ___ _ _            \n");
    printf(" |_ _| | |_   _ ___  \n");
    printf("  | || | | | | / __| \n");
    printf("  | || | | |_| \\__ \\ \n");
    printf(" |___|_|_|\\__,_|___/ \n\n");
    printf("\033[0m");
    printf(" ✦ Dreams Package Manager (illus) v%s ✦\n", ILLUS_VERSION);
    return 0;
}

/* ------------------------------------------------------------------------- */
/* Shed command                                                              */
/* ------------------------------------------------------------------------- */

int cmd_shed(const char *package_name) {
    char *home;
    char local_dir[PATH_MAX];
    char manifest_dir[PATH_MAX];
    char filename[PATH_MAX];
    char direct_manifest[PATH_MAX];
    char manifest_path[PATH_MAX] = "";
    char resolved_local[PATH_MAX];
    char line[PATH_MAX];
    FILE *f = NULL;
    int failures = 0;

    if (!is_valid_name(package_name)) {
        fprintf(stderr, "[Dreams] Error: Invalid package name \"%s\"\n", package_name);
        return 1;
    }

    printf("[Dreams] Shedding (uninstalling) package: %s\n", package_name);

    home = get_real_home(NULL, NULL);
    if (!home) {
        fprintf(stderr, "[Dreams] Error: Could not determine home directory.\n");
        return 1;
    }

    if (path_join(local_dir, sizeof local_dir, home, ".local") != 0 ||
        path_join(manifest_dir, sizeof manifest_dir, home,
                  ".local/share/illus") != 0 ||
        make_filename(filename, sizeof filename, package_name, ".txt") != 0 ||
        path_join(direct_manifest, sizeof direct_manifest, manifest_dir,
                  filename) != 0) {
        fprintf(stderr, "[Dreams] Error: path is too long.\n");
        free(home);
        return 1;
    }

    /* Resolve the install root once so containment checks are consistent. */
    if (!realpath(local_dir, resolved_local)) {
        if (path_join(resolved_local, sizeof resolved_local, home, ".local") != 0) {
            fprintf(stderr, "[Dreams] Error: path is too long.\n");
            free(home);
            return 1;
        }
    }

    f = fopen(direct_manifest, "r");
    if (f) {
        strcpy(manifest_path, direct_manifest);
    } else {
        char found[PATH_MAX];
        int matches = find_manifest_for_binary(manifest_dir, package_name,
                                               found, sizeof found);
        if (matches == -2) {
            fprintf(stderr,
                    "[Dreams] Error: multiple packages provide '%s'; use the package name.\n",
                    package_name);
            free(home);
            return 1;
        }
        if (matches == 1) {
            f = fopen(found, "r");
            if (f) strcpy(manifest_path, found);
        }
    }

    if (!f) {
        char bin_dir[PATH_MAX];
        char fallback[PATH_MAX];
        char resolved_fallback[PATH_MAX];
        struct stat st;

        if (path_join(bin_dir, sizeof bin_dir, home, ".local/bin") != 0 ||
            path_join(fallback, sizeof fallback, bin_dir, package_name) != 0) {
            fprintf(stderr, "[Dreams] Error: path is too long.\n");
            free(home);
            return 1;
        }
        if (lstat(fallback, &st) == 0) {
            printf("[Dreams] No manifest found, but found binary at %s. Removing...\n",
                   fallback);
            if (resolve_for_delete(fallback, resolved_fallback,
                                   sizeof resolved_fallback) != 0) {
                strcpy(resolved_fallback, fallback);
            }
            if (!is_within(resolved_fallback, resolved_local)) {
                fprintf(stderr,
                        "[Dreams] Error: refusing to delete outside %s: %s\n",
                        resolved_local, fallback);
                free(home);
                return 1;
            }
            if (remove(resolved_fallback) != 0) {
                perror("[Dreams] Error: could not remove binary");
                free(home);
                return 1;
            }
            remove_empty_parents(resolved_fallback, resolved_local);
            printf("[Dreams] Successfully removed %s\n", fallback);
            free(home);
            return 0;
        }

        fprintf(stderr, "[Dreams] Error: Package '%s' is not installed.\n",
                package_name);
        free(home);
        return 1;
    }

    if (!realpath(local_dir, resolved_local)) {
        if (path_join(resolved_local, sizeof resolved_local, home, ".local") != 0) {
            fprintf(stderr, "[Dreams] Error: path is too long.\n");
            fclose(f);
            free(home);
            return 1;
        }
    }

    while (fgets(line, sizeof line, f)) {
        char resolved[PATH_MAX];

        line[strcspn(line, "\r\n")] = '\0';
        if (!line[0]) continue;

        if (resolve_for_delete(line, resolved, sizeof resolved) != 0) {
            fprintf(stderr, "[Dreams] Warning: tracked file no longer exists: %s\n",
                    line);
            continue;
        }
        if (!is_within(resolved, resolved_local)) {
            fprintf(stderr,
                    "[Dreams] Error: refusing to delete outside %s: %s\n",
                    resolved_local, line);
            failures++;
            continue;
        }

        printf("[Dreams] Removing file: %s\n", resolved);
        if (remove(resolved) != 0) {
            if (errno == ENOENT) {
                /* Already gone (removed manually or by an earlier run). */
                remove_empty_parents(resolved, resolved_local);
            } else {
                perror("[Dreams] Warning: could not remove file");
                failures++;
            }
        } else {
            remove_empty_parents(resolved, resolved_local);
        }
    }
    fclose(f);

    if (failures == 0) {
        if (remove(manifest_path) != 0) {
            perror("[Dreams] Warning: could not remove manifest");
            failures++;
        }
    }

    if (failures == 0) {
        printf("[Dreams] Successfully shed package '%s'!\n", package_name);
    } else {
        fprintf(stderr,
                "[Dreams] Package '%s' was partially removed (%d issue(s)).\n",
                package_name, failures);
        fprintf(stderr, "[Dreams] Manifest kept at %s for retry.\n",
                manifest_path);
    }

    free(home);
    return failures == 0 ? 0 : 1;
}

/* ------------------------------------------------------------------------- */
/* List command                                                              */
/* ------------------------------------------------------------------------- */

int cmd_list(void) {
    char *home = get_real_home(NULL, NULL);
    char manifest_dir[PATH_MAX];
    DIR *dir;
    struct dirent *entry;
    int count = 0;

    if (!home) {
        fprintf(stderr, "[Dreams] Error: Could not determine home directory.\n");
        return 1;
    }
    if (path_join(manifest_dir, sizeof manifest_dir, home,
                  ".local/share/illus") != 0) {
        fprintf(stderr, "[Dreams] Error: path is too long.\n");
        free(home);
        return 1;
    }

    dir = opendir(manifest_dir);
    if (dir) {
        while ((entry = readdir(dir)) != NULL) {
            size_t len = strlen(entry->d_name);
            char name[256];

            if (len < 5 || strcmp(entry->d_name + len - 4, ".txt") != 0) continue;
            if (len - 4 >= sizeof name) continue;
            memcpy(name, entry->d_name, len - 4);
            name[len - 4] = '\0';

            if (count == 0) {
                printf("\n\033[36m=== Tracked packages ===\033[0m\n");
            }
            printf("  ✦ \033[35m%s\033[0m\n", name);
            count++;
        }
        closedir(dir);
    }

    if (count == 0) {
        char bin_dir[PATH_MAX];
        DIR *bin_dir_handle;

        printf("\n[Dreams] No tracked packages found.\n");
        if (path_join(bin_dir, sizeof bin_dir, home, ".local/bin") == 0 &&
            (bin_dir_handle = opendir(bin_dir)) != NULL) {
            printf("\n\033[36m=== Binaries in ~/.local/bin ===\033[0m\n");
            while ((entry = readdir(bin_dir_handle)) != NULL) {
                if (entry->d_name[0] == '.') continue;
                printf("  ✦ \033[35m%s\033[0m\n", entry->d_name);
                count++;
            }
            closedir(bin_dir_handle);
        }
        if (count == 0) {
            printf("  (none)\n");
        }
    }

    printf("\n");
    free(home);
    return 0;
}

/* ------------------------------------------------------------------------- */
/* Help                                                                      */
/* ------------------------------------------------------------------------- */

void print_help(const char *program_name) {
    printf("\033[36m");
    printf("  ___ _ _            \n");
    printf(" |_ _| | |_   _ ___  \n");
    printf("  | || | | | | / __| \n");
    printf("  | || | | |_| \\__ \\ \n");
    printf(" |___|_|_|\\__,_|___/ \n\n");
    printf("\033[0m");
    printf(" ✦ Dreams Package Manager (illus) v%s ✦\n\n", ILLUS_VERSION);
    printf("Usage:\n");
    printf("  %s --pull <url> [--dir <path>] [--force]\n", program_name);
    printf("      Clone a repository into ~/Dreams or an explicit destination.\n");
    printf("  %s --cast [--store] [--fresh]\n", program_name);
    printf("      Build the current project; --store installs into ~/.local.\n");
    printf("  %s --shed <package>\n", program_name);
    printf("      Uninstall a package; accepts its package or binary name.\n");
    printf("  %s --update\n", program_name);
    printf("      Pull, rebuild and reinstall every project in ~/Dreams.\n");
    printf("  %s --list\n", program_name);
    printf("      List tracked packages (or ~/.local/bin binaries).\n");
    printf("  %s --version\n", program_name);
    printf("      Show version information.\n");
    printf("  %s --help\n", program_name);
    printf("      Show this help.\n");
}
