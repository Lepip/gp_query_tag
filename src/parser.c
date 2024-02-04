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

bool split_tags(const char *tags, ParsedTags **parsed) {
    if (!tags) {
        elog(ERROR, "QUERY_TAG: there's a NULL query_tag somewhere.");
    }
    MemoryContext oldctx = MemoryContextSwitchTo(TopMemoryContext);
    *parsed = palloc(sizeof(**parsed));
    char *tags_mutable = pstrdup(tags);
    List *tag_pairs = NIL;
    bool ok = SplitIdentifierString(tags_mutable, ';', &tag_pairs);
    if (!ok) {
        pfree(*parsed);
        *parsed = NULL;
        pfree(tags_mutable);
        list_free(tag_pairs);
        MemoryContextSwitchTo(oldctx);
        return false;
    }
    ListCell *current_tag;
    List *parsed_tags = NIL;
    foreach(current_tag, tag_pairs) {
        List *parsed_current_tag = NIL;
        ok = SplitIdentifierString(lfirst(current_tag), '=', &parsed_current_tag);
        if (!ok || list_length(parsed_current_tag) != 2) {
            list_free(parsed_current_tag);
            free_double_list(&parsed_tags);
            pfree(*parsed);
            *parsed = NULL;
            pfree(tags_mutable);
            list_free(tag_pairs);
            MemoryContextSwitchTo(oldctx);
            return false;
        }
        parsed_tags = lappend(parsed_tags, parsed_current_tag);
    }

    (*parsed)->tags_mutable = tags_mutable;
    (*parsed)->parsed_tags = parsed_tags;
    MemoryContextSwitchTo(oldctx);
    return true;
}

bool is_parsed_rule_in_parsed_guc(ParsedTags *rule_tags, ParsedTags *guc_tags) {
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

void free_parsed_tags(ParsedTags **parsed) {
    if (*parsed) {
        free_double_list(&(*parsed)->parsed_tags);
        if ((*parsed)->tags_mutable)
            pfree((*parsed)->tags_mutable);
        pfree(*parsed);
        *parsed = NULL;
    }
}