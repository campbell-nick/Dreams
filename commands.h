#ifndef COMMANDS_H
#define COMMANDS_H

#include <sys/stat.h>
#include <sys/types.h>

#define ILLUS_VERSION "1.3.0(dev)"

/*
 * Parsed command-line options. The CLI layer fills this in once and every
 * command reads only the fields that are valid for it.
 */
typedef struct {
    int dry_run;
    int json;
    int verbose;
    int force;
    int all;
    int verify;
    int files;
    int reinstall;
    int store;
    int fresh;
    int recursive;
    int allow_untracked;
    int depth;
    int jobs;
    const char *branch;
    const char *name;
    const char *dir;
    const char *build_system;
    const char *source;
    const char *configure_args;
    const char *make_args;
    const char *sha256;
    const char *log_file;
    const char *token;
} illus_options;

int cmd_pull(const illus_options *opts, const char *url);
int cmd_cast(const illus_options *opts);
int cmd_install(const illus_options *opts, const char *spec);
int cmd_upgrade(const illus_options *opts, const char *package_name);
int cmd_update(const illus_options *opts);
int cmd_version(void);
int cmd_shed(const illus_options *opts, const char *package_name);
int cmd_list(const illus_options *opts);
int cmd_info(const illus_options *opts, const char *name);
int cmd_doctor(const illus_options *opts);

/* Exposed for unit testing of path-safety helpers. */
int is_valid_name(const char *name);
int path_join(char *out, size_t size, const char *a, const char *b);
int make_filename(char *out, size_t size, const char *stem, const char *suffix);
int parent_dir(const char *path, char *out, size_t size);
int is_within(const char *path, const char *root);
int is_within_real(const char *path, const char *root);
int is_inside_real(const char *path, const char *root);
int has_dotdot(const char *path);
char *expand_path(const char *input, const char *home);
int mkdir_p(const char *path, mode_t mode);
int is_protected_target(const char *target, const char *home);
int replace_allowed(const char *target, const char *home);
int is_system_area(const char *path);
int check_existing_ancestor(const char *path, const char *home);
int url_is_tarball(const char *url);

#endif
