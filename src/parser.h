#include "nodes/pg_list.h"

bool split_tags(char *tags, List **splitted);
bool is_tag_list_in_guc_list(List *rule_tags, List *guc_tags);