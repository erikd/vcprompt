/*
 * Copyright (C) 2009-2013, Gregory P. Ward and contributors.
 *
 * This is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>

#include "git.h"
#include "capture.h"
#include "common.h"


static int
git_probe(vccontext_t *context)
{
	// If this is a directory, then this is a full git clone. If its a file
	// then this is a git submodule.
    return isfileordir(".git");
}

static result_t*
git_common_read_revision(char * headpath, vccontext_t *context)
{
    char buf[1024];

    if (!read_first_line(headpath, buf, 1024)) {
        debug("unable to read %s: assuming not a git repo", headpath);
        return NULL;
    }

    result_t *result = init_result();
    char *prefix = "ref: refs/heads/";
    int prefixlen = strlen(prefix);

    if (context->options->show_branch || context->options->show_revision) {
        int found_branch = 0;
        if (strncmp(prefix, buf, prefixlen) == 0) {
            /* yep, we're on a known branch */
            debug("read a head ref from .git/HEAD: '%s'", buf);
            if (result_set_branch(result, buf + prefixlen))
                found_branch = 1;
        }
        else {
            /* if it's not a branch name, assume it is a commit ID */
            debug(".git/HEAD doesn't look like a head ref: unknown branch");
            result_set_branch(result, "(unknown)");
            result_set_revision(result, buf, 12);
        }
        if (context->options->show_revision && found_branch) {
            char buf[1024];
            char filename[1024] = ".git/refs/heads/";
            int nchars = sizeof(filename) - strlen(filename) - 1;
            strncat(filename, result->branch, nchars);
            if (read_first_line(filename, buf, 1024)) {
                result_set_revision(result, buf, 12);
            }
        }
    }
    if (context->options->show_modified) {
        char *argv[] = {
            "git", "diff", "--no-ext-diff", "--quiet", "--exit-code", NULL};
        capture_t *capture = capture_child("git", argv);
        result->modified = (capture->status == 1);

        /* any other outcome (including failure to fork/exec,
           failure to run git, or diff error): assume no
           modifications */
        free_capture(capture);
    }
    if (context->options->show_unknown) {
        char *argv[] = {
            "git", "ls-files", "--others", "--exclude-standard", NULL};
        capture_t *capture = capture_child("git", argv);
        result->unknown = (capture != NULL && capture->childout.len > 0);

        /* again, ignore other errors and assume no unknown files */
        free_capture(capture);
    }

	return result;
}

static result_t*
git_submodule_get_info(vccontext_t *context)
{
    char buf[1024], *headpath;

	// In a git submodule, `.git` is a file that points to the config directory
	// for this submodule, which allows us find the `HEAD` file.
    if (!read_first_line(".git", buf, 1024)) {
        debug("unable to read file '.git' assuming not a git repo");
        return NULL;
    }

	strncat(buf, "/HEAD", sizeof(buf) - strlen(buf) - 1);
	buf [sizeof(buf) - 1] = 0;

    if ((headpath = strchr(buf, '.')) == NULL) {
        debug("can't parse gitdir '%s'", buf);
        return NULL;
    }

	return git_common_read_revision(headpath, context);
}

static result_t*
git_repo_get_info(vccontext_t *context)
{
	// In a real git repo, so we know where the `HEAD` file is.
    return git_common_read_revision(".git/HEAD", context);
}

static result_t*
git_get_info(vccontext_t *context)
{
    if (isfile(".git"))
        return git_submodule_get_info(context);

    return git_repo_get_info(context);
}

vccontext_t*
get_git_context(options_t *options)
{
    return init_context("git", options, git_probe, git_get_info);
}
