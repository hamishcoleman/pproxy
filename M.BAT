@echo off

lcc 4pproxy.c
lcclnk 4pproxy.obj wsock32.lib
