CFLAGS := ${CFLAGS} -Wall

EXTENSION = query_tag
EXTVERSION = $(shell grep default_version $(EXTENSION).control | \
			   sed -e "s/default_version[[:space:]]*=[[:space:]]*'\([^']*\)'/\1/")

DATA = $(wildcard sql/*--*.sql)
REGRESS = query_tag
REGRESS_OPTS = --inputdir=test/
OBJS = src/query_tag.o src/parser.o

MODULE_big = query_tag

PG_CONFIG = pg_config
PGXS := $(shell $(PG_CONFIG) --pgxs)
include $(PGXS)