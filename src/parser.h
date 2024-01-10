#include "nodes/pg_list.h"

typedef struct tpair{
    char *key;
    char *value;
} tag_pair;

bool is_safe(const char *guc);
bool parse_tags(const char *guc, List **parsed);
void tag_list_free(List **tag_list);
char *copy_substr(const char *start, const char *end);
bool is_tag_in_guc_ctype(const char *tag, const char *guc);
