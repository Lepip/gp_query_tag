#include "postgres.h"

#include "parser.h"
#include "utils/builtins.h"
#include "c.h"
#include <locale.h>


bool split_tags(char *tags, List **splitted) {
    List *tags_list = NIL;
    bool ok = SplitIdentifierString(tags, ';', &tags_list);
    if (!ok) {
        list_free(tags_list);
        return false;
    }
    ListCell *current_tag;
    list_free(*splitted);
    *splitted = NIL;
    foreach(current_tag, tags_list) {
        List *parsed_tags;
        ok = SplitIdentifierString(lfirst(current_tag), '=', &parsed_tags);
        if (!ok || list_length(parsed_tags) != 2) {
            list_free(tags_list);
            list_free(parsed_tags);
            return false;
        }
        *splitted = lappend(*splitted, parsed_tags);
    }
    return true;
}

bool is_tag_list_in_guc_list(List *rule_tags, List *guc_tags) {
    ListCell *guc_cell, *tag_cell;
    foreach (tag_cell, rule_tags) {
        List *tag_pair = lfirst(tag_cell);
        bool found = false;
        foreach (guc_cell, guc_tags) {
            List *guc_pair = lfirst(guc_cell);
            if (strcmp(linitial(guc_pair), linitial(tag_pair)) == 0 &&
                strcmp(lsecond(guc_pair), lsecond(tag_pair)) == 0) {
                found = true;
                break;
            }
        }
        if (!found) {
            return false;
        }
    }
    return true;
}
