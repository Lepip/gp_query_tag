#include <postgres.h>

#include <utils/palloc.h>
#include <utils/elog.h>
#include <miscadmin.h>
#include <utils/builtins.h>
#include <utils/guc.h>
#include <utils/resgroup.h>
#include "fmgr.h"
#include "utils/resgroup-ops.h"
#include <commands/resgroupcmds.h>
#include <executor/spi.h>
#include "access/xact.h"
#include "access/transam.h"
#include "parser.h"
#include "utils/syscache.h"
#include "catalog/pg_authid.h"
#include "cdb/cdbvars.h"
#include "optimizer/planner.h"
#include <assert.h>

PG_MODULE_MAGIC;

PG_FUNCTION_INFO_V1(current_resgroup);
PG_FUNCTION_INFO_V1(is_tag_in_guc);

PlannedStmt *
kill_rules_manager(Query *parse, int cursorOptions, ParamListInfo boundParams);
static Oid resgroup_assign_by_query_tag(void);
static Oid current_resgroup_id(void);
static bool check_new_query_tag(char **, void **, GucSource);

void _PG_init(void);
void _PG_fini(void);

static ParsedTags *parsed_guc_tags;
static char *query_tag = NULL;
const static char *NO_GROUP_MSG = "unknown";
static const int MAX_QUERY_SIZE = 200;
static const int MAX_QUERY_TAG_LENGTH = 100;

static resgroup_assign_hook_type prev_hook = NULL;
static planner_hook_type prev_planner_hook = NULL;

struct ResGroupSlotData
{
	Oid				groupId;
	void	        *group;		/* pointer to the group */

	int32			memQuota;	/* memory quota of current slot */
	int32			memUsage;	/* total memory usage of procs belongs to this slot */
	int				nProcs;		/* number of procs in this slot */

	void	        *next;

	ResGroupCaps	caps;
};

typedef struct ResGroupSlotData ResGroupSlotData;

PlannedStmt *
kill_rules_manager(Query *parse, int cursorOptions, ParamListInfo boundParams)
{
    
    PlannedStmt *result = NULL;
    result = prev_planner_hook(parse, cursorOptions, boundParams);
    if (MySessionState == NULL) {
        return result;
    }
    void * slot = (MySessionState->resGroupSlot);
    ResGroupSlotData * slot_data = (ResGroupSlotData *)(slot);
    if (slot_data == NULL) {
        return result;
    }

    int n_moves = result->nMotionNodes;
    int SPI_status = -1;
    StringInfoData query;
    StringInfoData cost_query;
    
    char *rgname = GetResGroupNameForId(slot_data->groupId);
    char *rolename = GetUserNameFromId(GetUserId());
    initStringInfo(&query);
    initStringInfo(&cost_query);
    double cost = result->planTree->total_cost;
    elog(DEBUG4, "KILL RULE: Cost: %f", cost);
    if (result->planGen == PLANGEN_PLANNER) {
        appendStringInfo(&cost_query, "planner_cost <= %f", cost);
    }
    if (result->planGen == PLANGEN_OPTIMIZER) {
        appendStringInfo(&cost_query, "orca_cost <= %f", cost);
    }
    assert(cost_query.data != NULL);
    appendStringInfo(&query, "select rule_id, dest_resg from wlm_rules where resgname = '%s' and role "
        "= '%s' and active = TRUE and %s and is_tag_in_guc(query_tag) and kill_rule = TRUE order by order_id limit 1;", 
        rgname, rolename, cost_query.data);
    PG_TRY();
    {
        SPI_status = SPI_execute(query.data, false, 0);
    }
    PG_CATCH();
    {
        SPI_status = -1;
    }
    PG_END_TRY();
    elog(NOTICE, "The executed query in kill rule: %s", query.data);
    pfree(query.data);
    pfree(cost_query.data);
    if (SPI_status < 0) {
        SPI_finish();
        elog(DEBUG3, "QUERY_TAG: query failed in the kill rule processor.");
        return result;
    }
    if (SPI_processed > 0) {
        elog(ERROR, "QUERY_TAG: there's a kill_rule. Stopped");
    }
    return result;
}

Datum is_tag_in_guc(PG_FUNCTION_ARGS) {
    text *rule_query_tag = PG_GETARG_TEXT_P(0);
    char *rule_query_tag_cstr = text_to_cstring(rule_query_tag);
    if (!rule_query_tag_cstr) {
        PG_RETURN_BOOL(false);
    }
    ParsedTags *parsed_rule_tags = NULL;
    bool ok = split_tags(rule_query_tag_cstr, &parsed_rule_tags);
    if (!ok) {
        pfree(rule_query_tag_cstr);
        PG_RETURN_BOOL(false);
    }
    bool result = is_parsed_rule_in_parsed_guc(parsed_rule_tags, parsed_guc_tags);
    free_parsed_tags(&parsed_rule_tags);
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
        "= '%s' and active = TRUE and is_tag_in_guc(query_tag) and kill_rule = FALSE order by order_id limit 1;",
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
        elog(DEBUG1, "QUERY_TAG: set resgroup to: %s, %d", crgname,
             GetResGroupIdForName(crgname));
        SPI_finish();
        return GetResGroupIdForName(crgname);
    }
    elog(DEBUG3, "QUERY_TAG: didn't find matching rules for the current query. Keeping original groupId.");
    SPI_finish();
    return groupId;
}

static bool check_new_query_tag(char **newvalue, void **extra, GucSource source) {
    if (!*newvalue) {
        free_parsed_tags(&parsed_guc_tags);
        parsed_guc_tags = NULL;
        return true;
    }
    if (query_tag && (strcmp(*newvalue, query_tag) == 0)) {
        return true;
    }
    elog(DEBUG3, "QUERY_TAG: Checking new tag");
    if (strlen(*newvalue) >= MAX_QUERY_TAG_LENGTH) {
        elog(DEBUG3, "QUERY_TAG: Tag too long, didn't set.");
        return false;
    }
    ParsedTags *new_parsed_guc_tag = NULL;
    bool ok = split_tags(*newvalue, &new_parsed_guc_tag);
    if (!ok) {
        elog(DEBUG3, "QUERY_TAG: Tag didn't pass parsing.");
        return false;
    }
    // if they are the same, it means, that memory contexts messed up
    assert(parsed_guc_tags != new_parsed_guc_tag);
    if (parsed_guc_tags) {
        free_parsed_tags(&parsed_guc_tags);
        parsed_guc_tags = NULL;
    }
    parsed_guc_tags = new_parsed_guc_tag;
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
        check_new_query_tag,      /* check hook */
        NULL,                     /* assign hook */
        NULL);                    /* show hook */
    prev_hook = resgroup_assign_hook;
    if (planner_hook) {
        prev_planner_hook = planner_hook;
    } else {
        prev_planner_hook = standard_planner;
    }
    resgroup_assign_hook = resgroup_assign_by_query_tag;
    planner_hook = kill_rules_manager;
}

void _PG_fini(void) {
    resgroup_assign_hook = prev_hook;
}
