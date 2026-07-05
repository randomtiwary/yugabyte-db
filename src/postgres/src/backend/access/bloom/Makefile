#-------------------------------------------------------------------------
#
# Makefile--
#    Makefile for access/bloom
#
# IDENTIFICATION
#    src/backend/access/bloom/Makefile
#
#-------------------------------------------------------------------------

subdir = src/backend/access/bloom
top_builddir = ../../../..
include $(top_builddir)/src/Makefile.global

OBJS = \
	bloomam.o \
	bloom_cost.o \
	bloom_insert.o \
	bloom_page.o \
	bloom_scan.o \
	bloom_util.o \
	bloom_vacuum.o \
	bloom_validate.o

include $(top_srcdir)/src/backend/common.mk
