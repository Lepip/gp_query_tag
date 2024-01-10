
-- complain if script is sourced in psql, rather than via CREATE EXTENSION
\echo Use '''CREATE EXTENSION "query_tag"''' to load this file. \quit

CREATE FUNCTION show_current_rsgroup() RETURNS text
     AS 'query_tag', 'show_current_rsgroup'
     LANGUAGE C STRICT VOLATILE;

CREATE OR REPLACE FUNCTION isTagInGuc(query_tag text)
    RETURNS boolean
    AS 'query_tag', 'isTagInGuc'
    LANGUAGE C STRICT VOLATILE;

CREATE TABLE wlm_rules (
    serial_number   integer,
    rule_id         integer,
    rsgname         name,
    role            name,
    query_tag       text,
    dest_rsg        name,
    cpu_time        integer,
    running_time    integer,
    disk_io_md      integer,
    planner_cost    float8,
    orca_cost       float8,
    slice_num       integer,
    action          integer,
    active          boolean,
    ctime           timestamp without time zone,
    etime           timestamp without time zone,
    idle_session    json,
    spill_file_mb   int,
    cpuskew_percent int,
    cpuskew_duration_sec int,
    order_id        int not null
)

CREATE TABLE gpcc_wlm_log_history (
    ctime               timestamp without time zone,
    tstart              timestamp without time zone,
    tfinish             timestamp without time zone,
    rule_serial_number  integer,
    rule_id             integer,
    tmid                integer,
    ssid                integer,
    ccnt                integer,
    action              integer,
    rsgname             name,
    role                name,
    status              text,
    fail_msg            text
)