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

/* Reads up to 6 decimal digits at *p into *value. Returns false if there is
 * no digit or too many. */
static bool read_number(const char *text, size_t len, size_t *p, int *value) {
    size_t digits = 0;
    int v = 0;
    while (*p < len && isdigit((unsigned char)text[*p])) {
        if (++digits > 6) return false;
        v = v * 10 + (text[*p] - '0');
        (*p)++;
    }
    if (digits == 0) return false;
    *value = v;
    return true;
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

    /* Exact shape: "arete" SP digits "." digits { SP } */
    bool well_formed = strncmp(text, "arete", 5) == 0;
    size_t p = 5;
    int major = 0, minor = 0;
    if (well_formed) well_formed = p < len && text[p] == ' ';
    if (well_formed) { p++; well_formed = read_number(text, len, &p, &major); }
    if (well_formed) well_formed = p < len && text[p] == '.';
    if (well_formed) { p++; well_formed = read_number(text, len, &p, &minor); }
    while (well_formed && p < len) {
        if (text[p] != ' ') well_formed = false;
        p++;
    }
    if (!well_formed) {
        diag_set(err, 1, 1, "malformed version header",
                 "not of the form `arete <major>.<minor>`",
                 "the first line must be exactly `arete " ARETE_LANG_VERSION "`");
        return false;
    }

    char found[32];
    snprintf(found, sizeof found, "%d.%d", major, minor);
    if (strcmp(found, ARETE_LANG_VERSION) != 0) {
        char summary[120], label[120], help[120];
        snprintf(summary, sizeof summary,
                 "file requires Arete %s, this is Arete " ARETE_LANG_VERSION, found);
        snprintf(label, sizeof label, "this file is written for Arete %s", found);
        snprintf(help, sizeof help, "run: arete install %s", found);
        diag_set(err, 1, 1, summary, label, help);
        return false;
    }
    return true;
}
