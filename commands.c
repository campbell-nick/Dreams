/*
 * Darwin maps _XOPEN_SOURCE 700 to POSIX.1-2008, which is not enough for
 * mkdtemp(); it is only declared at Darwin's full C level. Enable that level
 * on Apple platforms while keeping strict POSIX declarations elsewhere.
 */
#ifdef __APPLE__
#define _DARWIN_C_SOURCE
#endif
#define _XOPEN_SOURCE 700
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <pwd.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "commands.h"

#define MANIFEST_HEADER      "# illus-manifest-v2"

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

/* ------------------------------------------------------------------------- */
/* Argument lists                                                            */
/* ------------------------------------------------------------------------- */

typedef struct {
    char **items;
    size_t len;
    size_t cap;
} arglist;

static int arglist_add(arglist *l, const char *s) {
    char *dup;
    char **grown;

    if (l->len == l->cap) {
        size_t ncap = l->cap ? l->cap * 2 : 8;

        grown = realloc(l->items, ncap * sizeof(*grown));
        if (!grown) return -1;
        l->items = grown;
        l->cap = ncap;
    }
    dup = strdup(s);
    if (!dup) return -1;
    l->items[l->len++] = dup;
    return 0;
}

static void arglist_free(arglist *l) {
    size_t i;

    for (i = 0; i < l->len; i++) free(l->items[i]);
    free(l->items);
    l->items = NULL;
    l->len = 0;
    l->cap = 0;
}

/*
 * Split a recipe argument string into words. This deliberately supports only
 * quoting and backslash escaping: no shell expansion, command substitution, or
 * globbing ever reaches the build.
 */
static int split_args(const char *text, arglist *out) {
    char token[PATH_MAX];
    size_t len = 0;
    int quote = 0;
    const char *p = text;

    if (!text) return 0;
    for (;; p++) {
        char c = *p;

        if (c == '\0') {
            if (quote) return -1;
            if (len > 0) {
                token[len] = '\0';
                if (arglist_add(out, token) != 0) return -1;
                len = 0;
            }
            return 0;
        }
        if (quote == 0 && isspace((unsigned char)c)) {
            if (len > 0) {
                token[len] = '\0';
                if (arglist_add(out, token) != 0) return -1;
                len = 0;
            }
            continue;
        }
        if (c == '\\' && quote != '\'') {
            p++;
            if (*p == '\0') return -1;
            c = *p;
        } else if (c == '\'' || c == '"') {
            if (quote == 0) {
                quote = c;
                continue;
            }
            if (quote == c) {
                quote = 0;
                continue;
            }
        }
        if (len + 1 >= sizeof token) return -1;
        token[len++] = c;
    }
}

static char **argv_with_extra(char *const base[], const char *extra,
                              arglist *args) {
    size_t nbase = 0;
    size_t i;
    char **argv;

    while (base[nbase] != NULL) nbase++;
    if (split_args(extra, args) != 0) return NULL;
    argv = calloc(nbase + args->len + 1, sizeof(*argv));
    if (!argv) return NULL;
    for (i = 0; i < nbase; i++) argv[i] = base[i];
    for (i = 0; i < args->len; i++) argv[nbase + i] = args->items[i];
    return argv;
}

static int run_cmd_extra(char *const base[], const char *extra) {
    arglist args;
    char **argv;
    int rc;

    memset(&args, 0, sizeof args);
    argv = argv_with_extra(base, extra, &args);
    if (!argv) {
        arglist_free(&args);
        fprintf(stderr, "[Dreams] Error: could not parse build arguments.\n");
        return -1;
    }
    rc = run_cmd(argv);
    free(argv);
    arglist_free(&args);
    return rc;
}

static int run_cmd_with_destdir_extra(char *const base[], const char *extra,
                                      const char *destdir) {
    arglist args;
    char **argv;
    int rc;

    memset(&args, 0, sizeof args);
    argv = argv_with_extra(base, extra, &args);
    if (!argv) {
        arglist_free(&args);
        fprintf(stderr, "[Dreams] Error: could not parse build arguments.\n");
        return -1;
    }
    rc = run_cmd_with_destdir(argv, destdir);
    free(argv);
    arglist_free(&args);
    return rc;
}

/* Get the real user's home directory and UID/GID, even under sudo. */
static char *get_real_home(uid_t *out_uid, gid_t *out_gid) {
    uid_t uid = getuid();
    const char *sudo_user = getenv("SUDO_USER");
    const char *override = getenv("ILLUS_HOME");
    struct passwd *pw = NULL;

    /* ILLUS_HOME is intended for tests and isolated installs. */
    if (override && *override) {
        if (out_uid) *out_uid = getuid();
        if (out_gid) *out_gid = getgid();
        return strdup(override);
    }

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

/* Like is_within, but resolve root first (e.g. /etc -> /private/etc). */
static int is_within_real(const char *path, const char *root) {
    char resolved[PATH_MAX];

    if (realpath(root, resolved)) {
        return is_within(path, resolved);
    }
    return is_within(path, root);
}

/* True when path is inside root but is not root itself. */
static int is_inside_real(const char *path, const char *root) {
    char resolved[PATH_MAX];

    if (!realpath(root, resolved)) return 0;
    return is_within(path, resolved) && strcmp(path, resolved) != 0;
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
    if (is_inside_real(target, "/tmp")) return 1;
    if (is_inside_real(target, "/var/tmp")) return 1;
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
    if (is_within_real(path, "/var/tmp")) return 0;
    if (is_within_real(path, "/var")) return 1;
    for (i = 0; roots[i] != NULL; i++) {
        if (is_within_real(path, roots[i])) return 1;
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

/* Remove a path whether it is a file, symlink, or directory. */
static void remove_path(const char *path) {
    struct stat st;

    if (lstat(path, &st) != 0) return;
    if (S_ISDIR(st.st_mode) && !S_ISLNK(st.st_mode)) {
        remove_directory(path);
    } else if (unlink(path) != 0) {
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

/* ------------------------------------------------------------------------- */
/* Output, root, and lookup helpers                                          */
/* ------------------------------------------------------------------------- */

static int use_color(void) {
    const char *no = getenv("NO_COLOR");

    if (no && *no) return 0;
    return isatty(STDOUT_FILENO) ? 1 : 0;
}

static int env_truthy(const char *name) {
    const char *value = getenv(name);

    if (!value || !*value) return 0;
    if (strcmp(value, "0") == 0 || strcmp(value, "false") == 0 ||
        strcmp(value, "no") == 0) {
        return 0;
    }
    return 1;
}

static int refuse_root(const char *what) {
    if (geteuid() != 0) return 0;
    if (env_truthy("ILLUS_ALLOW_ROOT")) {
        fprintf(stderr,
                "[Dreams] Warning: running %s as root (ILLUS_ALLOW_ROOT is set).\n",
                what);
        return 0;
    }
    fprintf(stderr, "[Dreams] Error: refusing to run %s as root.\n", what);
    fprintf(stderr,
            "[Dreams] Run illus as your normal user. Set ILLUS_ALLOW_ROOT=1 only "
            "in a trusted container.\n");
    return 1;
}

static int find_in_path(const char *name, char *out, size_t out_size) {
    const char *path = getenv("PATH");

    if (!path || !*path) return -1;
    for (;;) {
        const char *colon = strchr(path, ':');
        size_t len = colon ? (size_t)(colon - path) : strlen(path);
        char dir[PATH_MAX];
        char candidate[PATH_MAX];

        if (len == 0) {
            dir[0] = '.';
            dir[1] = '\0';
        } else if (len >= sizeof dir) {
            if (!colon) break;
            path = colon + 1;
            continue;
        } else {
            memcpy(dir, path, len);
            dir[len] = '\0';
        }
        if (path_join(candidate, sizeof candidate, dir, name) == 0 &&
            access(candidate, X_OK) == 0) {
            if (strlen(candidate) + 1 > out_size) return -1;
            strcpy(out, candidate);
            return 0;
        }
        if (!colon) break;
        path = colon + 1;
    }
    return -1;
}

static int dir_on_path(const char *dir) {
    const char *path = getenv("PATH");
    char resolved_dir[PATH_MAX];
    const char *want = dir;

    if (!path || !*path) return 0;
    if (realpath(dir, resolved_dir)) want = resolved_dir;
    for (;;) {
        const char *colon = strchr(path, ':');
        size_t len = colon ? (size_t)(colon - path) : strlen(path);
        char entry[PATH_MAX];
        char resolved[PATH_MAX];
        const char *cmp = entry;

        if (len == 0) {
            entry[0] = '.';
            entry[1] = '\0';
        } else if (len >= sizeof entry) {
            if (!colon) break;
            path = colon + 1;
            continue;
        } else {
            memcpy(entry, path, len);
            entry[len] = '\0';
        }
        if (realpath(entry, resolved)) cmp = resolved;
        if (strcmp(cmp, want) == 0) return 1;
        if (!colon) break;
        path = colon + 1;
    }
    return 0;
}

static int tool_available(const char *name) {
    char path[PATH_MAX];

    return find_in_path(name, path, sizeof path) == 0;
}

/* Run a command and capture its standard output. */
static int run_cmd_capture(char *const argv[], char *out, size_t out_size) {
    int fds[2];
    pid_t pid;
    int status;
    size_t used = 0;

    if (!out || out_size == 0) return -1;
    out[0] = '\0';
    if (pipe(fds) != 0) {
        perror("[Dreams] Error: pipe failed");
        return -1;
    }
    fflush(NULL);
    pid = fork();
    if (pid < 0) {
        perror("[Dreams] Error: fork failed");
        close(fds[0]);
        close(fds[1]);
        return -1;
    }
    if (pid == 0) {
        close(fds[0]);
        if (dup2(fds[1], STDOUT_FILENO) < 0) _exit(127);
        close(fds[1]);
        execvp(argv[0], argv);
        _exit(127);
    }
    close(fds[1]);
    for (;;) {
        char buf[512];
        ssize_t n = read(fds[0], buf, sizeof buf);

        if (n < 0) {
            if (errno == EINTR) continue;
            break;
        }
        if (n == 0) break;
        if (used + (size_t)n < out_size) {
            memcpy(out + used, buf, (size_t)n);
            used += (size_t)n;
        }
    }
    close(fds[0]);
    out[used] = '\0';
    if (waitpid(pid, &status, 0) < 0) return -1;
    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

static int hex_digest(const char *text, char *out, size_t out_size) {
    const char *p;
    size_t n = 0;

    if (out_size < 65) return -1;
    for (p = text; *p; p++) {
        if (!isxdigit((unsigned char)*p)) continue;
        out[n++] = *p;
        if (n == 64) {
            out[n] = '\0';
            return 0;
        }
    }
    return -1;
}

/* Compute a SHA-256 digest with whichever tool the platform provides. */
static int sha256_file(const char *path, char *out, size_t out_size) {
    char buf[1024];
    char *sum_argv[] = {"sha256sum", (char *)path, NULL};
    char *shasum_argv[] = {"shasum", "-a", "256", (char *)path, NULL};
    char *openssl_argv[] = {"openssl", "dgst", "-sha256", (char *)path, NULL};

    if (tool_available("sha256sum") &&
        run_cmd_capture(sum_argv, buf, sizeof buf) == 0 &&
        hex_digest(buf, out, out_size) == 0) {
        return 0;
    }
    if (tool_available("shasum") &&
        run_cmd_capture(shasum_argv, buf, sizeof buf) == 0 &&
        hex_digest(buf, out, out_size) == 0) {
        return 0;
    }
    if (tool_available("openssl") &&
        run_cmd_capture(openssl_argv, buf, sizeof buf) == 0 &&
        hex_digest(buf, out, out_size) == 0) {
        return 0;
    }
    return -1;
}

static int copy_file(const char *src, const char *dst) {
    FILE *in;
    FILE *out;
    char buf[8192];
    size_t n;
    int rc = 0;

    in = fopen(src, "rb");
    if (!in) {
        perror("[Dreams] Error: could not open source file");
        return -1;
    }
    out = fopen(dst, "wb");
    if (!out) {
        perror("[Dreams] Error: could not create destination file");
        fclose(in);
        return -1;
    }
    while ((n = fread(buf, 1, sizeof buf, in)) > 0) {
        if (fwrite(buf, 1, n, out) != n) {
            perror("[Dreams] Error: could not write destination file");
            rc = -1;
            break;
        }
    }
    if (ferror(in)) {
        perror("[Dreams] Error: could not read source file");
        rc = -1;
    }
    if (fclose(out) != 0) rc = -1;
    fclose(in);
    if (rc != 0) unlink(dst);
    return rc;
}

static int download_file(const char *url, const char *dest) {
    const char *local = url;

    if (strncmp(local, "file://", 7) == 0) local += 7;
    if (strstr(local, "://") == NULL && access(local, R_OK) == 0) {
        return copy_file(local, dest);
    }
    if (tool_available("curl")) {
        char *argv[] = {
            "curl", "-fL", "--retry", "3", "-o", (char *)dest, "--",
            (char *)url, NULL
        };
        return run_cmd(argv) == 0 ? 0 : -1;
    }
    if (tool_available("wget")) {
        char *argv[] = {
            "wget", "-q", "-O", (char *)dest, "--", (char *)url, NULL
        };
        return run_cmd(argv) == 0 ? 0 : -1;
    }
    fprintf(stderr,
            "[Dreams] Error: neither curl nor wget is available to download tarballs.\n");
    return -1;
}

static int has_suffix(const char *text, const char *suffix) {
    size_t a = strlen(text);
    size_t b = strlen(suffix);

    return a >= b && strcmp(text + a - b, suffix) == 0;
}

static int url_is_tarball(const char *url) {
    static const char *exts[] = {
        ".tar.gz", ".tgz", ".tar.xz", ".txz", ".tar.bz2", ".tbz2",
        ".tar.zst", ".tzst", ".tar", ".zip", NULL
    };
    size_t len;
    size_t i;

    if (!url) return 0;
    len = strlen(url);
    {
        const char *query = strchr(url, '?');
        if (query) len = (size_t)(query - url);
    }
    for (i = 0; exts[i] != NULL; i++) {
        size_t elen = strlen(exts[i]);
        if (len >= elen && strncmp(url + len - elen, exts[i], elen) == 0) {
            return 1;
        }
    }
    return 0;
}

static size_t strip_archive_suffix(char *name, size_t len) {
    static const char *exts[] = {
        ".tar.gz", ".tgz", ".tar.xz", ".txz", ".tar.bz2", ".tbz2",
        ".tar.zst", ".tzst", ".tar", ".zip", NULL
    };
    size_t i;

    for (i = 0; exts[i] != NULL; i++) {
        size_t elen = strlen(exts[i]);
        if (len >= elen && strcmp(name + len - elen, exts[i]) == 0) {
            name[len - elen] = '\0';
            return len - elen;
        }
    }
    return len;
}

static int extract_archive(const char *archive, const char *dest) {
    if (has_suffix(archive, ".zip")) {
        char *argv[] = {
            "unzip", "-q", (char *)archive, "-d", (char *)dest, NULL
        };
        if (!tool_available("unzip")) {
            fprintf(stderr, "[Dreams] Error: unzip is required for .zip sources.\n");
            return -1;
        }
        return run_cmd(argv) == 0 ? 0 : -1;
    }
    if (!tool_available("tar")) {
        fprintf(stderr, "[Dreams] Error: tar is required for tarball sources.\n");
        return -1;
    }
    {
        char *argv[] = {
            "tar", "-xf", (char *)archive, "-C", (char *)dest, NULL
        };
        return run_cmd(argv) == 0 ? 0 : -1;
    }
}

static void format_size(unsigned long long bytes, char *out, size_t size) {
    static const char *units[] = {"B", "KiB", "MiB", "GiB", "TiB"};
    double value = (double)bytes;
    int unit = 0;

    while (value >= 1024.0 && unit < 4) {
        value /= 1024.0;
        unit++;
    }
    if (unit == 0) {
        snprintf(out, size, "%llu B", bytes);
    } else {
        snprintf(out, size, "%.1f %s", value, units[unit]);
    }
}

static void json_string(FILE *f, const char *s) {
    const unsigned char *p = (const unsigned char *)s;

    fputc('"', f);
    for (; *p; p++) {
        switch (*p) {
        case '"':  fputs("\\\"", f); break;
        case '\\': fputs("\\\\", f); break;
        case '\b': fputs("\\b", f); break;
        case '\f': fputs("\\f", f); break;
        case '\n': fputs("\\n", f); break;
        case '\r': fputs("\\r", f); break;
        case '\t': fputs("\\t", f); break;
        default:
            if (*p < 0x20) {
                fprintf(f, "\\u%04x", *p);
            } else {
                fputc(*p, f);
            }
            break;
        }
    }
    fputc('"', f);
}

static int looks_like_url(const char *s) {
    size_t len;

    if (!s || !*s) return 0;
    if (strstr(s, "://") != NULL) return 1;
    if (strncmp(s, "git@", 4) == 0) return 1;
    len = strlen(s);
    if (len > 4 && strcmp(s + len - 4, ".git") == 0) return 1;
    return 0;
}

static int validate_repo_url(const char *url) {
    size_t i;

    if (!url || !*url) return -1;
    if (url[0] == '-') return -1;
    for (i = 0; url[i] != '\0'; i++) {
        unsigned char c = (unsigned char)url[i];
        if (c < 0x20 || c == 0x7f || isspace(c)) return -1;
    }
    return 0;
}

static int derive_repo_name(const char *url, char *out, size_t out_size) {
    size_t url_len;
    const char *name_end;
    const char *name_start;
    size_t name_len;

    if (!url || !*url) return -1;
    url_len = strlen(url);
    while (url_len > 0 && url[url_len - 1] == '/') url_len--;
    name_end = url + url_len;
    name_start = name_end;
    while (name_start > url && name_start[-1] != '/' && name_start[-1] != ':') {
        name_start--;
    }
    name_len = (size_t)(name_end - name_start);
    if (name_len == 0 || name_len >= out_size) return -1;
    memcpy(out, name_start, name_len);
    out[name_len] = '\0';
    if (name_len > 4 && strcmp(out + name_len - 4, ".git") == 0) {
        out[name_len - 4] = '\0';
        name_len -= 4;
    }
    strip_archive_suffix(out, name_len);
    return is_valid_name(out) ? 0 : -1;
}

/* ------------------------------------------------------------------------- */
/* String lists                                                              */
/* ------------------------------------------------------------------------- */

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


/* ------------------------------------------------------------------------- */
/* Manifest handling                                                         */
/* ------------------------------------------------------------------------- */

typedef struct {
    char package[256];
    char source[PATH_MAX];
    char branch[256];
    char installed[64];
    int version;
} manifest_meta;

static void copy_field(char *dst, size_t size, const char *src) {
    size_t len = strlen(src);

    if (len >= size) len = size - 1;
    memcpy(dst, src, len);
    dst[len] = '\0';
}

static void manifest_meta_clear(manifest_meta *meta) {
    memset(meta, 0, sizeof *meta);
}

static void manifest_parse_header(manifest_meta *meta, const char *line) {
    if (strcmp(line, MANIFEST_HEADER) == 0) {
        meta->version = 2;
    } else if (strncmp(line, "# package: ", 11) == 0) {
        copy_field(meta->package, sizeof meta->package, line + 11);
    } else if (strncmp(line, "# source: ", 10) == 0) {
        copy_field(meta->source, sizeof meta->source, line + 10);
    } else if (strncmp(line, "# branch: ", 10) == 0) {
        copy_field(meta->branch, sizeof meta->branch, line + 10);
    } else if (strncmp(line, "# installed: ", 13) == 0) {
        copy_field(meta->installed, sizeof meta->installed, line + 13);
    }
}

/* Resolve a manifest entry to an absolute path. */
static int manifest_resolve_entry(const char *line, const char *local_dir,
                                  char *out, size_t out_size) {
    if (!line || !*line) return -1;
    if (line[0] == '/') {
        if (strlen(line) + 1 > out_size) return -1;
        strcpy(out, line);
        return 0;
    }
    if (has_dotdot(line)) return -1;
    return path_join(out, out_size, local_dir, line);
}

/*
 * Read a manifest. Absolute paths are accepted for backward compatibility;
 * relative paths are resolved against local_dir. Entries outside local_dir
 * are counted in *unsafe and skipped. Returns 0 on success, -1 if the file
 * could not be opened.
 */
static int manifest_read(strlist *files, manifest_meta *meta, size_t *unsafe,
                         const char *manifest, const char *local_dir,
                         const char *skip_dir) {
    FILE *f;
    char line[PATH_MAX];
    size_t bad = 0;

    if (meta) manifest_meta_clear(meta);
    if (unsafe) *unsafe = 0;
    f = fopen(manifest, "r");
    if (!f) return -1;
    while (fgets(line, sizeof line, f)) {
        char resolved[PATH_MAX];

        line[strcspn(line, "\r\n")] = '\0';
        if (!line[0]) continue;
        if (line[0] == '#') {
            if (meta) manifest_parse_header(meta, line);
            continue;
        }
        if (manifest_resolve_entry(line, local_dir, resolved,
                                   sizeof resolved) != 0) {
            bad++;
            continue;
        }
        if (!is_within(resolved, local_dir)) {
            bad++;
            continue;
        }
        if (skip_dir && is_within(resolved, skip_dir)) continue;
        if (strlist_add(files, resolved) != 0) break;
    }
    fclose(f);
    if (unsafe) *unsafe = bad;
    return 0;
}

/* Drop entries whose files no longer exist (used when upgrading). */
static void manifest_filter_existing(strlist *files) {
    size_t i;
    size_t out = 0;

    for (i = 0; i < files->len; i++) {
        struct stat st;
        if (lstat(files->items[i], &st) == 0) {
            files->items[out++] = files->items[i];
        } else {
            free(files->items[i]);
        }
    }
    files->len = out;
}

/* Convert an absolute path under root to a relative manifest entry. */
static int path_relative(const char *path, const char *root,
                         char *out, size_t out_size) {
    size_t root_len = strlen(root);
    const char *rel;

    if (!is_within(path, root)) return -1;
    rel = path + root_len;
    if (*rel == '/') rel++;
    if (!*rel) return -1;
    if (strlen(rel) + 1 > out_size) return -1;
    strcpy(out, rel);
    return 0;
}

/*
 * Write a manifest atomically: create a temporary file in the same directory,
 * fsync it, then rename it over the old manifest. Relative paths are stored
 * whenever the file lives under local_dir.
 */
static int manifest_write_atomic(const char *manifest_file, const char *local_dir,
                                 const manifest_meta *meta,
                                 const strlist *files) {
    char dir[PATH_MAX];
    char tmpl[PATH_MAX];
    int fd;
    FILE *f;
    size_t i;
    int failed = 0;

    if (parent_dir(manifest_file, dir, sizeof dir) != 0) return -1;
    if (path_join(tmpl, sizeof tmpl, dir, ".manifest-XXXXXX") != 0) return -1;
    fd = mkstemp(tmpl);
    if (fd < 0) {
        perror("[Dreams] Error: could not create temporary manifest");
        return -1;
    }
    f = fdopen(fd, "w");
    if (!f) {
        perror("[Dreams] Error: could not open temporary manifest");
        close(fd);
        unlink(tmpl);
        return -1;
    }

    if (fprintf(f, "%s\n", MANIFEST_HEADER) < 0) failed = 1;
    if (meta) {
        if (meta->package[0] &&
            fprintf(f, "# package: %s\n", meta->package) < 0) failed = 1;
        if (meta->source[0] &&
            fprintf(f, "# source: %s\n", meta->source) < 0) failed = 1;
        if (meta->branch[0] &&
            fprintf(f, "# branch: %s\n", meta->branch) < 0) failed = 1;
        if (meta->installed[0] &&
            fprintf(f, "# installed: %s\n", meta->installed) < 0) failed = 1;
    }
    for (i = 0; i < files->len; i++) {
        char rel[PATH_MAX];
        const char *entry = files->items[i];

        if (path_relative(entry, local_dir, rel, sizeof rel) == 0) entry = rel;
        if (fprintf(f, "%s\n", entry) < 0) {
            failed = 1;
            break;
        }
    }
    if (fflush(f) != 0) failed = 1;
    if (fsync(fileno(f)) != 0) failed = 1;
    if (fclose(f) != 0) failed = 1;
    if (failed) {
        unlink(tmpl);
        return -1;
    }
    if (chmod(tmpl, 0644) != 0) {
        /* best effort */
    }
    if (rename(tmpl, manifest_file) != 0) {
        perror("[Dreams] Error: could not replace manifest");
        unlink(tmpl);
        return -1;
    }
    return 0;
}

/* Move a staged install tree into place, recording every final file path. */
static int move_tree(const char *src_root, const char *dst_root, strlist *files,
                     uid_t uid, gid_t gid) {
    DIR *d;
    struct dirent *p;
    int rc = 0;

    d = opendir(src_root);
    if (!d) {
        perror("[Dreams] Error: could not read staged install");
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
                if (unlink(dst) != 0) {
                    perror("[Dreams] Error: could not replace destination");
                    rc = -1;
                    continue;
                }
            }
            if (mkdir_p(dst, st.st_mode & 0777) != 0) {
                perror("[Dreams] Error: could not create installation directory");
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
                            "[Dreams] Error: refusing to replace directory with file: %s\n",
                            dst);
                    rc = -1;
                    continue;
                } else if (unlink(dst) != 0) {
                    perror("[Dreams] Error: could not replace destination");
                    rc = -1;
                    continue;
                }
            }
            if (rename(src, dst) != 0) {
                perror("[Dreams] Error: could not install file");
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
            if (!line[0] || line[0] == '#') continue;
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

/*
 * Move a DESTDIR stage into ~/.local and write the package manifest.
 * Returns 0 only when every file moved and the manifest was written.
 */
static int track_install(const char *home, const char *pkg_name, const char *stage,
                         uid_t uid, gid_t gid, const manifest_meta *meta,
                         int allow_untracked) {
    char local_dir[PATH_MAX];
    char staged_local[PATH_MAX];
    char manifest_dir[PATH_MAX];
    char filename[PATH_MAX];
    char manifest_file[PATH_MAX];
    struct stat st;
    strlist files;
    int move_rc;
    int write_rc;

    memset(&files, 0, sizeof files);
    if (path_join(local_dir, sizeof local_dir, home, ".local") != 0) return -1;
    if (path_join(staged_local, sizeof staged_local, stage, local_dir) != 0) {
        return -1;
    }

    if (lstat(staged_local, &st) != 0 || !S_ISDIR(st.st_mode)) {
        if (allow_untracked) {
            fprintf(stderr,
                    "[Dreams] Warning: build system ignored DESTDIR; no manifest written.\n");
            return 0;
        }
        fprintf(stderr,
                "[Dreams] Error: build system ignored DESTDIR; refusing an untracked install.\n");
        fprintf(stderr,
                "[Dreams] Fix the install rule or pass --allow-untracked to accept it.\n");
        return -1;
    }

    if (!pkg_name || !*pkg_name) {
        move_rc = move_tree(staged_local, local_dir, &files, uid, gid);
        if (getuid() == 0) chown_with_parents(local_dir, home, uid, gid);
        strlist_free(&files);
        if (move_rc != 0) {
            fprintf(stderr, "[Dreams] Error: some staged files could not be installed.\n");
            return -1;
        }
        return 0;
    }

    if (path_join(manifest_dir, sizeof manifest_dir, home,
                  ".local/share/illus") != 0) {
        return -1;
    }
    if (mkdir_p(manifest_dir, 0755) != 0) {
        perror("[Dreams] Error: could not create manifest directory");
        return -1;
    }
    if (make_filename(filename, sizeof filename, pkg_name, ".txt") != 0) {
        return -1;
    }
    if (path_join(manifest_file, sizeof manifest_file, manifest_dir,
                  filename) != 0) {
        return -1;
    }

    /* Keep ownership of files from the previous version that still exist. */
    manifest_read(&files, NULL, NULL, manifest_file, local_dir, manifest_dir);
    manifest_filter_existing(&files);

    move_rc = move_tree(staged_local, local_dir, &files, uid, gid);
    if (move_rc != 0) {
        fprintf(stderr, "[Dreams] Error: some staged files could not be installed.\n");
    }

    write_rc = manifest_write_atomic(manifest_file, local_dir, meta, &files);
    if (write_rc != 0) {
        fprintf(stderr, "[Dreams] Error: could not write package manifest.\n");
    } else if (getuid() == 0) {
        chown_with_parents(manifest_file, local_dir, uid, gid);
    }

    if (move_rc == 0 && write_rc == 0) {
        printf("[Dreams] Manifest created for package: %s (%zu files)\n",
               pkg_name, files.len);
    }
    strlist_free(&files);
    return (move_rc == 0 && write_rc == 0) ? 0 : -1;
}

/* ------------------------------------------------------------------------- */
/* Git helpers                                                               */
/* ------------------------------------------------------------------------- */

static int git_clone(const illus_options *opts, const char *url,
                     const char *target) {
    char *argv[16];
    char depth[32];
    int n = 0;

    argv[n++] = "git";
    argv[n++] = "clone";
    if (opts->depth > 0) {
        snprintf(depth, sizeof depth, "%d", opts->depth);
        argv[n++] = "--depth";
        argv[n++] = depth;
    }
    if (opts->branch && *opts->branch) {
        argv[n++] = "--branch";
        argv[n++] = (char *)opts->branch;
    }
    if (opts->recursive) argv[n++] = "--recursive";
    /* "--" prevents a URL that looks like an option from being parsed as one. */
    argv[n++] = "--";
    argv[n++] = (char *)url;
    argv[n++] = (char *)target;
    argv[n] = NULL;
    return run_cmd(argv);
}

static int git_pull_ff(const char *dir) {
    char *argv[] = {"git", "-C", (char *)dir, "pull", "--ff-only", NULL};
    return run_cmd(argv);
}

/*
 * Fetch and check out the requested ref, then fast-forward it when it is a
 * branch. Tags and raw commits end up detached, where "git pull" does not
 * apply; the checkout itself is the update in that case.
 */
static int git_update_ref(const char *dir, const char *ref) {
    char *fetch_argv[] = {
        "git", "-C", (char *)dir, "fetch", "--all", "--tags", NULL
    };
    char *checkout_argv[] = {
        "git", "-C", (char *)dir, "checkout", (char *)ref, NULL
    };
    char *symbolic_argv[] = {
        "git", "-C", (char *)dir, "symbolic-ref", "-q", "HEAD", NULL
    };

    if (!ref || !*ref) return git_pull_ff(dir);
    if (run_cmd(fetch_argv) != 0) {
        fprintf(stderr, "[Dreams] Error: could not fetch updates for %s.\n", dir);
        return -1;
    }
    if (run_cmd(checkout_argv) != 0) {
        fprintf(stderr, "[Dreams] Error: could not check out '%s'.\n", ref);
        return -1;
    }
    /* symbolic-ref exits non-zero for a detached HEAD (tag or commit). */
    if (run_cmd(symbolic_argv) != 0) return 0;
    return git_pull_ff(dir);
}

/* Move a prepared directory into place, replacing an existing one on request. */
static int swap_dir_into_place(const char *src, const char *target, int force,
                               const char *home, uid_t uid, gid_t gid) {
    char parent[PATH_MAX];
    char backup[PATH_MAX];
    struct stat st;

    if (parent_dir(target, parent, sizeof parent) != 0) return -1;
    if (lstat(target, &st) == 0) {
        if (!force) {
            fprintf(stderr, "[Dreams] Error: destination already exists: %s\n",
                    target);
            fprintf(stderr, "[Dreams] Use --force to replace it.\n");
            return -1;
        }
        if (path_join(backup, sizeof backup, parent, ".illus-old-XXXXXX") != 0 ||
            !mkdtemp(backup)) {
            perror("[Dreams] Error: could not create backup directory");
            return -1;
        }
        if (rmdir(backup) != 0) {
            /* best effort */
        }
        if (rename(target, backup) != 0) {
            perror("[Dreams] Error: could not move existing destination aside");
            return -1;
        }
        if (rename(src, target) != 0) {
            perror("[Dreams] Error: could not move new directory into place");
            rename(backup, target);
            return -1;
        }
        remove_path(backup);
    } else if (rename(src, target) != 0) {
        perror("[Dreams] Error: could not move directory into place");
        return -1;
    }

    if (getuid() == 0) {
        recursive_chown(target, uid, gid);
        chown_with_parents(target, home, uid, gid);
    }
    return 0;
}

/* Clone into a temporary sibling, then swap it into place atomically. */
static int clone_atomic(const illus_options *opts, const char *url,
                        const char *target, int force, const char *home,
                        uid_t uid, gid_t gid) {
    char parent[PATH_MAX];
    char tmp[PATH_MAX];

    if (parent_dir(target, parent, sizeof parent) != 0) return -1;
    if (path_join(tmp, sizeof tmp, parent, ".illus-clone-XXXXXX") != 0) return -1;
    if (!mkdtemp(tmp)) {
        perror("[Dreams] Error: could not create clone directory");
        return -1;
    }

    printf("[Dreams] Cloning into %s...\n", target);
    if (git_clone(opts, url, tmp) != 0) {
        remove_directory(tmp);
        fprintf(stderr, "[Dreams] Error: git clone failed.\n");
        return -1;
    }

    if (swap_dir_into_place(tmp, target, force, home, uid, gid) != 0) {
        remove_directory(tmp);
        return -1;
    }
    return 0;
}

static int hex_equal(const char *a, const char *b) {
    size_t i;

    for (i = 0; i < 64; i++) {
        if (tolower((unsigned char)a[i]) != tolower((unsigned char)b[i])) {
            return 0;
        }
    }
    return 1;
}

/* Download, verify, extract, and atomically place a tarball source. */
static int fetch_tarball_atomic(const char *url, const char *sha256,
                                const char *target, int force,
                                const char *home, uid_t uid, gid_t gid) {
    char parent[PATH_MAX];
    char tmp[PATH_MAX];
    char archive[PATH_MAX];
    char extract[PATH_MAX];
    char actual[PATH_MAX];
    char single[256] = "";
    char digest[65];
    DIR *d;
    struct dirent *p;
    int entries = 0;
    int only_dir = 1;
    struct stat st;

    if (parent_dir(target, parent, sizeof parent) != 0) return -1;
    if (path_join(tmp, sizeof tmp, parent, ".illus-clone-XXXXXX") != 0) return -1;
    if (!mkdtemp(tmp)) {
        perror("[Dreams] Error: could not create download directory");
        return -1;
    }
    if (path_join(archive, sizeof archive, tmp, "source.archive") != 0 ||
        path_join(extract, sizeof extract, tmp, "src") != 0) {
        remove_directory(tmp);
        return -1;
    }

    printf("[Dreams] Downloading %s...\n", url);
    if (download_file(url, archive) != 0) {
        fprintf(stderr, "[Dreams] Error: could not download %s.\n", url);
        remove_directory(tmp);
        return -1;
    }
    if (sha256_file(archive, digest, sizeof digest) != 0) {
        fprintf(stderr,
                "[Dreams] Error: no SHA-256 tool found (sha256sum, shasum, or openssl).\n");
        remove_directory(tmp);
        return -1;
    }
    if (!hex_equal(digest, sha256)) {
        fprintf(stderr, "[Dreams] Error: SHA-256 mismatch for %s.\n", url);
        fprintf(stderr, "[Dreams] Expected: %s\n", sha256);
        fprintf(stderr, "[Dreams] Got:      %s\n", digest);
        remove_directory(tmp);
        return -1;
    }

    if (mkdir(extract, 0755) != 0) {
        perror("[Dreams] Error: could not create extraction directory");
        remove_directory(tmp);
        return -1;
    }
    if (extract_archive(archive, extract) != 0) {
        fprintf(stderr, "[Dreams] Error: could not extract %s.\n", url);
        remove_directory(tmp);
        return -1;
    }

    d = opendir(extract);
    if (!d) {
        perror("[Dreams] Error: could not read extracted archive");
        remove_directory(tmp);
        return -1;
    }
    while ((p = readdir(d)) != NULL) {
        char candidate[PATH_MAX];

        if (strcmp(p->d_name, ".") == 0 || strcmp(p->d_name, "..") == 0) {
            continue;
        }
        entries++;
        if (entries == 1) copy_field(single, sizeof single, p->d_name);
        if (path_join(candidate, sizeof candidate, extract, p->d_name) != 0 ||
            lstat(candidate, &st) != 0 || !S_ISDIR(st.st_mode)) {
            only_dir = 0;
        }
        if (entries > 1) only_dir = 0;
    }
    closedir(d);

    if (entries == 0) {
        fprintf(stderr, "[Dreams] Error: archive %s was empty.\n", url);
        remove_directory(tmp);
        return -1;
    }
    if (only_dir && single[0]) {
        if (path_join(actual, sizeof actual, extract, single) != 0) {
            remove_directory(tmp);
            return -1;
        }
    } else {
        copy_field(actual, sizeof actual, extract);
    }

    if (swap_dir_into_place(actual, target, force, home, uid, gid) != 0) {
        remove_directory(tmp);
        return -1;
    }
    remove_directory(tmp);
    return 0;
}

/* Resolve the destination for pull/install, enforcing the same safety rules. */
static int resolve_pull_target(const illus_options *opts, const char *home,
                               const char *name, char *target,
                               size_t target_size) {
    if (opts->dir && *opts->dir) {
        char *expanded = expand_path(opts->dir, home);
        char parent[PATH_MAX];
        char cwd[PATH_MAX];

        if (!expanded) {
            fprintf(stderr, "[Dreams] Error: invalid --dir path.\n");
            return -1;
        }
        if (has_dotdot(expanded)) {
            fprintf(stderr, "[Dreams] Error: --dir must not contain '..'.\n");
            free(expanded);
            return -1;
        }
        if (is_protected_target(expanded, home)) {
            fprintf(stderr,
                    "[Dreams] Error: refusing to use protected directory: %s\n",
                    expanded);
            free(expanded);
            return -1;
        }
        if (getcwd(cwd, sizeof cwd) && strcmp(expanded, cwd) == 0) {
            fprintf(stderr,
                    "[Dreams] Error: refusing to use the current directory as a destination.\n");
            free(expanded);
            return -1;
        }
        if (parent_dir(expanded, parent, sizeof parent) != 0) {
            fprintf(stderr, "[Dreams] Error: destination path is too long.\n");
            free(expanded);
            return -1;
        }
        if (check_existing_ancestor(parent, home) != 0) {
            fprintf(stderr,
                    "[Dreams] Error: refusing to create destination under system path: %s\n",
                    parent);
            free(expanded);
            return -1;
        }
        if (!opts->dry_run && mkdir_p(parent, 0755) != 0) {
            perror("[Dreams] Error: could not create destination parent");
            free(expanded);
            return -1;
        }
        if (!opts->dry_run) {
            char resolved_parent[PATH_MAX];
            char resolved_home[PATH_MAX];
            const char *safe_home = home;

            if (realpath(home, resolved_home)) safe_home = resolved_home;
            if (!realpath(parent, resolved_parent)) {
                perror("[Dreams] Error: could not resolve destination parent");
                free(expanded);
                return -1;
            }
            if (is_system_area(resolved_parent) &&
                !is_within(resolved_parent, safe_home)) {
                fprintf(stderr,
                        "[Dreams] Error: refusing to clone into system directory: %s\n",
                        resolved_parent);
                free(expanded);
                return -1;
            }
        }
        if (strlen(expanded) + 1 > target_size) {
            fprintf(stderr, "[Dreams] Error: destination path is too long.\n");
            free(expanded);
            return -1;
        }
        strcpy(target, expanded);
        free(expanded);
        return 0;
    }

    {
        char dreams_dir[PATH_MAX];
        char target_buf[PATH_MAX];
        struct stat st;

        if (path_join(dreams_dir, sizeof dreams_dir, home, "Dreams") != 0 ||
            path_join(target_buf, sizeof target_buf, dreams_dir, name) != 0) {
            fprintf(stderr, "[Dreams] Error: destination path is too long.\n");
            return -1;
        }
        if (lstat(dreams_dir, &st) == 0) {
            if (S_ISLNK(st.st_mode)) {
                fprintf(stderr,
                        "[Dreams] Error: ~/Dreams is a symlink; refusing to continue.\n");
                return -1;
            }
            if (!S_ISDIR(st.st_mode)) {
                fprintf(stderr,
                        "[Dreams] Error: ~/Dreams exists but is not a directory.\n");
                return -1;
            }
        } else if (!opts->dry_run && mkdir_p(dreams_dir, 0755) != 0) {
            perror("[Dreams] Error: could not create ~/Dreams");
            return -1;
        }
        if (strlen(target_buf) + 1 > target_size) {
            fprintf(stderr, "[Dreams] Error: destination path is too long.\n");
            return -1;
        }
        strcpy(target, target_buf);
        return 0;
    }
}

/* ------------------------------------------------------------------------- */
/* Pull command                                                              */
/* ------------------------------------------------------------------------- */

int cmd_pull(const illus_options *opts, const char *url) {
    uid_t uid = 0;
    gid_t gid = 0;
    char *home;
    char repo_name[256];
    char target[PATH_MAX];
    struct stat st;

    if (refuse_root("pull")) return 1;
    if (validate_repo_url(url) != 0) {
        fprintf(stderr, "[Dreams] Error: invalid repository URL.\n");
        return 1;
    }

    home = get_real_home(&uid, &gid);
    if (!home) {
        fprintf(stderr, "[Dreams] Error: Could not determine home directory.\n");
        return 1;
    }

    if (opts->name && *opts->name) {
        if (!is_valid_name(opts->name)) {
            fprintf(stderr, "[Dreams] Error: invalid --name '%s'.\n", opts->name);
            free(home);
            return 1;
        }
        strcpy(repo_name, opts->name);
    } else if (derive_repo_name(url, repo_name, sizeof repo_name) != 0) {
        fprintf(stderr, "[Dreams] Error: could not derive a repository name from the URL.\n");
        fprintf(stderr, "[Dreams] Use --name <pkg> to set one.\n");
        free(home);
        return 1;
    }

    if (resolve_pull_target(opts, home, repo_name, target,
                            sizeof target) != 0) {
        free(home);
        return 1;
    }

    if (lstat(target, &st) == 0) {
        if (!opts->force) {
            fprintf(stderr, "[Dreams] Error: destination already exists: %s\n",
                    target);
            fprintf(stderr, "[Dreams] Use --force to replace it.\n");
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
                free(home);
                return 1;
            }
            if (!replace_allowed(resolved_target, safe_home)) {
                fprintf(stderr,
                        "[Dreams] Error: refusing to recursively remove %s\n",
                        resolved_target);
                fprintf(stderr,
                        "[Dreams] Choose a destination inside your home directory or /tmp.\n");
                free(home);
                return 1;
            }
        }
        if (opts->dry_run) {
            printf("[Dreams] Dry run: would replace %s with a clone of %s\n",
                   target, url);
            free(home);
            return 0;
        }
    } else if (opts->dry_run) {
        printf("[Dreams] Dry run: would clone %s into %s\n", url, target);
        free(home);
        return 0;
    }

    if (clone_atomic(opts, url, target, opts->force, home, uid, gid) != 0) {
        free(home);
        return 1;
    }

    printf("[Dreams] Successfully pulled and stored in %s\n", target);
    free(home);
    return 0;
}

/* ------------------------------------------------------------------------- */
/* Cast command                                                              */
/* ------------------------------------------------------------------------- */

int cmd_cast(const illus_options *opts) {
    uid_t uid = 0;
    gid_t gid = 0;
    char *home;
    char workspace[PATH_MAX];
    char local_dir[PATH_MAX];
    char stage[PATH_MAX];
    char cmake_path[PATH_MAX];
    char makefile_path[PATH_MAX];
    char configure_path[PATH_MAX];
    char configure_ac_path[PATH_MAX];
    char configure_in_path[PATH_MAX];
    char makefile_am_path[PATH_MAX];
    char makefile_in_path[PATH_MAX];
    char build_dir[PATH_MAX];
    char pkg_name[256];
    char jobs_buf[32];
    const char *base;
    size_t base_len;
    int can_track;
    int is_cmake;
    int is_makefile;
    int is_autotools;
    int has_configure;
    int rc = 0;
    manifest_meta meta;

    if (refuse_root(opts->store ? "cast --store" : "cast")) return 1;

    home = get_real_home(&uid, &gid);
    if (!home) {
        fprintf(stderr, "[Dreams] Error: Could not determine home directory.\n");
        return 1;
    }
    if (!getcwd(workspace, sizeof workspace)) {
        fprintf(stderr,
                "[Dreams] Error: Could not determine current working directory.\n");
        free(home);
        return 1;
    }
    if (path_join(local_dir, sizeof local_dir, home, ".local") != 0 ||
        path_join(cmake_path, sizeof cmake_path, workspace,
                  "CMakeLists.txt") != 0 ||
        path_join(makefile_path, sizeof makefile_path, workspace,
                  "Makefile") != 0 ||
        path_join(configure_path, sizeof configure_path, workspace,
                  "configure") != 0 ||
        path_join(configure_ac_path, sizeof configure_ac_path, workspace,
                  "configure.ac") != 0 ||
        path_join(configure_in_path, sizeof configure_in_path, workspace,
                  "configure.in") != 0 ||
        path_join(makefile_am_path, sizeof makefile_am_path, workspace,
                  "Makefile.am") != 0 ||
        path_join(makefile_in_path, sizeof makefile_in_path, workspace,
                  "Makefile.in") != 0 ||
        path_join(build_dir, sizeof build_dir, workspace, "build") != 0) {
        fprintf(stderr, "[Dreams] Error: path is too long.\n");
        free(home);
        return 1;
    }

    is_cmake = (access(cmake_path, F_OK) == 0);
    is_makefile = (access(makefile_path, F_OK) == 0);
    has_configure = (access(configure_path, F_OK) == 0);
    is_autotools = has_configure ||
        ((access(configure_ac_path, F_OK) == 0 ||
          access(configure_in_path, F_OK) == 0) &&
         (access(makefile_am_path, F_OK) == 0 ||
          access(makefile_in_path, F_OK) == 0));

    if (opts->build_system && *opts->build_system &&
        strcmp(opts->build_system, "auto") != 0) {
        if (strcmp(opts->build_system, "cmake") == 0) {
            is_makefile = 0;
            is_autotools = 0;
            if (!is_cmake) {
                fprintf(stderr,
                        "[Dreams] Error: --build-system=cmake but no CMakeLists.txt found.\n");
                free(home);
                return 1;
            }
        } else if (strcmp(opts->build_system, "autotools") == 0) {
            is_cmake = 0;
            is_makefile = 0;
            if (!is_autotools) {
                fprintf(stderr,
                        "[Dreams] Error: --build-system=autotools but no configure or "
                        "configure.ac found.\n");
                free(home);
                return 1;
            }
        } else if (strcmp(opts->build_system, "make") == 0) {
            is_cmake = 0;
            is_autotools = 0;
            if (!is_makefile) {
                fprintf(stderr,
                        "[Dreams] Error: --build-system=make but no Makefile found.\n");
                free(home);
                return 1;
            }
        } else {
            fprintf(stderr,
                    "[Dreams] Error: --build-system must be 'auto', 'make', 'cmake', "
                    "or 'autotools'.\n");
            free(home);
            return 1;
        }
    } else if (is_cmake) {
        /* CMake wins when a project ships both CMake and Autotools files. */
        is_autotools = 0;
    } else if (is_autotools) {
        is_makefile = 0;
    }

    if (!is_cmake && !is_autotools && !is_makefile) {
        fprintf(stderr,
                "[Dreams] Error: No recognized build system found in current directory.\n");
        free(home);
        return 1;
    }

    if (opts->name && *opts->name) {
        if (!is_valid_name(opts->name)) {
            fprintf(stderr, "[Dreams] Error: invalid --name '%s'.\n", opts->name);
            free(home);
            return 1;
        }
        strcpy(pkg_name, opts->name);
        can_track = 1;
    } else {
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
    }

    if (opts->store && !can_track && !opts->allow_untracked) {
        fprintf(stderr,
                "[Dreams] Error: cannot determine a valid package name from '%s'.\n",
                workspace);
        fprintf(stderr, "[Dreams] Use --name <pkg> or --allow-untracked.\n");
        free(home);
        return 1;
    }

    if (opts->jobs > 0) {
        snprintf(jobs_buf, sizeof jobs_buf, "%d", opts->jobs);
    }

    manifest_meta_clear(&meta);
    copy_field(meta.package, sizeof meta.package, pkg_name);
    if (opts->source && *opts->source) {
        copy_field(meta.source, sizeof meta.source, opts->source);
    } else {
        copy_field(meta.source, sizeof meta.source, workspace);
    }
    if (opts->branch && *opts->branch) {
        copy_field(meta.branch, sizeof meta.branch, opts->branch);
    }
    {
        time_t now = time(NULL);
        struct tm *tmv = gmtime(&now);
        if (tmv) {
            strftime(meta.installed, sizeof meta.installed,
                     "%Y-%m-%dT%H:%M:%SZ", tmv);
        }
    }

    if (opts->fresh) {
        struct stat st;

        printf("[Dreams] Sweeping clean build artifacts (--fresh)...\n");
        if (lstat(build_dir, &st) == 0) {
            if (S_ISDIR(st.st_mode) && !S_ISLNK(st.st_mode)) {
                remove_directory(build_dir);
            } else if (unlink(build_dir) != 0) {
                perror("[Dreams] Warning: could not remove build path");
            }
        }
        if (is_autotools && access(makefile_path, F_OK) == 0) {
            char *const distclean_argv[] = {"make", "distclean", NULL};
            if (run_cmd(distclean_argv) != 0) {
                fprintf(stderr, "[Dreams] Warning: 'make distclean' failed.\n");
            }
        } else if (is_makefile) {
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
        if (opts->jobs > 0) {
            char *const build_argv[] = {
                "cmake", "--build", build_dir, "--parallel", jobs_buf, NULL
            };
            if (run_cmd(build_argv) != 0) {
                fprintf(stderr, "[Dreams] Error: build failed.\n");
                free(home);
                return 1;
            }
        } else {
            char *const build_argv[] = {"cmake", "--build", build_dir, NULL};
            if (run_cmd(build_argv) != 0) {
                fprintf(stderr, "[Dreams] Error: build failed.\n");
                free(home);
                return 1;
            }
        }
        if (opts->store) {
            char *const install_argv[] = {
                "cmake", "--install", build_dir, NULL
            };

            if (make_stage_dir(home, stage, sizeof stage) != 0) {
                fprintf(stderr,
                        "[Dreams] Error: could not create staging directory.\n");
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
                if (track_install(home, pkg_name, stage, uid, gid, &meta,
                                  opts->allow_untracked) != 0) {
                    rc = -1;
                }
            } else {
                fprintf(stderr,
                        "[Dreams] Warning: installing without a manifest (--allow-untracked).\n");
                if (track_install(home, NULL, stage, uid, gid, NULL, 1) != 0) {
                    rc = -1;
                }
            }
            remove_directory(stage);
            if (rc != 0) {
                free(home);
                return 1;
            }
        }
    } else if (is_autotools) {
        char prefix_arg[PATH_MAX];
        const char *opt = "--prefix=";
        size_t opt_len = strlen(opt);
        size_t dir_len = strlen(local_dir);

        printf("[Dreams] Detected Autotools build system.\n");
        if (!has_configure) {
            char *const autoreconf_argv[] = {"autoreconf", "-fi", NULL};

            if (!tool_available("autoreconf")) {
                fprintf(stderr,
                        "[Dreams] Error: configure is missing and autoreconf is not installed.\n");
                fprintf(stderr,
                        "[Dreams] Install autoconf, automake, and libtool, or use a "
                        "release tarball that ships configure.\n");
                free(home);
                return 1;
            }
            printf("[Dreams] Bootstrapping configure with autoreconf...\n");
            if (run_cmd(autoreconf_argv) != 0) {
                fprintf(stderr, "[Dreams] Error: autoreconf failed.\n");
                free(home);
                return 1;
            }
        }
        if (opt_len + dir_len + 1 > sizeof prefix_arg) {
            fprintf(stderr, "[Dreams] Error: install prefix is too long.\n");
            free(home);
            return 1;
        }
        memcpy(prefix_arg, opt, opt_len);
        memcpy(prefix_arg + opt_len, local_dir, dir_len + 1);

        {
            char *const config_argv[] = {"./configure", prefix_arg, NULL};
            if (run_cmd_extra(config_argv, opts->configure_args) != 0) {
                fprintf(stderr, "[Dreams] Error: Autotools configuration failed.\n");
                free(home);
                return 1;
            }
        }
        if (opts->jobs > 0) {
            char *const make_argv[] = {"make", "-j", jobs_buf, NULL};
            if (run_cmd_extra(make_argv, opts->make_args) != 0) {
                fprintf(stderr, "[Dreams] Error: build failed.\n");
                free(home);
                return 1;
            }
        } else {
            char *const make_argv[] = {"make", NULL};
            if (run_cmd_extra(make_argv, opts->make_args) != 0) {
                fprintf(stderr, "[Dreams] Error: build failed.\n");
                free(home);
                return 1;
            }
        }
        if (opts->store) {
            char destdir_env[PATH_MAX];
            const char *dopt = "DESTDIR=";
            size_t dopt_len = strlen(dopt);
            size_t stage_len;
            char *install_argv[4];

            if (make_stage_dir(home, stage, sizeof stage) != 0) {
                fprintf(stderr,
                        "[Dreams] Error: could not create staging directory.\n");
                free(home);
                return 1;
            }
            stage_len = strlen(stage);
            if (dopt_len + stage_len + 1 > sizeof destdir_env) {
                fprintf(stderr, "[Dreams] Error: install prefix is too long.\n");
                remove_directory(stage);
                free(home);
                return 1;
            }
            memcpy(destdir_env, dopt, dopt_len);
            memcpy(destdir_env + dopt_len, stage, stage_len + 1);

            install_argv[0] = "make";
            install_argv[1] = "install";
            install_argv[2] = destdir_env;
            install_argv[3] = NULL;

            printf("[Dreams] Storing package into %s (--store)...\n", local_dir);
            if (run_cmd_with_destdir_extra(install_argv, opts->make_args,
                                           stage) != 0) {
                fprintf(stderr, "[Dreams] Error: installation failed.\n");
                remove_directory(stage);
                free(home);
                return 1;
            }
            if (can_track) {
                if (track_install(home, pkg_name, stage, uid, gid, &meta,
                                  opts->allow_untracked) != 0) {
                    rc = -1;
                }
            } else {
                fprintf(stderr,
                        "[Dreams] Warning: installing without a manifest (--allow-untracked).\n");
                if (track_install(home, NULL, stage, uid, gid, NULL, 1) != 0) {
                    rc = -1;
                }
            }
            remove_directory(stage);
            if (rc != 0) {
                free(home);
                return 1;
            }
        }
    } else {
        printf("[Dreams] Detected Make build system.\n");
        if (opts->jobs > 0) {
            char *const make_argv[] = {"make", "-j", jobs_buf, NULL};
            if (run_cmd_extra(make_argv, opts->make_args) != 0) {
                fprintf(stderr, "[Dreams] Error: build failed.\n");
                free(home);
                return 1;
            }
        } else {
            char *const make_argv[] = {"make", NULL};
            if (run_cmd_extra(make_argv, opts->make_args) != 0) {
                fprintf(stderr, "[Dreams] Error: build failed.\n");
                free(home);
                return 1;
            }
        }
        if (opts->store) {
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
                fprintf(stderr,
                        "[Dreams] Error: could not create staging directory.\n");
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
            if (run_cmd_with_destdir_extra(install_argv, opts->make_args,
                                           stage) != 0) {
                fprintf(stderr, "[Dreams] Error: installation failed.\n");
                remove_directory(stage);
                free(home);
                return 1;
            }
            if (can_track) {
                if (track_install(home, pkg_name, stage, uid, gid, &meta,
                                  opts->allow_untracked) != 0) {
                    rc = -1;
                }
            } else {
                fprintf(stderr,
                        "[Dreams] Warning: installing without a manifest (--allow-untracked).\n");
                if (track_install(home, NULL, stage, uid, gid, NULL, 1) != 0) {
                    rc = -1;
                }
            }
            remove_directory(stage);
            if (rc != 0) {
                free(home);
                return 1;
            }
        }
    }

    if (getuid() == 0 && strlen(workspace) > strlen(home) + 1 &&
        is_within(workspace, home)) {
        recursive_chown(workspace, uid, gid);
    }

    printf("[Dreams] Successfully built%s project!\n",
           opts->store ? " and installed" : "");
    free(home);
    return rc;
}

/* ------------------------------------------------------------------------- */
/* Install command                                                           */
/* ------------------------------------------------------------------------- */

int cmd_install(const illus_options *opts, const char *spec) {
    uid_t uid = 0;
    gid_t gid = 0;
    char *home;
    char url[PATH_MAX];
    char name[256];
    char branch[256];
    char target[PATH_MAX];
    struct stat st;
    int is_tarball;
    int rc;

    if (refuse_root("install")) return 1;
    if (!spec || !*spec) {
        fprintf(stderr, "[Dreams] Error: install requires a repository or tarball URL.\n");
        return 1;
    }

    home = get_real_home(&uid, &gid);
    if (!home) {
        fprintf(stderr, "[Dreams] Error: Could not determine home directory.\n");
        return 1;
    }

    memset(url, 0, sizeof url);
    memset(name, 0, sizeof name);
    branch[0] = '\0';
    if (opts->branch && *opts->branch) {
        copy_field(branch, sizeof branch, opts->branch);
    }

    if (is_valid_name(spec) && !looks_like_url(spec) && !url_is_tarball(spec) &&
        strchr(spec, '/') == NULL && access(spec, F_OK) != 0) {
        fprintf(stderr,
                "[Dreams] Error: name-based install was removed; '%s' is not a URL.\n",
                spec);
        fprintf(stderr,
                "[Dreams] Pass a repository or tarball URL, for example:\n");
        fprintf(stderr, "[Dreams]   illus install https://github.com/owner/%s.git\n",
                spec);
        free(home);
        return 1;
    }

    copy_field(url, sizeof url, spec);
    if (opts->name && *opts->name) {
        if (!is_valid_name(opts->name)) {
            fprintf(stderr, "[Dreams] Error: invalid --name '%s'.\n", opts->name);
            free(home);
            return 1;
        }
        copy_field(name, sizeof name, opts->name);
    } else if (derive_repo_name(url, name, sizeof name) != 0) {
        fprintf(stderr,
                "[Dreams] Error: could not derive a package name from the URL.\n");
        fprintf(stderr, "[Dreams] Use --name <pkg> to set one.\n");
        free(home);
        return 1;
    }

    if (!is_valid_name(name)) {
        fprintf(stderr, "[Dreams] Error: invalid package name '%s'.\n", name);
        free(home);
        return 1;
    }
    if (validate_repo_url(url) != 0) {
        fprintf(stderr, "[Dreams] Error: invalid repository or tarball URL.\n");
        free(home);
        return 1;
    }

    is_tarball = url_is_tarball(url);
    if (is_tarball) {
        if (!opts->sha256 || strlen(opts->sha256) != 64) {
            fprintf(stderr,
                    "[Dreams] Error: tarball installs require --sha256 <64-char hex>.\n");
            free(home);
            return 1;
        }
        if (branch[0]) {
            fprintf(stderr,
                    "[Dreams] Error: --branch is not supported for tarball sources.\n");
            free(home);
            return 1;
        }
    }

    if (resolve_pull_target(opts, home, name, target, sizeof target) != 0) {
        free(home);
        return 1;
    }

    if (lstat(target, &st) == 0) {
        if (is_tarball) {
            char git_dir[PATH_MAX];

            if (!opts->force &&
                path_join(git_dir, sizeof git_dir, target, ".git") == 0 &&
                access(git_dir, F_OK) == 0) {
                fprintf(stderr,
                        "[Dreams] Error: destination is a git clone, not a tarball source: %s\n",
                        target);
                fprintf(stderr, "[Dreams] Use --force to replace it.\n");
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
                    free(home);
                    return 1;
                }
                if (!replace_allowed(resolved_target, safe_home)) {
                    fprintf(stderr,
                            "[Dreams] Error: refusing to replace %s\n",
                            resolved_target);
                    free(home);
                    return 1;
                }
            }
            if (opts->dry_run) {
                printf("[Dreams] Dry run: would refresh %s from %s\n", target, url);
                free(home);
                return 0;
            }
            if (fetch_tarball_atomic(url, opts->sha256, target, 1, home,
                                     uid, gid) != 0) {
                free(home);
                return 1;
            }
        } else if (opts->force) {
            if (S_ISDIR(st.st_mode) && !S_ISLNK(st.st_mode)) {
                char resolved_target[PATH_MAX];
                char resolved_home[PATH_MAX];
                const char *safe_home = home;

                if (realpath(home, resolved_home)) safe_home = resolved_home;
                if (!realpath(target, resolved_target)) {
                    perror("[Dreams] Error: could not resolve existing destination");
                    free(home);
                    return 1;
                }
                if (!replace_allowed(resolved_target, safe_home)) {
                    fprintf(stderr,
                            "[Dreams] Error: refusing to recursively remove %s\n",
                            resolved_target);
                    free(home);
                    return 1;
                }
            }
            if (opts->dry_run) {
                printf("[Dreams] Dry run: would replace %s with a clone of %s\n",
                       target, url);
                free(home);
                return 0;
            }
            if (clone_atomic(opts, url, target, 1, home, uid, gid) != 0) {
                free(home);
                return 1;
            }
        } else {
            char git_dir[PATH_MAX];

            if (path_join(git_dir, sizeof git_dir, target, ".git") != 0 ||
                access(git_dir, F_OK) != 0) {
                fprintf(stderr,
                        "[Dreams] Error: destination exists but is not a git clone: %s\n",
                        target);
                fprintf(stderr, "[Dreams] Use --force to replace it.\n");
                free(home);
                return 1;
            }
            if (opts->dry_run) {
                if (branch[0]) {
                    printf("[Dreams] Dry run: would check out '%s' and update %s\n",
                           branch, target);
                } else {
                    printf("[Dreams] Dry run: would update %s with git pull --ff-only\n",
                           target);
                }
            } else {
                printf("[Dreams] Updating %s...\n", target);
                if (git_update_ref(target, branch) != 0) {
                    fprintf(stderr, "[Dreams] Error: could not update %s.\n",
                            target);
                    free(home);
                    return 1;
                }
            }
        }
    } else {
        if (opts->dry_run) {
            if (is_tarball) {
                printf("[Dreams] Dry run: would download and extract %s into %s\n",
                       url, target);
            } else {
                printf("[Dreams] Dry run: would clone %s into %s\n", url, target);
            }
            printf("[Dreams] Dry run: would build and install package '%s'\n", name);
            free(home);
            return 0;
        }
        if (is_tarball) {
            if (fetch_tarball_atomic(url, opts->sha256, target, 0, home,
                                     uid, gid) != 0) {
                free(home);
                return 1;
            }
        } else if (clone_atomic(opts, url, target, 0, home, uid, gid) != 0) {
            free(home);
            return 1;
        }
    }

    if (opts->dry_run) {
        printf("[Dreams] Dry run: would %s package '%s'\n",
               opts->reinstall ? "clean rebuild and install" : "build and install",
               name);
        free(home);
        return 0;
    }

    {
        char cwd[PATH_MAX];
        illus_options cast_opts = *opts;

        if (!getcwd(cwd, sizeof cwd)) {
            perror("[Dreams] Error: could not determine current directory");
            free(home);
            return 1;
        }
        if (chdir(target) != 0) {
            perror("[Dreams] Error: could not enter package directory");
            free(home);
            return 1;
        }
        cast_opts.store = 1;
        cast_opts.fresh = opts->reinstall;
        cast_opts.name = name;
        cast_opts.source = url;
        cast_opts.branch = branch[0] ? branch : NULL;
        cast_opts.dir = NULL;
        cast_opts.dry_run = 0;
        rc = cmd_cast(&cast_opts);
        if (chdir(cwd) != 0) {
            perror("[Dreams] Warning: could not restore working directory");
        }
    }

    free(home);
    return rc;
}


/* ------------------------------------------------------------------------- */
/* Update command                                                            */
/* ------------------------------------------------------------------------- */

int cmd_update(const illus_options *opts) {
    char *home;
    char dreams_path[PATH_MAX];
    char cwd_buf[PATH_MAX];
    DIR *dir;
    struct dirent *entry;
    int found_repos = 0;
    int failures = 0;

    if (refuse_root("update")) return 1;

    home = get_real_home(NULL, NULL);
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
        fprintf(stderr,
                "[Dreams] Error: could not determine current directory.\n");
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
        if (path_join(repo_path, sizeof repo_path, dreams_path,
                      entry->d_name) != 0) {
            continue;
        }
        if (path_join(git_dir, sizeof git_dir, repo_path, ".git") != 0) continue;
        if (access(git_dir, F_OK) != 0) continue;

        found_repos = 1;
        if (opts->dry_run) {
            printf("[Dreams] Dry run: would update %s\n", entry->d_name);
            continue;
        }

        printf("[Dreams] Updating project: %s\n", entry->d_name);
        {
            char *const pull_argv[] = {
                "git", "-C", repo_path, "pull", "--ff-only", NULL
            };
            if (run_cmd(pull_argv) != 0) {
                fprintf(stderr, "[Dreams] Error: 'git pull' failed for %s.\n",
                        entry->d_name);
                failures++;
                continue;
            }
        }

        if (path_join(cmake_path, sizeof cmake_path, repo_path,
                      "CMakeLists.txt") != 0 ||
            path_join(makefile_path, sizeof makefile_path, repo_path,
                      "Makefile") != 0) {
            fprintf(stderr, "[Dreams] Warning: path too long for %s.\n",
                    entry->d_name);
            failures++;
            continue;
        }

        if (access(cmake_path, F_OK) == 0 ||
            access(makefile_path, F_OK) == 0) {
            illus_options cast_opts = *opts;

            cast_opts.store = 1;
            cast_opts.fresh = 0;
            cast_opts.source = NULL;
            cast_opts.branch = NULL;
            cast_opts.name = NULL;
            cast_opts.dir = NULL;
            cast_opts.dry_run = 0;

            if (chdir(repo_path) != 0) {
                perror("[Dreams] Error: could not enter project directory");
                failures++;
                continue;
            }
            if (cmd_cast(&cast_opts) != 0) {
                fprintf(stderr,
                        "[Dreams] Error: rebuild/install failed for %s.\n",
                        entry->d_name);
                failures++;
            } else {
                printf("[Dreams] Successfully updated and rebuilt %s!\n\n",
                       entry->d_name);
            }
            if (chdir(cwd_buf) != 0) {
                perror("[Dreams] Warning: could not restore working directory");
                failures++;
            }
        } else {
            printf("[Dreams] Pulled latest changes for %s "
                   "(no build system found, skipping build).\n\n",
                   entry->d_name);
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
    int color = use_color();

    if (color) printf("\033[36m");
    printf("  ___ _ _            \n");
    printf(" |_ _| | |_   _ ___  \n");
    printf("  | || | | | | / __| \n");
    printf("  | || | | |_| \\__ \\ \n");
    printf(" |___|_|_|\\__,_|___/ \n\n");
    if (color) printf("\033[0m");
    printf(" ✦ Dreams Package Manager (illus) v%s ✦\n", ILLUS_VERSION);
    return 0;
}

/* ------------------------------------------------------------------------- */
/* Shed command                                                              */
/* ------------------------------------------------------------------------- */

int cmd_shed(const illus_options *opts, const char *package_name) {
    char *home;
    char local_dir[PATH_MAX];
    char manifest_dir[PATH_MAX];
    char filename[PATH_MAX];
    char direct_manifest[PATH_MAX];
    char manifest_path[PATH_MAX] = "";
    char resolved_local[PATH_MAX];
    strlist files;
    manifest_meta meta;
    size_t unsafe = 0;
    int failures = 0;
    size_t i;
    FILE *f;

    if (refuse_root("remove")) return 1;
    if (!is_valid_name(package_name)) {
        fprintf(stderr, "[Dreams] Error: Invalid package name \"%s\"\n",
                package_name);
        return 1;
    }

    if (opts->dry_run) {
        printf("[Dreams] Dry run: would shed package: %s\n", package_name);
    } else {
        printf("[Dreams] Shedding (uninstalling) package: %s\n", package_name);
    }

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

    if (!realpath(local_dir, resolved_local)) {
        if (path_join(resolved_local, sizeof resolved_local, home,
                      ".local") != 0) {
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
            if (opts->dry_run) {
                printf("[Dreams] Dry run: would remove %s\n", fallback);
                free(home);
                return 0;
            }
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

    fclose(f);

    if (!realpath(local_dir, resolved_local)) {
        if (path_join(resolved_local, sizeof resolved_local, home,
                      ".local") != 0) {
            fprintf(stderr, "[Dreams] Error: path is too long.\n");
            free(home);
            return 1;
        }
    }

    memset(&files, 0, sizeof files);
    manifest_read(&files, &meta, &unsafe, manifest_path, resolved_local, NULL);
    if (unsafe > 0) {
        fprintf(stderr,
                "[Dreams] Error: manifest contains %zu unsafe entr%s; refusing to continue.\n",
                unsafe, unsafe == 1 ? "y" : "ies");
        strlist_free(&files);
        free(home);
        return 1;
    }

    if (opts->dry_run) {
        for (i = 0; i < files.len; i++) {
            printf("[Dreams] Would remove file: %s\n", files.items[i]);
        }
        if (failures == 0) {
            printf("[Dreams] Dry run: would remove %zu file(s) and manifest %s\n",
                   files.len, manifest_path);
        }
        strlist_free(&files);
        free(home);
        return failures > 0 ? 1 : 0;
    }

    for (i = 0; i < files.len; i++) {
        char resolved[PATH_MAX];

        if (resolve_for_delete(files.items[i], resolved,
                               sizeof resolved) != 0) {
            fprintf(stderr, "[Dreams] Warning: tracked file no longer exists: %s\n",
                    files.items[i]);
            continue;
        }
        if (!is_within(resolved, resolved_local)) {
            fprintf(stderr,
                    "[Dreams] Error: refusing to delete outside %s: %s\n",
                    resolved_local, files.items[i]);
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
    strlist_free(&files);

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

typedef struct {
    char name[256];
    char manifest[PATH_MAX];
    size_t file_count;
    size_t missing;
    size_t unsafe;
    unsigned long long bytes;
    manifest_meta meta;
} package_info;

static void package_scan(package_info *pkg, const char *local_dir) {
    strlist files;
    size_t i;

    memset(&files, 0, sizeof files);
    if (manifest_read(&files, &pkg->meta, &pkg->unsafe, pkg->manifest,
                      local_dir, NULL) != 0) {
        strlist_free(&files);
        return;
    }
    pkg->file_count = files.len;
    for (i = 0; i < files.len; i++) {
        struct stat st;
        if (lstat(files.items[i], &st) != 0) {
            pkg->missing++;
            continue;
        }
        if (S_ISREG(st.st_mode)) {
            pkg->bytes += (unsigned long long)st.st_size;
        }
    }
    strlist_free(&files);
}

static int package_compare(const void *a, const void *b) {
    const package_info *pa = (const package_info *)a;
    const package_info *pb = (const package_info *)b;

    return strcmp(pa->name, pb->name);
}

static int collect_packages(const char *manifest_dir, const char *local_dir,
                            package_info **out, size_t *out_count) {
    DIR *d;
    struct dirent *entry;
    package_info *items = NULL;
    size_t count = 0;
    size_t cap = 0;

    *out = NULL;
    *out_count = 0;
    d = opendir(manifest_dir);
    if (!d) {
        if (errno == ENOENT) return 0;
        perror("[Dreams] Error: could not read manifest directory");
        return -1;
    }
    while ((entry = readdir(d)) != NULL) {
        size_t len = strlen(entry->d_name);
        char manifest[PATH_MAX];
        package_info *grown;

        if (len < 5 || strcmp(entry->d_name + len - 4, ".txt") != 0) continue;
        if (len - 4 >= sizeof items[0].name) continue;
        if (path_join(manifest, sizeof manifest, manifest_dir,
                      entry->d_name) != 0) {
            continue;
        }
        if (count == cap) {
            size_t ncap = cap ? cap * 2 : 8;
            grown = realloc(items, ncap * sizeof *grown);
            if (!grown) {
                free(items);
                closedir(d);
                return -1;
            }
            items = grown;
            cap = ncap;
        }
        memset(&items[count], 0, sizeof items[count]);
        memcpy(items[count].name, entry->d_name, len - 4);
        items[count].name[len - 4] = '\0';
        strcpy(items[count].manifest, manifest);
        package_scan(&items[count], local_dir);
        count++;
    }
    closedir(d);
    if (count > 1) {
        qsort(items, count, sizeof *items, package_compare);
    }
    *out = items;
    *out_count = count;
    return 0;
}

static int binary_is_tracked(const package_info *pkgs, size_t count,
                             const char *binary) {
    size_t i;

    for (i = 0; i < count; i++) {
        FILE *f = fopen(pkgs[i].manifest, "r");
        char line[PATH_MAX];

        if (!f) continue;
        while (fgets(line, sizeof line, f)) {
            const char *base;
            line[strcspn(line, "\r\n")] = '\0';
            if (!line[0] || line[0] == '#') continue;
            base = strrchr(line, '/');
            base = base ? base + 1 : line;
            if (strcmp(base, binary) == 0) {
                fclose(f);
                return 1;
            }
        }
        fclose(f);
    }
    return 0;
}

static void manifest_package_name(const char *manifest, char *out,
                                  size_t out_size) {
    const char *base = strrchr(manifest, '/');
    size_t len;

    base = base ? base + 1 : manifest;
    len = strlen(base);
    if (len > 4 && strcmp(base + len - 4, ".txt") == 0) len -= 4;
    if (len >= out_size) len = out_size - 1;
    memcpy(out, base, len);
    out[len] = '\0';
}

static int string_compare(const void *a, const void *b) {
    return strcmp(*(const char *const *)a, *(const char *const *)b);
}

/* Collect and sort untracked executable names from ~/.local/bin. */
static int collect_untracked(const package_info *pkgs, size_t count,
                             const char *bin_dir, strlist *names) {
    DIR *d = opendir(bin_dir);
    struct dirent *entry;

    if (!d) return 0;
    while ((entry = readdir(d)) != NULL) {
        char path[PATH_MAX];
        struct stat st;

        if (entry->d_name[0] == '.') continue;
        if (path_join(path, sizeof path, bin_dir, entry->d_name) != 0) continue;
        if (lstat(path, &st) != 0 || S_ISDIR(st.st_mode)) continue;
        if (binary_is_tracked(pkgs, count, entry->d_name)) continue;
        if (strlist_add(names, entry->d_name) != 0) {
            closedir(d);
            return -1;
        }
    }
    closedir(d);
    if (names->len > 1) {
        qsort(names->items, names->len, sizeof *names->items, string_compare);
    }
    return 0;
}

/* ------------------------------------------------------------------------- */
/* List command                                                              */
/* ------------------------------------------------------------------------- */

int cmd_list(const illus_options *opts) {
    char *home = get_real_home(NULL, NULL);
    char local_dir[PATH_MAX];
    char manifest_dir[PATH_MAX];
    char bin_dir[PATH_MAX];
    package_info *pkgs = NULL;
    strlist untracked;
    size_t count = 0;
    size_t i;
    int verify_failed = 0;
    int color = use_color();
    int first_json = 1;

    memset(&untracked, 0, sizeof untracked);
    if (!home) {
        fprintf(stderr, "[Dreams] Error: Could not determine home directory.\n");
        return 1;
    }
    if (path_join(local_dir, sizeof local_dir, home, ".local") != 0 ||
        path_join(manifest_dir, sizeof manifest_dir, home,
                  ".local/share/illus") != 0 ||
        path_join(bin_dir, sizeof bin_dir, home, ".local/bin") != 0) {
        fprintf(stderr, "[Dreams] Error: path is too long.\n");
        free(home);
        return 1;
    }

    if (collect_packages(manifest_dir, local_dir, &pkgs, &count) != 0) {
        free(home);
        return 1;
    }
    if (opts->all && collect_untracked(pkgs, count, bin_dir, &untracked) != 0) {
        fprintf(stderr, "[Dreams] Error: could not read ~/.local/bin.\n");
        free(pkgs);
        free(home);
        return 1;
    }

    if (opts->json) {
        printf("[");
        for (i = 0; i < count; i++) {
            if (!first_json) printf(",");
            first_json = 0;
            printf("{\"name\":");
            json_string(stdout, pkgs[i].name);
            printf(",\"tracked\":true,\"files\":%zu,\"missing\":%zu,"
                   "\"unsafe\":%zu,\"bytes\":%llu",
                   pkgs[i].file_count, pkgs[i].missing, pkgs[i].unsafe,
                   pkgs[i].bytes);
            if (opts->verbose) {
                printf(",\"manifest\":");
                json_string(stdout, pkgs[i].manifest);
                if (pkgs[i].meta.source[0]) {
                    printf(",\"source\":");
                    json_string(stdout, pkgs[i].meta.source);
                }
                if (pkgs[i].meta.branch[0]) {
                    printf(",\"branch\":");
                    json_string(stdout, pkgs[i].meta.branch);
                }
                if (pkgs[i].meta.installed[0]) {
                    printf(",\"installed\":");
                    json_string(stdout, pkgs[i].meta.installed);
                }
            }
            printf("}");
        }
        for (i = 0; i < untracked.len; i++) {
            if (!first_json) printf(",");
            first_json = 0;
            printf("{\"name\":");
            json_string(stdout, untracked.items[i]);
            printf(",\"tracked\":false}");
        }
        printf("]\n");
    } else {
        printf("\n");
        if (color) {
            printf("\033[36m=== Tracked packages ===\033[0m\n");
        } else {
            printf("=== Tracked packages ===\n");
        }
        if (count == 0) {
            printf("  (none)\n");
        } else {
            printf("  %-24s %6s %10s  %s\n", "PACKAGE", "FILES", "SIZE",
                   "STATUS");
            for (i = 0; i < count; i++) {
                char sizebuf[32];

                format_size(pkgs[i].bytes, sizebuf, sizeof sizebuf);
                printf("  %-24s %6zu %10s  ", pkgs[i].name,
                       pkgs[i].file_count, sizebuf);
                if (pkgs[i].unsafe > 0) {
                    if (color) printf("\033[31m");
                    printf("%zu unsafe", pkgs[i].unsafe);
                    if (color) printf("\033[0m");
                } else if (pkgs[i].missing > 0) {
                    if (color) printf("\033[33m");
                    printf("%zu missing", pkgs[i].missing);
                    if (color) printf("\033[0m");
                } else {
                    if (color) printf("\033[32m");
                    printf("ok");
                    if (color) printf("\033[0m");
                }
                printf("\n");
                if (opts->verbose) {
                    printf("      manifest: %s\n", pkgs[i].manifest);
                    if (pkgs[i].meta.source[0]) {
                        printf("      source: %s\n", pkgs[i].meta.source);
                    }
                    if (pkgs[i].meta.branch[0]) {
                        printf("      branch: %s\n", pkgs[i].meta.branch);
                    }
                    if (pkgs[i].meta.installed[0]) {
                        printf("      installed: %s\n",
                               pkgs[i].meta.installed);
                    }
                }
            }
        }

        if (opts->all) {
            printf("\n");
            if (color) {
                printf("\033[36m=== Untracked binaries in ~/.local/bin ===\033[0m\n");
            } else {
                printf("=== Untracked binaries in ~/.local/bin ===\n");
            }
            if (untracked.len == 0) {
                printf("  (none)\n");
            } else {
                for (i = 0; i < untracked.len; i++) {
                    printf("  %s (untracked)\n", untracked.items[i]);
                }
            }
        }
        printf("\n");
    }

    if (opts->verify) {
        for (i = 0; i < count; i++) {
            if (pkgs[i].missing > 0 || pkgs[i].unsafe > 0) {
                verify_failed = 1;
            }
        }
        if (verify_failed) {
            fprintf(stderr,
                    "[Dreams] Verification failed: some tracked files are "
                    "missing or unsafe.\n");
        }
    }

    free(pkgs);
    strlist_free(&untracked);
    free(home);
    return verify_failed ? 1 : 0;
}

/* ------------------------------------------------------------------------- */
/* Info command                                                              */
/* ------------------------------------------------------------------------- */

int cmd_info(const illus_options *opts, const char *name) {
    char *home;
    char local_dir[PATH_MAX];
    char manifest_dir[PATH_MAX];
    char direct[PATH_MAX];
    char manifest[PATH_MAX] = "";
    char pkg_name[256];
    strlist files;
    manifest_meta meta;
    size_t unsafe = 0;
    size_t missing = 0;
    unsigned long long bytes = 0;
    size_t i;
    int found = 0;
    int via_binary = 0;

    if (!is_valid_name(name)) {
        fprintf(stderr, "[Dreams] Error: Invalid package name \"%s\"\n", name);
        return 1;
    }

    home = get_real_home(NULL, NULL);
    if (!home) {
        fprintf(stderr, "[Dreams] Error: Could not determine home directory.\n");
        return 1;
    }
    if (path_join(local_dir, sizeof local_dir, home, ".local") != 0 ||
        path_join(manifest_dir, sizeof manifest_dir, home,
                  ".local/share/illus") != 0) {
        fprintf(stderr, "[Dreams] Error: path is too long.\n");
        free(home);
        return 1;
    }

    {
        char filename[PATH_MAX];
        if (make_filename(filename, sizeof filename, name, ".txt") == 0 &&
            path_join(direct, sizeof direct, manifest_dir, filename) == 0 &&
            access(direct, R_OK) == 0) {
            copy_field(manifest, sizeof manifest, direct);
            found = 1;
        }
    }
    if (!found) {
        int matches = find_manifest_for_binary(manifest_dir, name, manifest,
                                               sizeof manifest);
        if (matches == -2) {
            fprintf(stderr,
                    "[Dreams] Error: multiple packages provide '%s'; use the package name.\n",
                    name);
            free(home);
            return 1;
        }
        if (matches == 1) {
            found = 1;
            via_binary = 1;
        }
    }

    if (!found) {
        if (opts->json) {
            printf("{\"name\":");
            json_string(stdout, name);
            printf(",\"installed\":false}\n");
        } else {
            printf("\nPackage: %s\n", name);
            printf("Status: not installed (no manifest found)\n");
            printf("\nInstall from a URL with: illus install <url> --name %s\n\n",
                   name);
        }
        free(home);
        return 0;
    }

    if (via_binary) {
        manifest_package_name(manifest, pkg_name, sizeof pkg_name);
    } else {
        copy_field(pkg_name, sizeof pkg_name, name);
    }

    memset(&files, 0, sizeof files);
    if (manifest_read(&files, &meta, &unsafe, manifest, local_dir, NULL) != 0) {
        perror("[Dreams] Error: could not read manifest");
        free(home);
        return 1;
    }
    for (i = 0; i < files.len; i++) {
        struct stat st;
        if (lstat(files.items[i], &st) != 0) {
            missing++;
            continue;
        }
        if (S_ISREG(st.st_mode)) {
            bytes += (unsigned long long)st.st_size;
        }
    }

    if (opts->json) {
        printf("{\"name\":");
        json_string(stdout, pkg_name);
        printf(",\"installed\":true,\"manifest\":");
        json_string(stdout, manifest);
        printf(",\"files\":%zu,\"missing\":%zu,\"unsafe\":%zu,"
               "\"bytes\":%llu",
               files.len, missing, unsafe, bytes);
        if (meta.source[0]) {
            printf(",\"source\":");
            json_string(stdout, meta.source);
        }
        if (meta.branch[0]) {
            printf(",\"branch\":");
            json_string(stdout, meta.branch);
        }
        if (meta.installed[0]) {
            printf(",\"installed_at\":");
            json_string(stdout, meta.installed);
        }
        if (opts->files) {
            printf(",\"paths\":[");
            for (i = 0; i < files.len; i++) {
                struct stat st;
                int exists = (lstat(files.items[i], &st) == 0);

                if (i > 0) printf(",");
                printf("{\"path\":");
                json_string(stdout, files.items[i]);
                printf(",\"exists\":%s}", exists ? "true" : "false");
            }
            printf("]");
        }
        printf("}\n");
    } else {
        char sizebuf[32];

        format_size(bytes, sizebuf, sizeof sizebuf);
        printf("\nPackage: %s\n", pkg_name);
        printf("Status: installed\n");
        printf("Manifest: %s\n", manifest);
        if (meta.source[0]) printf("Source: %s\n", meta.source);
        if (meta.branch[0]) printf("Branch: %s\n", meta.branch);
        if (meta.installed[0]) printf("Installed: %s\n", meta.installed);
        printf("Files: %zu\n", files.len);
        printf("Size: %s\n", sizebuf);
        printf("Missing: %zu\n", missing);
        printf("Unsafe entries: %zu\n", unsafe);
        if (opts->files) {
            printf("\nFiles:\n");
            for (i = 0; i < files.len; i++) {
                struct stat st;
                if (lstat(files.items[i], &st) != 0) {
                    printf("  %s (missing)\n", files.items[i]);
                } else {
                    printf("  %s\n", files.items[i]);
                }
            }
        }
        printf("\n");
    }

    strlist_free(&files);
    free(home);
    return 0;
}


/* ------------------------------------------------------------------------- */
/* Doctor command                                                            */
/* ------------------------------------------------------------------------- */

typedef struct {
    int failures;
    int warnings;
    int json;
    int first;
} doctor_state;

static void doctor_emit(doctor_state *state, const char *status,
                        const char *check, const char *detail) {
    if (state->json) {
        if (!state->first) printf(",");
        state->first = 0;
        printf("{\"check\":");
        json_string(stdout, check);
        printf(",\"status\":");
        json_string(stdout, status);
        if (detail && *detail) {
            printf(",\"detail\":");
            json_string(stdout, detail);
        }
        printf("}");
    } else {
        if (strcmp(status, "pass") == 0) {
            printf("  [ OK ] %s", check);
        } else if (strcmp(status, "warn") == 0) {
            printf("  [WARN] %s", check);
        } else {
            printf("  [FAIL] %s", check);
        }
        if (detail && *detail) printf(": %s", detail);
        printf("\n");
    }
    if (strcmp(status, "fail") == 0) state->failures++;
    if (strcmp(status, "warn") == 0) state->warnings++;
}

int cmd_doctor(const illus_options *opts) {
    doctor_state state;
    char *home = NULL;
    char local_dir[PATH_MAX];
    char bin_dir[PATH_MAX];
    char manifest_dir[PATH_MAX];
    char buf[PATH_MAX];
    struct stat st;

    memset(&state, 0, sizeof state);
    state.json = opts->json;
    state.first = 1;
    if (opts->json) {
        printf("[");
    } else {
        printf("\nDreams Package Manager (illus) doctor\n\n");
    }

    home = get_real_home(NULL, NULL);
    if (!home) {
        doctor_emit(&state, "fail", "home directory", "could not resolve");
        goto done;
    }
    doctor_emit(&state, "pass", "home directory", home);

    if (geteuid() == 0) {
        if (env_truthy("ILLUS_ALLOW_ROOT")) {
            doctor_emit(&state, "warn", "running as root",
                        "ILLUS_ALLOW_ROOT is set");
        } else {
            doctor_emit(&state, "fail", "running as root",
                        "package operations will be refused");
        }
    } else {
        doctor_emit(&state, "pass", "not running as root", "");
    }

    if (path_join(local_dir, sizeof local_dir, home, ".local") != 0 ||
        path_join(bin_dir, sizeof bin_dir, home, ".local/bin") != 0 ||
        path_join(manifest_dir, sizeof manifest_dir, home,
                  ".local/share/illus") != 0) {
        doctor_emit(&state, "fail", "paths", "path is too long");
        goto done;
    }
    if (opts->verbose) {
        char detail[PATH_MAX * 3 + 64];

        snprintf(detail, sizeof detail, "local=%s bin=%s manifests=%s",
                 local_dir, bin_dir, manifest_dir);
        doctor_emit(&state, "pass", "paths", detail);
    }

    if (stat(local_dir, &st) != 0) {
        doctor_emit(&state, "warn", "~/.local",
                    "missing (created on first install)");
    } else if (!S_ISDIR(st.st_mode)) {
        doctor_emit(&state, "fail", "~/.local", "not a directory");
    } else {
        if (st.st_uid != getuid()) {
            doctor_emit(&state, "fail", "~/.local ownership",
                        "not owned by you");
        } else {
            doctor_emit(&state, "pass", "~/.local ownership", "");
        }
        if (st.st_mode & S_IWOTH) {
            doctor_emit(&state, "fail", "~/.local permissions",
                        "world-writable");
        } else {
            doctor_emit(&state, "pass", "~/.local permissions", "");
        }
    }

    if (dir_on_path(bin_dir)) {
        doctor_emit(&state, "pass", "~/.local/bin on PATH", "");
    } else {
        doctor_emit(&state, "warn", "~/.local/bin on PATH", "add it to PATH");
    }

    {
        const char *tools[] = {"git", "make", "cmake", "autoreconf", "tar", NULL};
        size_t i;

        for (i = 0; tools[i] != NULL; i++) {
            char found[PATH_MAX];
            char check[64];

            snprintf(check, sizeof check, "%s available", tools[i]);
            if (find_in_path(tools[i], found, sizeof found) == 0) {
                doctor_emit(&state, "pass", check, found);
            } else if (strcmp(tools[i], "cmake") == 0) {
                doctor_emit(&state, "warn", check, "needed for CMake projects");
            } else if (strcmp(tools[i], "autoreconf") == 0) {
                doctor_emit(&state, "warn", check, "needed for Autotools bootstrap");
            } else if (strcmp(tools[i], "tar") == 0) {
                doctor_emit(&state, "warn", check, "needed for tarball sources");
            } else {
                doctor_emit(&state, "warn", check, "not found");
            }
        }
    }

    {
        DIR *d = opendir(manifest_dir);

        if (!d) {
            if (errno == ENOENT) {
                doctor_emit(&state, "warn", "manifest directory",
                            "missing (no packages installed)");
            } else {
                doctor_emit(&state, "fail", "manifest directory",
                            strerror(errno));
            }
        } else {
            package_info *pkgs = NULL;
            size_t count = 0;

            closedir(d);
            if (collect_packages(manifest_dir, local_dir, &pkgs, &count) != 0) {
                doctor_emit(&state, "fail", "manifest directory",
                            "could not read manifests");
            } else {
                size_t i;
                size_t missing = 0;
                size_t unsafe = 0;

                snprintf(buf, sizeof buf, "%zu package(s)", count);
                doctor_emit(&state, "pass", "manifests", buf);
                for (i = 0; i < count; i++) {
                    missing += pkgs[i].missing;
                    unsafe += pkgs[i].unsafe;
                }
                if (missing > 0) {
                    snprintf(buf, sizeof buf, "%zu missing file(s)", missing);
                    doctor_emit(&state, "warn", "tracked files", buf);
                } else {
                    doctor_emit(&state, "pass", "tracked files", "");
                }
                if (unsafe > 0) {
                    snprintf(buf, sizeof buf, "%zu unsafe entr%s", unsafe,
                             unsafe == 1 ? "y" : "ies");
                    doctor_emit(&state, "fail", "manifest safety", buf);
                } else {
                    doctor_emit(&state, "pass", "manifest safety", "");
                }
                free(pkgs);
            }
        }
    }

    {
        DIR *d = opendir(local_dir);
        int stale = 0;

        if (d) {
            struct dirent *entry;
            while ((entry = readdir(d)) != NULL) {
                if (strncmp(entry->d_name, ".illus-stage-", 13) == 0) stale++;
            }
            closedir(d);
        }
        if (stale > 0) {
            snprintf(buf, sizeof buf, "%d stale staging director%s", stale,
                     stale == 1 ? "y" : "ies");
            doctor_emit(&state, "warn", "staging directories", buf);
        } else {
            doctor_emit(&state, "pass", "staging directories", "");
        }
    }

    {
        package_info *pkgs = NULL;
        size_t count = 0;
        strlist untracked;

        memset(&untracked, 0, sizeof untracked);
        collect_packages(manifest_dir, local_dir, &pkgs, &count);
        collect_untracked(pkgs, count, bin_dir, &untracked);
        free(pkgs);
        if (untracked.len > 0) {
            snprintf(buf, sizeof buf, "%zu untracked binar%s", untracked.len,
                     untracked.len == 1 ? "y" : "ies");
            doctor_emit(&state, "warn", "untracked binaries", buf);
        } else {
            doctor_emit(&state, "pass", "untracked binaries", "");
        }
        strlist_free(&untracked);
    }

done:
    if (opts->json) {
        printf("]\n");
    } else {
        printf("\n%d failure(s), %d warning(s)\n\n", state.failures,
               state.warnings);
    }
    free(home);
    return state.failures > 0 ? 1 : 0;
}
