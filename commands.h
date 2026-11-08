#ifndef COMMANDS_H
#define COMMANDS_H

#define ILLUS_VERSION "1.1.0"

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
    int refresh;
    int allow_untracked;
    int depth;
    int jobs;
    const char *branch;
    const char *name;
    const char *dir;
    const char *registry;
    const char *build_system;
    const char *source;
} illus_options;

int cmd_pull(const illus_options *opts, const char *url);
int cmd_cast(const illus_options *opts);
int cmd_install(const illus_options *opts, const char *spec);
int cmd_update(const illus_options *opts);
int cmd_version(void);
int cmd_shed(const illus_options *opts, const char *package_name);
int cmd_list(const illus_options *opts);
int cmd_info(const illus_options *opts, const char *name);
int cmd_search(const illus_options *opts, const char *term);
int cmd_doctor(const illus_options *opts);

#endif
