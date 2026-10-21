/* version.h - the language version this toolchain implements, the table of
 * installed versions, and the check of line 1 of every .rt file. */
#ifndef ARETE_VERSION_H
#define ARETE_VERSION_H

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

#include "diag.h"

#define ARETE_LANG_VERSION "0.1"

/* Phase 3 decision (SPEC.md, open item for phase 3): one binary with a
 * built-in table of versions. A launcher over ~/.arete/versions/ can replace
 * this table later without changing the command line. */
bool version_is_installed(const char *version);
void version_print_installed(FILE *out);

/* True for strings of the shape <digits>.<digits>, used to tell
 * `arete 2.0` (a version) from `arete frobnicate` (a typo). */
bool version_looks_like(const char *s);

/* True when `text` (one line, without or with its line end) has the exact
 * shape of a version header. Used to catch a header on the wrong line. */
bool version_line_looks_like_header(const char *text, size_t len);

/* Checks that the line starting at byte `start` is exactly
 * `arete <major>.<minor>` for the version this toolchain implements.
 * On success stores the offset of the first byte after that line in
 * `next_line`. On failure fills `err` with one of:
 *   missing version header, malformed version header,
 *   file requires Arete X.Y, this is Arete 0.1 */
bool version_check_header(const Source *src, size_t start,
                          size_t *next_line, Diag *err);

#endif
