#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "commands.h"

static int fails = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { fprintf(stderr, "FAIL: %s\\n", msg); fails++; } \
    else { printf("  ok: %s\\n", msg); } \
} while (0)

int main(void) {
    printf("== unit: path safety ==\\n");

    /* has_dotdot */
    CHECK(has_dotdot("/a/b/c") == 0, "has_dotdot clean path");
    CHECK(has_dotdot("/a/../b") == 1, "has_dotdot detects ..");
    CHECK(has_dotdot("/a/./b") == 0, "has_dotdot allows .");
    CHECK(has_dotdot("../x") == 1, "has_dotdot leading ..");

    /* is_valid_name */
    CHECK(is_valid_name("foo") == 1, "valid name foo");
    CHECK(is_valid_name("") == 0, "empty name invalid");
    CHECK(is_valid_name(".hidden") == 0, "dot-prefixed invalid");
    CHECK(is_valid_name("a/b") == 0, "slash invalid");
    CHECK(is_valid_name("a..b") == 1, "dots in middle ok");

    /* url_is_tarball */
    CHECK(url_is_tarball("https://x/y.tar.gz") == 1, "tar.gz url");
    CHECK(url_is_tarball("https://x/y.zip?foo=bar") == 1, "zip with query");
    CHECK(url_is_tarball("https://x/y.git") == 0, "git url not tarball");

    /* make_filename */
    {
        char buf[64];
        CHECK(make_filename(buf, sizeof buf, "pkg", ".txt") == 0, "make_filename ok");
        CHECK(strcmp(buf, "pkg.txt") == 0, "make_filename value");
    }

    /* path_join */
    {
        char buf[64];
        CHECK(path_join(buf, sizeof buf, "/a", "b") == 0, "path_join ok");
        CHECK(strcmp(buf, "/a/b") == 0, "path_join value");
        CHECK(path_join(buf, sizeof buf, "/a/", "b") == 0, "path_join trailing slash");
        CHECK(strcmp(buf, "/a/b") == 0, "path_join trailing slash value");
    }

    /* parent_dir */
    {
        char buf[64];
        CHECK(parent_dir("/a/b/c", buf, sizeof buf) == 0, "parent_dir ok");
        CHECK(strcmp(buf, "/a/b") == 0, "parent_dir value");
    }

    /* is_protected_target */
    CHECK(is_protected_target("/home", "/home/user") == 1, "protected /home");
    CHECK(is_protected_target("/", "/home/user") == 1, "protected /");
    {
        char homebuf[256];
        snprintf(homebuf, sizeof homebuf, "%s", "/home/user");
        CHECK(is_protected_target(homebuf, homebuf) == 1, "protected home itself");
    }

    /* expand_path */
    {
        char *p = expand_path("~/foo", "/home/user");
        CHECK(p != NULL && strcmp(p, "/home/user/foo") == 0, "expand_path ~");
        free(p);
        p = expand_path("/abs/path", "/home/user");
        CHECK(p != NULL && strcmp(p, "/abs/path") == 0, "expand_path abs");
        free(p);
    }

    /* has_dotdot */
    CHECK(has_dotdot("/a/b") == 0, "has_dotdot clean");
    CHECK(has_dotdot("/a/../b") == 1, "has_dotdot ..");

    /* is_system_area */
    CHECK(is_system_area("/usr") == 1, "system area /usr");
    CHECK(is_system_area("/tmp") == 0, "tmp not system area");

    if (fails) {
        fprintf(stderr, "%d unit test(s) failed\\n", fails);
        return 1;
    }
    printf("all unit checks passed\\n");
    return 0;
}