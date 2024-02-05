CREATE EXTENSION query_tag;


SET QUERY_TAG TO "aboba=aboba";

-- weird query_tags

SET QUERY_TAG TO "a";
SHOW QUERY_TAG;

SET QUERY_TAG TO "=aboba";
SHOW QUERY_TAG;

SET QUERY_TAG TO "a=a;";
SHOW QUERY_TAG;

SET QUERY_TAG TO "a;";
SHOW QUERY_TAG;

SET QUERY_TAG TO "123=123";
SHOW QUERY_TAG;

SET QUERY_TAG TO ";";
SHOW QUERY_TAG;

-- complex behaviour

-- should set to rgroup1

CREATE RESOURCE GROUP rgroup1 WITH (CPU_RATE_LIMIT=20, MEMORY_LIMIT=25, MEMORY_SPILL_RATIO=20);
CREATE RESOURCE GROUP rgroup2 WITH (CPU_RATE_LIMIT=20, MEMORY_LIMIT=25, MEMORY_SPILL_RATIO=20);

INSERT INTO wlm_rules (resgname, role, dest_resg, order_id, query_tag, active, kill_rule)
    VALUES ('admin_group', 'clepip', 'rgroup1', 3, 'group=rgroup1', TRUE, FALSE);
INSERT INTO wlm_rules (resgname, role, dest_resg, order_id, query_tag, active, kill_rule)
    VALUES ('admin_group', 'clepip', 'rgroup2', 4, 'group=rgroup1', TRUE, FALSE);

SET QUERY_TAG TO "group=rgroup1";
SELECT current_resgroup();

-- should still set to rgroup1 (we are in admin_group, the "current_rsgroup" shows group after
--- applying tags, and tags are applied accordingly to users rsgroup)

INSERT INTO wlm_rules (resgname, role, dest_resg, order_id, query_tag, active, kill_rule)
    VALUES ('rgroup1', 'clepip', 'rgroup2', 1, 'group=still_1', TRUE, FALSE);
INSERT INTO wlm_rules (resgname, role, dest_resg, order_id, query_tag, active, kill_rule)
    VALUES ('admin_group', 'clepip', 'rgroup1', 5, 'group=still_1', TRUE, FALSE);

SET QUERY_TAG TO "group=still_1";
SELECT CURRENT_RESGROUP();

-- should set to rgroup1

INSERT INTO wlm_rules (resgname, role, dest_resg, order_id, query_tag, active, kill_rule)
    VALUES ('admin_group', 'clepip', 'rgroup2', 1, 'group=cancelled', FALSE, FALSE);
INSERT INTO wlm_rules (resgname, role, dest_resg, order_id, query_tag, active, kill_rule)
    VALUES ('admin_group', 'clepip', 'rgroup1', 2, 'group=cancelled', TRUE, FALSE);

SET QUERY_TAG TO "group=cancelled";
SELECT CURRENT_RESGROUP();

INSERT INTO wlm_rules (resgname, role, dest_resg, order_id, query_tag, active, kill_rule, planner_cost, orca_cost)
    VALUES ('admin_group', 'clepip', 'rgroup1', 2, 'group=cancelled', FALSE, TRUE, 1000, 1000);