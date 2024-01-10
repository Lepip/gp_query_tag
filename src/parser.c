#include "postgres.h"

#include "parser.h"
#include "utils/builtins.h"
#include "c.h"
#include <locale.h>

static bool good_char(char c) {
    return (isalnum(c) || c == '=' || c == ';' || c == '_');
}

bool parse_tags(const char *guc, List **parsed) {
    *parsed = NIL;
    bool is_in_key = true;
    bool is_empty = true;
    const char *substr_ptr_start;
    const char *substr_ptr_end;
    char *key_tag = NULL, *value_tag = NULL;
    if (!guc || *guc == '\0') {
        return true;
    }
    substr_ptr_start = guc;
    const char *cur;
    for (cur = guc;; ++cur) {
        if (*cur == '\0') {
            if (is_in_key) {
                return false;
            }
            if (is_empty) {
                return true;
            }
            substr_ptr_end = cur;
            value_tag = copy_substr(substr_ptr_start, substr_ptr_end);
            substr_ptr_start = cur + 1;

            tag_pair *tpair;
            tpair = malloc(sizeof(tpair));
            tpair->key = key_tag;
            tpair->value = value_tag;
            *parsed = lappend(*parsed, tpair);
            break;
        }
        if (!good_char(*cur)) {
            return false;
        }
        if (*cur == '=') {
            if (is_empty) {
                return false;
            }
            if (is_in_key) {
                is_in_key = false;
                is_empty = true;
            } else {
                return false;
            }
        }
        if (*cur == ';') {
            if (is_empty) {
                return false;
            }
            if (is_in_key) {
                return false;
            } else {
                is_in_key = true;
                is_empty = true;
            }
        }
        if (isalpha(*cur) || *cur == '_') {
            is_empty = false;
        }
        if (*cur == '=') {
            substr_ptr_end = cur;
            key_tag = copy_substr(substr_ptr_start, substr_ptr_end);
            substr_ptr_start = cur + 1;
        }
        if (*cur == ';') {
            substr_ptr_end = cur;
            value_tag = copy_substr(substr_ptr_start, substr_ptr_end);
            substr_ptr_start = cur + 1;

            tag_pair *tpair;
            tpair = malloc(sizeof(tpair));
            tpair->key = key_tag;
            tpair->value = value_tag;
            *parsed = lappend(*parsed, tpair);
        }
    }

    return true;
}

void tag_list_free(List **tag_list) {
    ListCell *cell;
    foreach (cell, *tag_list) {
        tag_pair *tpair = lfirst(cell);
        free(tpair->key);
        free(tpair->value);
        free(tpair);
    }
    list_free(*tag_list);
    *tag_list = NIL;
}

bool is_safe(const char *guc) {
    if (!guc)
        return false;
    if (*guc == '\0')
        return true;
    bool is_in_key = true;
    bool is_empty = true;
    for (const char *cur = guc; *cur != '\0'; ++cur) {
        if (!good_char(*cur)) {
            elog(INFO, "GUHed char: %c", *cur);
            return false;
        }
        if (*cur == '=') {
            if (is_empty) {
                return false;
            }
            if (is_in_key) {
                is_in_key = false;
                is_empty = true;
            } else {
                return false;
            }
        }
        if (*cur == ';') {
            if (is_empty) {
                return false;
            }
            if (is_in_key) {
                return false;
            } else {
                is_in_key = true;
                is_empty = true;
            }
        }
        if (isalnum(*cur) || *cur == '_') {
            is_empty = false;
        }
    }
    if (is_in_key && !is_empty)
        return false;
    return true;
}

char *copy_substr(const char *start, const char *end) {
    int len = end - start;
    char *ret = calloc(len + 1, sizeof(char));
    memcpy(ret, start, len);
    ret[len] = '\0';
    return ret;
}

bool is_tag_in_guc(const char *tag, const char *guc) {
    List *parsed_guc = NIL;
    List *parsed_tag = NIL;

    bool ok = parse_tags(guc, &parsed_guc);
    if (!ok) {
        tag_list_free(&parsed_guc);
        return false;
    }

    ok = parse_tags(tag, &parsed_tag);
    if (!ok) {
        tag_list_free(&parsed_tag);
        return false;
    }
    ListCell *guc_pair_cell, *tag_pair_cell;
    foreach (tag_pair_cell, parsed_tag) {
        tag_pair *cur_tag = lfirst(tag_pair_cell);
        bool found = false;
        foreach (guc_pair_cell, parsed_guc) {
            tag_pair *cur_guc = lfirst(guc_pair_cell);
            if (strcmp(cur_tag->key, cur_guc->key) == 0 &&
                strcmp(cur_tag->value, cur_guc->value) == 0) {
                found = true;
                break;
            }
        }
        if (!found) {
            tag_list_free(&parsed_guc);
            tag_list_free(&parsed_tag);
            return false;
        }
    }
    tag_list_free(&parsed_guc);
    tag_list_free(&parsed_tag);
    return true;
}
