#include <postgres.h>

#include <miscadmin.h>
#include <utils/builtins.h>
#include <utils/guc.h>
#include "fmgr.h"
#include <utils/resgroup.h>
#include <commands/resgroupcmds.h>
#include <executor/spi.h>
#include "access/xact.h"
#include "access/transam.h"
#include "parser.h"
#include "utils/syscache.h"
#include "catalog/pg_authid.h"

PG_MODULE_MAGIC;

PG_FUNCTION_INFO_V1(current_resgroup);
PG_FUNCTION_INFO_V1(is_tag_in_guc);

static Oid resgroup_assign_by_query_tag(void);
static Oid current_resgroup_id(void);
static bool check_new_query_tag(char **, void **, GucSource);

void _PG_init(void);
void _PG_fini(void);

static char *query_tag = NULL;
const static char *NO_GROUP_MSG = "unknown";
static const int MAX_QUERY_SIZE = 200;
static const int MAX_QUERY_TAG_LENGTH = 100;

static resgroup_assign_hook_type prev_hook = NULL;

Datum is_tag_in_guc(PG_FUNCTION_ARGS) {
    text *query_tag = PG_GETARG_TEXT_P(0);
    char *query_tag_str = text_to_cstring(query_tag);

    const char *guc_query_tag_value =
        GetConfigOption("QUERY_TAG", false, false);
    bool result = is_tag_in_guc_ctype(query_tag_str, guc_query_tag_value);

    pfree(query_tag_str);

    PG_RETURN_BOOL(result);
}

static Oid resgroup_assign_by_query_tag(void) {

    Oid groupId = InvalidOid;
    if (prev_hook)
        groupId = prev_hook();

    if (groupId == InvalidOid)
        groupId = GetResGroupIdForRole(GetUserId());

    if (SPI_connect() == SPI_ERROR_CONNECT) {
        elog(ERROR, "SPI connection failed in query_tag");
        return groupId;
    }

    char query[MAX_QUERY_SIZE];
    strcpy(query, "SELECT EXISTS (SELECT 1 FROM pg_catalog.pg_tables WHERE "
                  "tablename = 'wlm_rules')");
    
    elog(DEBUG3, "QUERY_TAG: Entered resgroup hook.");

    int SPI_status;
    bool extension_created = false;
    SPI_status = SPI_execute(query, false, 0);
    if (SPI_status != SPI_OK_SELECT) {
        elog(ERROR, "QUERY_TAG: Error executing query: %s", query);
    }

    /* Check if the table exists */
    if (SPI_processed > 0) {
        char *existsStr =
            SPI_getvalue(SPI_tuptable->vals[0], SPI_tuptable->tupdesc, 1);
        if (existsStr && existsStr[0] == 't') {
            extension_created = true;
        }
    }
    if (!extension_created) {
        elog(DEBUG3, "QUERY_TAG: Table wlm_rules doesn't exist, abort.");
        SPI_finish();
        return groupId;
    }
    elog(DEBUG3, "QUERY_TAG: Table wlm_rules exists, continue.");

    char *rgname = GetResGroupNameForId(groupId);
    char *rolename = GetUserNameFromId(GetUserId());
    elog(DEBUG3, "QUERY_TAG: Trying to fetch rules from wlm_rules, where resgname = %s and rolename = %s",
         rgname, rolename);
    int full_length = snprintf(
        query, sizeof(query),
        "select rule_id, dest_resg from wlm_rules where resgname = '%s' and role "
        "= '%s' and active = TRUE and is_tag_in_guc(query_tag) order by order_id limit 1;",
        rgname, rolename);
    if (full_length >= MAX_QUERY_SIZE) {
        elog(ERROR, "QUERY_TAG: failed, query tag too long");
        SPI_finish();
        return groupId;
    }
    SPI_status = SPI_execute(query, false, 0);
    if (SPI_status < 0) {
        elog(ERROR, "QUERY_TAG: Failed to execute SQL query: %s", query);
        SPI_finish();
        return groupId;
    }
    if (SPI_processed > 0) {
        char *crgname =
            SPI_getvalue(SPI_tuptable->vals[0], SPI_tuptable->tupdesc, 2);
        if (!crgname) {
            SPI_finish();
            return groupId;
        }
        elog(DEBUG3, "QUERY_TAG: set resgroup to: %s, %d", crgname,
             GetResGroupIdForName(crgname));
        SPI_finish();
        return GetResGroupIdForName(crgname);
    }
    elog(DEBUG3, "QUERY_TAG: didn't find matching rules for the current query. Keeping original groupId.");
    SPI_finish();
    return groupId;
}

static bool check_new_query_tag(char **newvalue, void **extra, GucSource source) {
    elog(DEBUG3, "QUERY_TAG: Checking new tag");
    if (strlen(*newvalue) >= MAX_QUERY_TAG_LENGTH) {
        elog(DEBUG3, "QUERY_TAG: Tag too long, didn't set.");
        return false;
    }
    if (!is_safe(*newvalue)) {
        return false;
    }
    elog(DEBUG3, "QUERY_TAG: All okay, set new tag.");
    return true;
}

static Oid current_resgroup_id() {
    Oid group_id = ResGroupGetGroupIdBySessionId(MySessionState->sessionId);
    return group_id;
}

Datum current_resgroup(PG_FUNCTION_ARGS) {
    Oid group_id = current_resgroup_id();
    if (!OidIsValid(group_id))
        PG_RETURN_TEXT_P(cstring_to_text(NO_GROUP_MSG));
    char *resgroup_name = GetResGroupNameForId(group_id);
    if (!resgroup_name)
        PG_RETURN_TEXT_P(cstring_to_text(NO_GROUP_MSG));
    PG_RETURN_TEXT_P(cstring_to_text(resgroup_name));
}

void _PG_init(void) {
    DefineCustomStringVariable(
        "QUERY_TAG", 
        "Tag for applying rules from wlm_rules",
        NULL,                     /* long description */
        &query_tag, 
        "",                       /* initial value */
        PGC_USERSET, 0,           /* flags */
        check_new_query_tag,     /* check hook */
        NULL,                     /* assign hook */
        NULL);                    /* show hook */
    prev_hook = resgroup_assign_hook;
    resgroup_assign_hook = resgroup_assign_by_query_tag;
}

void _PG_fini(void) {
    resgroup_assign_hook = prev_hook;
}
