#include "postgres.h"

#include "parser.h"
#include "utils/builtins.h"
#include "c.h"
#include <locale.h>
#include "utils/memutils.h"

static void free_double_list(List **double_list) {
    ListCell *current_cell;
    foreach(current_cell, *double_list) {
        list_free(lfirst(current_cell));
    }
    list_free(*double_list);
    *double_list = NIL;
}

bool split_tags(const char *tags, ParsedTags **parced) {
    MemoryContext oldctx = MemoryContextSwitchTo(TopMemoryContext);
    *parced = palloc(sizeof(**parced));
    if (!tags) {
        elog(ERROR, "QUERY_TAG: there's a NULL query_tag somewhere.");
    }
    char *tags_mutable = pstrdup(tags);
    List *almost_parced_tags = NIL;
    bool ok = SplitIdentifierString(tags_mutable, ';', &almost_parced_tags);
    if (!ok) {
        pfree(*parced);
        *parced = NULL;
        pfree(tags_mutable);
        list_free(almost_parced_tags);
        MemoryContextSwitchTo(oldctx);
        return false;
    }
    ListCell *current_tag;
    List *parced_tags = NIL;
    foreach(current_tag, almost_parced_tags) {
        List *parsed_current_tag = NIL;
        ok = SplitIdentifierString(lfirst(current_tag), '=', &parsed_current_tag);
        if (!ok || list_length(parsed_current_tag) != 2) {
            list_free(parsed_current_tag);
            free_double_list(&parced_tags);
            pfree(*parced);
            *parced = NULL;
            pfree(tags_mutable);
            list_free(almost_parced_tags);
            MemoryContextSwitchTo(oldctx);
            return false;
        }
        parced_tags = lappend(parced_tags, parsed_current_tag);
    }

    (*parced)->tags_mutable = tags_mutable;
    (*parced)->parsed_tags = parced_tags;
    MemoryContextSwitchTo(oldctx);
    return true;
}

bool is_parsed_rule_in_parsed_guc(ParsedTags *rule_tags, ParsedTags *guc_tags) {
    elog(DEBUG4, "Printing guc:");
    print_parsed_tags(guc_tags);
    elog(DEBUG4, "Printing rule:");
    print_parsed_tags(rule_tags);
    ListCell *guc_cell, *tag_cell;
    foreach (tag_cell, rule_tags->parsed_tags) {
        List *tag_pair = lfirst(tag_cell);
        bool found = false;
        foreach (guc_cell, guc_tags->parsed_tags) {
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

void free_parsed_tags(ParsedTags **parced) {
    if (*parced) {
        free_double_list(&(*parced)->parsed_tags);
        if ((*parced)->tags_mutable)
            pfree((*parced)->tags_mutable);
        elog(DEBUG4, "FREED: %d", (int)*parced);
        pfree(*parced);
        *parced = NULL;
    }
}

void print_parsed_tags(ParsedTags *parsed) {
    elog(DEBUG4, "Printing tags:");
    ListCell *current_cell;
    ListCell *current_tag;
    int id = 0;
    if (parsed) {
        foreach(current_cell, parsed->parsed_tags) {
            foreach(current_tag, lfirst(current_cell)) {
                elog(DEBUG4, "%d: %s", id, lfirst(current_tag));
                id++;
            }
        }
    } else {
        elog(DEBUG4, "Null, lol");
    }
}