#include "version.h"

#include <ctype.h>
#include <string.h>

static const char *const INSTALLED[] = { ARETE_LANG_VERSION };
static const size_t INSTALLED_COUNT = sizeof INSTALLED / sizeof INSTALLED[0];

bool version_is_installed(const char *version) {
    for (size_t i = 0; i < INSTALLED_COUNT; i++) {
        if (strcmp(INSTALLED[i], version) == 0) return true;
    }
    return false;
}

void version_print_installed(FILE *out) {
    for (size_t i = 0; i < INSTALLED_COUNT; i++) {
        fprintf(out, "%s%s", i == 0 ? "" : ", ", INSTALLED[i]);
    }
}

bool version_looks_like(const char *s) {
    const char *p = s;
    if (!isdigit((unsigned char)*p)) return false;
    while (isdigit((unsigned char)*p)) p++;
    if (*p != '.') return false;
    p++;
    if (!isdigit((unsigned char)*p)) return false;
    while (isdigit((unsigned char)*p)) p++;
    return *p == '\0';
}

/* Consumes a run of 1 to 6 digits at *p. A leading zero is allowed only for
 * the number 0 itself: `0.01` is not `0.1`, the spelling is the version. */
static bool read_digits(const char *text, size_t len, size_t *p) {
    size_t start = *p;
    while (*p < len && isdigit((unsigned char)text[*p])) (*p)++;
    size_t digits = *p - start;
    if (digits == 0 || digits > 6) return false;
    if (digits > 1 && text[start] == '0') return false;
    return true;
}

/* The exact shape of a header line without its line end:
 *   "arete" SP digits "." digits { SP }
 * On success ver_start and ver_len cover the "<major>.<minor>" text. */
static bool header_shape(const char *text, size_t len, size_t *ver_start, size_t *ver_len) {
    if (len < 6 || strncmp(text, "arete ", 6) != 0) return false;
    size_t p = 6;
    size_t start = p;
    if (!read_digits(text, len, &p)) return false;
    if (p >= len || text[p] != '.') return false;
    p++;
    if (!read_digits(text, len, &p)) return false;
    *ver_start = start;
    *ver_len = p - start;
    while (p < len) {
        if (text[p] != ' ') return false;
        p++;
    }
    return true;
}

bool version_line_looks_like_header(const char *text, size_t len) {
    if (len > 0 && text[len - 1] == '\r') len--;
    size_t start, n;
    return header_shape(text, len, &start, &n);
}

bool version_check_header(const Source *src, size_t start,
                          size_t *next_line, Diag *err) {
    const char *text = src->text + start;
    size_t remaining = src->length - start;

    /* Isolate line 1, without its line end. */
    size_t len = 0;
    while (len < remaining && text[len] != '\n') len++;
    *next_line = start + len + (len < remaining ? 1 : 0);
    if (len > 0 && text[len - 1] == '\r') len--;

    /* Does the line start with the word `arete` at all? If not, there is no
     * header. If it does but the rest is wrong, the header is malformed. */
    bool starts_with_arete = len >= 5
        && tolower((unsigned char)text[0]) == 'a'
        && tolower((unsigned char)text[1]) == 'r'
        && tolower((unsigned char)text[2]) == 'e'
        && tolower((unsigned char)text[3]) == 't'
        && tolower((unsigned char)text[4]) == 'e'
        && (len == 5 || text[5] == ' ' || text[5] == '\t');
    if (!starts_with_arete) {
        diag_set(err, 1, 1, "missing version header",
                 "expected `arete " ARETE_LANG_VERSION "` here",
                 "add `arete " ARETE_LANG_VERSION "` at the top of the file");
        return false;
    }

    size_t ver_start = 0, ver_len = 0;
    if (!header_shape(text, len, &ver_start, &ver_len)) {
        diag_set(err, 1, 1, "malformed version header",
                 "not of the form `arete <major>.<minor>`",
                 "the first line must be exactly `arete " ARETE_LANG_VERSION "`");
        return false;
    }

    /* Compare the version as written, not as a number. */
    if (ver_len != strlen(ARETE_LANG_VERSION)
        || strncmp(text + ver_start, ARETE_LANG_VERSION, ver_len) != 0) {
        char found[16], summary[120], label[120], help[120];
        snprintf(found, sizeof found, "%.*s", (int)ver_len, text + ver_start);
        snprintf(summary, sizeof summary,
                 "file requires Arete %s, this is Arete " ARETE_LANG_VERSION, found);
        snprintf(label, sizeof label, "this file is written for Arete %s", found);
        snprintf(help, sizeof help, "run: arete install %s", found);
        diag_set(err, 1, 1, summary, label, help);
        return false;
    }
    return true;
}
