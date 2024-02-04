#include "nodes/pg_list.h"

typedef struct ParsedTags {
    char *tags_mutable;
    List *parsed_tags;
} ParsedTags;

bool split_tags(const char *tags, ParsedTags **parsed);
bool is_parsed_rule_in_parsed_guc(ParsedTags *parsed_rule, ParsedTags *parsed_guc);
void free_parsed_tags(ParsedTags **parsed);
