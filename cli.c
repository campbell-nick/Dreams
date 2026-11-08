/*
 * Command-line parsing and dispatch.
 *
 * The parser is deliberately small and table-driven so that every command
 * rejects unknown options, reports missing values consistently, and accepts
 * both the modern subcommand form (`illus install foo`) and the original
 * legacy flags (`illus --install foo`).
 */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cli.h"

#if defined(__GNUC__)
#define ILLUS_PRINTF(a, b) __attribute__((format(printf, a, b)))
#else
#define ILLUS_PRINTF(a, b)
#endif

typedef enum {
    OPT_DRY_RUN = 0,
    OPT_JSON,
    OPT_VERBOSE,
    OPT_FORCE,
    OPT_ALL,
    OPT_VERIFY,
    OPT_FILES,
    OPT_REINSTALL,
    OPT_STORE,
    OPT_FRESH,
    OPT_RECURSIVE,
    OPT_REFRESH,
    OPT_ALLOW_UNTRACKED,
    OPT_BRANCH,
    OPT_NAME,
    OPT_DIR,
    OPT_REGISTRY,
    OPT_BUILD_SYSTEM,
    OPT_JOBS,
    OPT_DEPTH,
    OPT_HELP,
    OPT_COUNT
} opt_id;

#define OPT_BIT(x) (1ULL << (x))
#define OPT_COMMON (OPT_BIT(OPT_VERBOSE) | OPT_BIT(OPT_HELP))

typedef struct {
    const char *name;
    opt_id id;
    int has_value;
} opt_def;

static const opt_def option_defs[] = {
    {"dry-run",         OPT_DRY_RUN,         0},
    {"json",            OPT_JSON,            0},
    {"verbose",         OPT_VERBOSE,         0},
    {"force",           OPT_FORCE,           0},
    {"all",             OPT_ALL,             0},
    {"verify",          OPT_VERIFY,          0},
    {"files",           OPT_FILES,           0},
    {"reinstall",       OPT_REINSTALL,       0},
    {"store",           OPT_STORE,           0},
    {"fresh",           OPT_FRESH,           0},
    {"recursive",       OPT_RECURSIVE,       0},
    {"refresh",         OPT_REFRESH,         0},
    {"allow-untracked", OPT_ALLOW_UNTRACKED, 0},
    {"branch",          OPT_BRANCH,          1},
    {"name",            OPT_NAME,            1},
    {"dir",             OPT_DIR,             1},
    {"registry",        OPT_REGISTRY,        1},
    {"build-system",    OPT_BUILD_SYSTEM,    1},
    {"jobs",            OPT_JOBS,            1},
    {"depth",           OPT_DEPTH,           1},
    {"help",            OPT_HELP,            0},
    {NULL,              OPT_COUNT,           0}
};

static unsigned long long allowed_mask(cli_command command) {
    switch (command) {
    case CLI_CMD_INSTALL:
        return OPT_COMMON | OPT_BIT(OPT_DRY_RUN) | OPT_BIT(OPT_FORCE) |
               OPT_BIT(OPT_REINSTALL) | OPT_BIT(OPT_BRANCH) | OPT_BIT(OPT_NAME) |
               OPT_BIT(OPT_DIR) | OPT_BIT(OPT_REGISTRY) | OPT_BIT(OPT_REFRESH) |
               OPT_BIT(OPT_ALLOW_UNTRACKED) | OPT_BIT(OPT_JOBS) |
               OPT_BIT(OPT_BUILD_SYSTEM);
    case CLI_CMD_REMOVE:
        return OPT_COMMON | OPT_BIT(OPT_DRY_RUN);
    case CLI_CMD_LIST:
        return OPT_BIT(OPT_ALL) | OPT_BIT(OPT_JSON) | OPT_BIT(OPT_VERBOSE) |
               OPT_BIT(OPT_VERIFY) | OPT_BIT(OPT_HELP);
    case CLI_CMD_INFO:
        return OPT_COMMON | OPT_BIT(OPT_FILES) | OPT_BIT(OPT_JSON) |
               OPT_BIT(OPT_REGISTRY) | OPT_BIT(OPT_REFRESH);
    case CLI_CMD_SEARCH:
        return OPT_BIT(OPT_JSON) | OPT_BIT(OPT_VERBOSE) | OPT_BIT(OPT_REGISTRY) |
               OPT_BIT(OPT_REFRESH) | OPT_BIT(OPT_HELP);
    case CLI_CMD_UPDATE:
        return OPT_COMMON | OPT_BIT(OPT_DRY_RUN) | OPT_BIT(OPT_ALL);
    case CLI_CMD_PULL:
        return OPT_COMMON | OPT_BIT(OPT_DRY_RUN) | OPT_BIT(OPT_FORCE) |
               OPT_BIT(OPT_BRANCH) | OPT_BIT(OPT_NAME) | OPT_BIT(OPT_DIR) |
               OPT_BIT(OPT_DEPTH) | OPT_BIT(OPT_RECURSIVE);
    case CLI_CMD_CAST:
        return OPT_COMMON | OPT_BIT(OPT_STORE) | OPT_BIT(OPT_FRESH) |
               OPT_BIT(OPT_BUILD_SYSTEM) | OPT_BIT(OPT_JOBS) |
               OPT_BIT(OPT_ALLOW_UNTRACKED) | OPT_BIT(OPT_NAME);
    case CLI_CMD_DOCTOR:
        return OPT_BIT(OPT_JSON) | OPT_BIT(OPT_VERBOSE) | OPT_BIT(OPT_HELP);
    case CLI_CMD_VERSION:
        return OPT_BIT(OPT_VERBOSE) | OPT_BIT(OPT_HELP);
    default:
        return 0;
    }
}

const char *cli_command_name(cli_command command) {
    switch (command) {
    case CLI_CMD_HELP:    return "help";
    case CLI_CMD_VERSION: return "version";
    case CLI_CMD_INSTALL: return "install";
    case CLI_CMD_REMOVE:  return "remove";
    case CLI_CMD_LIST:    return "list";
    case CLI_CMD_INFO:    return "info";
    case CLI_CMD_SEARCH:  return "search";
    case CLI_CMD_UPDATE:  return "update";
    case CLI_CMD_PULL:    return "pull";
    case CLI_CMD_CAST:    return "cast";
    case CLI_CMD_DOCTOR:  return "doctor";
    default:              return "";
    }
}

static cli_command command_from_word(const char *word) {
    if (strcmp(word, "help") == 0) return CLI_CMD_HELP;
    if (strcmp(word, "version") == 0) return CLI_CMD_VERSION;
    if (strcmp(word, "install") == 0) return CLI_CMD_INSTALL;
    if (strcmp(word, "remove") == 0) return CLI_CMD_REMOVE;
    if (strcmp(word, "shed") == 0) return CLI_CMD_REMOVE;
    if (strcmp(word, "uninstall") == 0) return CLI_CMD_REMOVE;
    if (strcmp(word, "list") == 0) return CLI_CMD_LIST;
    if (strcmp(word, "ls") == 0) return CLI_CMD_LIST;
    if (strcmp(word, "info") == 0) return CLI_CMD_INFO;
    if (strcmp(word, "search") == 0) return CLI_CMD_SEARCH;
    if (strcmp(word, "update") == 0) return CLI_CMD_UPDATE;
    if (strcmp(word, "pull") == 0) return CLI_CMD_PULL;
    if (strcmp(word, "cast") == 0) return CLI_CMD_CAST;
    if (strcmp(word, "doctor") == 0) return CLI_CMD_DOCTOR;
    return CLI_CMD_NONE;
}

static cli_command command_from_legacy(const char *flag) {
    if (strcmp(flag, "--help") == 0) return CLI_CMD_HELP;
    if (strcmp(flag, "--version") == 0) return CLI_CMD_VERSION;
    if (strcmp(flag, "--install") == 0) return CLI_CMD_INSTALL;
    if (strcmp(flag, "--remove") == 0) return CLI_CMD_REMOVE;
    if (strcmp(flag, "--shed") == 0) return CLI_CMD_REMOVE;
    if (strcmp(flag, "--uninstall") == 0) return CLI_CMD_REMOVE;
    if (strcmp(flag, "--list") == 0) return CLI_CMD_LIST;
    if (strcmp(flag, "--info") == 0) return CLI_CMD_INFO;
    if (strcmp(flag, "--search") == 0) return CLI_CMD_SEARCH;
    if (strcmp(flag, "--update") == 0) return CLI_CMD_UPDATE;
    if (strcmp(flag, "--pull") == 0) return CLI_CMD_PULL;
    if (strcmp(flag, "--cast") == 0) return CLI_CMD_CAST;
    if (strcmp(flag, "--doctor") == 0) return CLI_CMD_DOCTOR;
    return CLI_CMD_NONE;
}

static void print_usage(const char *program, cli_command command) {
    switch (command) {
    case CLI_CMD_INSTALL:
        fprintf(stderr, "Usage: %s install <name|url> [--branch <ref>] [--name <pkg>]\n", program);
        fprintf(stderr, "       [--dir <path>] [--force] [--reinstall] [--dry-run]\n");
        break;
    case CLI_CMD_REMOVE:
        fprintf(stderr, "Usage: %s remove <package>... [--dry-run]\n", program);
        break;
    case CLI_CMD_LIST:
        fprintf(stderr, "Usage: %s list [--all] [--json] [--verbose] [--verify]\n", program);
        break;
    case CLI_CMD_INFO:
        fprintf(stderr, "Usage: %s info <package> [--files] [--json]\n", program);
        break;
    case CLI_CMD_SEARCH:
        fprintf(stderr, "Usage: %s search [term] [--json] [--refresh]\n", program);
        break;
    case CLI_CMD_UPDATE:
        fprintf(stderr, "Usage: %s update [--dry-run] [--all]\n", program);
        break;
    case CLI_CMD_PULL:
        fprintf(stderr, "Usage: %s pull <url> [--dir <path>] [--branch <ref>]\n", program);
        fprintf(stderr, "       [--name <pkg>] [--depth <n>] [--recursive] [--force] [--dry-run]\n");
        break;
    case CLI_CMD_CAST:
        fprintf(stderr, "Usage: %s cast [--store] [--fresh] [--build-system make|cmake]\n", program);
        fprintf(stderr, "       [--jobs <n>] [--name <pkg>] [--allow-untracked]\n");
        break;
    case CLI_CMD_DOCTOR:
        fprintf(stderr, "Usage: %s doctor [--json] [--verbose]\n", program);
        break;
    case CLI_CMD_VERSION:
        fprintf(stderr, "Usage: %s version\n", program);
        break;
    default:
        fprintf(stderr, "Usage: %s <command> [options]\n", program);
        fprintf(stderr, "Run '%s help' for a list of commands.\n", program);
        break;
    }
}

static void parse_error(const char *program, cli_command command,
                        const char *fmt, ...)
    ILLUS_PRINTF(3, 4);

static void parse_error(const char *program, cli_command command,
                        const char *fmt, ...) {
    va_list ap;

    fprintf(stderr, "[Dreams] Error: ");
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fprintf(stderr, "\n");
    print_usage(program, command);
}

static const opt_def *find_option(const char *name, size_t len) {
    size_t i;

    for (i = 0; option_defs[i].name != NULL; i++) {
        if (strlen(option_defs[i].name) == len &&
            strncmp(option_defs[i].name, name, len) == 0) {
            return &option_defs[i];
        }
    }
    return NULL;
}

static int parse_positive_int(const char *value, int *out) {
    char *end = NULL;
    long n;

    if (!value || !*value) return -1;
    n = strtol(value, &end, 10);
    if (!end || *end != '\0' || n <= 0 || n > 1000000) return -1;
    *out = (int)n;
    return 0;
}

static int apply_value(cli_parsed *parsed, opt_id id, const char *value) {
    switch (id) {
    case OPT_BRANCH:
        parsed->opts.branch = value;
        return 0;
    case OPT_NAME:
        parsed->opts.name = value;
        return 0;
    case OPT_DIR:
        parsed->opts.dir = value;
        return 0;
    case OPT_REGISTRY:
        parsed->opts.registry = value;
        return 0;
    case OPT_BUILD_SYSTEM:
        parsed->opts.build_system = value;
        return 0;
    case OPT_JOBS:
        return parse_positive_int(value, &parsed->opts.jobs);
    case OPT_DEPTH:
        return parse_positive_int(value, &parsed->opts.depth);
    default:
        return -1;
    }
}

static void apply_flag(cli_parsed *parsed, opt_id id) {
    switch (id) {
    case OPT_DRY_RUN:         parsed->opts.dry_run = 1; break;
    case OPT_JSON:            parsed->opts.json = 1; break;
    case OPT_VERBOSE:         parsed->opts.verbose = 1; break;
    case OPT_FORCE:           parsed->opts.force = 1; break;
    case OPT_ALL:             parsed->opts.all = 1; break;
    case OPT_VERIFY:          parsed->opts.verify = 1; break;
    case OPT_FILES:           parsed->opts.files = 1; break;
    case OPT_REINSTALL:       parsed->opts.reinstall = 1; break;
    case OPT_STORE:           parsed->opts.store = 1; break;
    case OPT_FRESH:           parsed->opts.fresh = 1; break;
    case OPT_RECURSIVE:       parsed->opts.recursive = 1; break;
    case OPT_REFRESH:         parsed->opts.refresh = 1; break;
    case OPT_ALLOW_UNTRACKED: parsed->opts.allow_untracked = 1; break;
    case OPT_HELP:            parsed->want_help = 1; break;
    default:                  break;
    }
}

static int parse_long_option(const char *program, cli_command command,
                             char *arg, char *argv[], int *index, int argc,
                             cli_parsed *parsed, unsigned long long allowed) {
    char *eq = strchr(arg, '=');
    size_t name_len = eq ? (size_t)(eq - arg) : strlen(arg);
    const opt_def *def;

    if (name_len <= 2) {
        parse_error(program, command, "unknown option '%s'", arg);
        return -1;
    }
    def = find_option(arg + 2, name_len - 2);
    if (!def) {
        parse_error(program, command, "unknown option '%.*s'", (int)name_len, arg);
        return -1;
    }
    if (!(allowed & OPT_BIT(def->id))) {
        parse_error(program, command, "option '--%s' is not valid for '%s'",
                    def->name, cli_command_name(command));
        return -1;
    }

    if (def->has_value) {
        const char *value;
        if (eq) {
            value = eq + 1;
        } else if (*index + 1 < argc) {
            value = argv[++(*index)];
        } else {
            parse_error(program, command, "option '--%s' requires a value", def->name);
            return -1;
        }
        if (!*value) {
            parse_error(program, command, "option '--%s' requires a non-empty value",
                        def->name);
            return -1;
        }
        if (apply_value(parsed, def->id, value) != 0) {
            parse_error(program, command, "invalid value for '--%s': %s",
                        def->name, value);
            return -1;
        }
    } else {
        if (eq) {
            parse_error(program, command, "option '--%s' does not take a value",
                        def->name);
            return -1;
        }
        apply_flag(parsed, def->id);
    }
    return 0;
}

static int parse_args(int argc, char *argv[], cli_parsed *parsed) {
    const char *program = argv[0];
    const char *first;
    unsigned long long allowed;
    int end_opts = 0;
    int i;

    if (argc < 2) {
        parsed->command = CLI_CMD_HELP;
        parsed->want_help = 1;
        return 0;
    }

    first = argv[1];
    if (strcmp(first, "--help") == 0 || strcmp(first, "-h") == 0) {
        parsed->command = CLI_CMD_HELP;
        i = 2;
    } else if (strcmp(first, "--version") == 0 || strcmp(first, "-v") == 0) {
        parsed->command = CLI_CMD_VERSION;
        i = 2;
    } else if (first[0] == '-' && first[1] == '-') {
        cli_command command = command_from_legacy(first);
        if (command == CLI_CMD_NONE) {
            parse_error(program, CLI_CMD_NONE, "unknown option '%s'", first);
            return -1;
        }
        parsed->command = command;
        i = 2;
    } else if (first[0] == '-') {
        parse_error(program, CLI_CMD_NONE, "unknown option '%s'", first);
        return -1;
    } else {
        cli_command command = command_from_word(first);
        if (command == CLI_CMD_NONE) {
            parse_error(program, CLI_CMD_NONE, "unknown command '%s'", first);
            return -1;
        }
        parsed->command = command;
        i = 2;
    }

    allowed = allowed_mask(parsed->command);
    for (; i < argc; i++) {
        char *arg = argv[i];

        if (!end_opts && strcmp(arg, "--") == 0) {
            end_opts = 1;
            continue;
        }
        if (!end_opts && arg[0] == '-' && arg[1] != '\0') {
            if (arg[1] == '-') {
                if (parse_long_option(program, parsed->command, arg, argv, &i,
                                      argc, parsed, allowed) != 0) {
                    return -1;
                }
            } else if (strcmp(arg, "-h") == 0) {
                parsed->want_help = 1;
            } else {
                parse_error(program, parsed->command, "unknown option '%s'", arg);
                return -1;
            }
        } else {
            if (parsed->nargs >= CLI_MAX_POSITIONALS) {
                parse_error(program, parsed->command, "too many arguments");
                return -1;
            }
            parsed->args[parsed->nargs++] = arg;
        }
    }
    return 0;
}

void cli_print_help(const char *program, cli_command command) {
    if (command == CLI_CMD_NONE || command == CLI_CMD_HELP) {
        printf("Dreams Package Manager (illus) v%s\n\n", ILLUS_VERSION);
        printf("Usage:\n");
        printf("  %s <command> [options]\n", program);
        printf("  %s --<command> [options]    (legacy form)\n\n", program);
        printf("Commands:\n");
        printf("  install <name|url>   Clone, build, and install a package\n");
        printf("  remove <name>...     Uninstall packages (alias: shed)\n");
        printf("  list                 List installed packages\n");
        printf("  info <name>          Show details for an installed or available package\n");
        printf("  search [term]        Search the package registry\n");
        printf("  update               Pull, rebuild, and reinstall everything in ~/Dreams\n");
        printf("  pull <url>           Clone a repository\n");
        printf("  cast                 Build the current project\n");
        printf("  doctor               Diagnose common problems\n");
        printf("  version              Show version information\n");
        printf("  help [command]       Show help\n\n");
        printf("Environment:\n");
        printf("  ILLUS_HOME          Use a different home directory (tests/isolated installs)\n");
        printf("  ILLUS_REGISTRY      Registry path or git URL (default: Dreams registry)\n");
        printf("  ILLUS_ALLOW_ROOT    Set to 1 to allow package operations as root\n");
        printf("  NO_COLOR            Disable ANSI colour output\n\n");
        printf("Run '%s help <command>' for command-specific options.\n", program);
        return;
    }

    printf("Dreams Package Manager (illus) v%s\n\n", ILLUS_VERSION);
    switch (command) {
    case CLI_CMD_INSTALL:
        printf("Usage: %s install <name|url> [options]\n\n", program);
        printf("Clone, build, and install a package into ~/.local.\n");
        printf("If <name> is found in the registry its URL is used automatically.\n\n");
        printf("Options:\n");
        printf("  --branch <ref>       Clone/update a specific branch or tag\n");
        printf("  --name <pkg>         Override the package name\n");
        printf("  --dir <path>         Clone destination (default ~/Dreams/<pkg>)\n");
        printf("  --force              Re-clone even if the destination exists\n");
        printf("  --reinstall          Clean rebuild (--fresh) after updating\n");
        printf("  --build-system <s>   Force 'make' or 'cmake'\n");
        printf("  --jobs <n>           Parallel build jobs\n");
        printf("  --allow-untracked    Accept installs that ignore DESTDIR\n");
        printf("  --registry <path>    Registry file, directory, or git URL\n");
        printf("  --refresh            Refresh the cached registry\n");
        printf("  --dry-run            Show what would happen without changing anything\n");
        break;
    case CLI_CMD_REMOVE:
        printf("Usage: %s remove <package>... [--dry-run]\n\n", program);
        printf("Remove every file tracked by the package manifest.\n");
        printf("Accepts package names or installed binary names.\n");
        break;
    case CLI_CMD_LIST:
        printf("Usage: %s list [options]\n\n", program);
        printf("List installed packages sorted by name.\n\n");
        printf("Options:\n");
        printf("  --all                Also list untracked binaries in ~/.local/bin\n");
        printf("  --verbose            Show manifest paths and problem counts\n");
        printf("  --verify             Exit non-zero if files are missing or unsafe\n");
        printf("  --json               Machine-readable output\n");
        break;
    case CLI_CMD_INFO:
        printf("Usage: %s info <package> [options]\n\n", program);
        printf("Show manifest, registry, and file details for a package.\n\n");
        printf("Options:\n");
        printf("  --files              List every tracked file\n");
        printf("  --json               Machine-readable output\n");
        printf("  --registry <path>    Registry file, directory, or git URL\n");
        printf("  --refresh            Refresh the cached registry\n");
        break;
    case CLI_CMD_SEARCH:
        printf("Usage: %s search [term] [options]\n\n", program);
        printf("Search the package registry. With no term, lists every entry.\n\n");
        printf("Options:\n");
        printf("  --json               Machine-readable output\n");
        printf("  --registry <path>    Registry file, directory, or git URL\n");
        printf("  --refresh            Refresh the cached registry\n");
        break;
    case CLI_CMD_UPDATE:
        printf("Usage: %s update [options]\n\n", program);
        printf("Pull, rebuild, and reinstall every project in ~/Dreams.\n\n");
        printf("Options:\n");
        printf("  --dry-run            Show what would be updated\n");
        printf("  --all                Accepted for compatibility (updates everything)\n");
        break;
    case CLI_CMD_PULL:
        printf("Usage: %s pull <url> [options]\n\n", program);
        printf("Clone a repository into ~/Dreams/<name> or an explicit destination.\n\n");
        printf("Options:\n");
        printf("  --dir <path>         Exact destination directory\n");
        printf("  --branch <ref>       Clone a specific branch or tag\n");
        printf("  --name <pkg>         Override the destination name\n");
        printf("  --depth <n>          Shallow clone depth\n");
        printf("  --recursive          Clone submodules\n");
        printf("  --force              Replace an existing destination\n");
        printf("  --dry-run            Show what would happen without cloning\n");
        break;
    case CLI_CMD_CAST:
        printf("Usage: %s cast [options]\n\n", program);
        printf("Build the project in the current directory.\n\n");
        printf("Options:\n");
        printf("  --store              Install into ~/.local and write a manifest\n");
        printf("  --fresh              Clean build artifacts first\n");
        printf("  --build-system <s>   Force 'make' or 'cmake'\n");
        printf("  --jobs <n>           Parallel build jobs\n");
        printf("  --name <pkg>         Override the package name\n");
        printf("  --allow-untracked    Accept installs that ignore DESTDIR\n");
        break;
    case CLI_CMD_DOCTOR:
        printf("Usage: %s doctor [options]\n\n", program);
        printf("Check paths, permissions, tools, manifests, and stale state.\n\n");
        printf("Options:\n");
        printf("  --json               Machine-readable output\n");
        printf("  --verbose            Show additional details\n");
        break;
    case CLI_CMD_VERSION:
        printf("Usage: %s version\n", program);
        break;
    default:
        break;
    }
}

int cli_main(int argc, char *argv[]) {
    cli_parsed parsed;
    const char *program;

    memset(&parsed, 0, sizeof parsed);
    if (parse_args(argc, argv, &parsed) != 0) {
        return 2;
    }

    program = argv[0];

    if (parsed.command == CLI_CMD_HELP) {
        if (parsed.nargs > 0) {
            cli_command target = command_from_word(parsed.args[0]);
            if (target == CLI_CMD_NONE) {
                parse_error(program, CLI_CMD_NONE, "unknown command '%s'", parsed.args[0]);
                return 2;
            }
            cli_print_help(program, target);
        } else {
            cli_print_help(program, CLI_CMD_NONE);
        }
        return 0;
    }

    if (parsed.want_help) {
        cli_print_help(program, parsed.command);
        return 0;
    }

    switch (parsed.command) {
    case CLI_CMD_VERSION:
        if (parsed.nargs != 0) {
            parse_error(program, parsed.command, "unexpected argument '%s'", parsed.args[0]);
            return 2;
        }
        return cmd_version();
    case CLI_CMD_LIST:
        if (parsed.nargs != 0) {
            parse_error(program, parsed.command, "unexpected argument '%s'", parsed.args[0]);
            return 2;
        }
        return cmd_list(&parsed.opts);
    case CLI_CMD_INFO:
        if (parsed.nargs != 1) {
            parse_error(program, parsed.command, "info requires exactly one package name");
            return 2;
        }
        return cmd_info(&parsed.opts, parsed.args[0]);
    case CLI_CMD_SEARCH:
        if (parsed.nargs > 1) {
            parse_error(program, parsed.command, "search accepts at most one term");
            return 2;
        }
        return cmd_search(&parsed.opts, parsed.nargs == 1 ? parsed.args[0] : NULL);
    case CLI_CMD_PULL:
        if (parsed.nargs != 1) {
            parse_error(program, parsed.command, "pull requires exactly one repository URL");
            return 2;
        }
        return cmd_pull(&parsed.opts, parsed.args[0]);
    case CLI_CMD_INSTALL:
        if (parsed.nargs != 1) {
            parse_error(program, parsed.command, "install requires exactly one package name or URL");
            return 2;
        }
        return cmd_install(&parsed.opts, parsed.args[0]);
    case CLI_CMD_CAST:
        if (parsed.nargs != 0) {
            parse_error(program, parsed.command, "cast does not take positional arguments");
            return 2;
        }
        return cmd_cast(&parsed.opts);
    case CLI_CMD_UPDATE:
        if (parsed.nargs != 0) {
            parse_error(program, parsed.command, "update does not take positional arguments");
            return 2;
        }
        return cmd_update(&parsed.opts);
    case CLI_CMD_DOCTOR:
        if (parsed.nargs != 0) {
            parse_error(program, parsed.command, "doctor does not take positional arguments");
            return 2;
        }
        return cmd_doctor(&parsed.opts);
    case CLI_CMD_REMOVE: {
        int failures = 0;
        int i;
        if (parsed.nargs == 0) {
            parse_error(program, parsed.command, "remove requires at least one package name");
            return 2;
        }
        for (i = 0; i < parsed.nargs; i++) {
            if (cmd_shed(&parsed.opts, parsed.args[i]) != 0) {
                failures++;
            }
        }
        return failures == 0 ? 0 : 1;
    }
    default:
        parse_error(program, CLI_CMD_NONE, "no command given");
        return 2;
    }
}
