/*
 * The running build: generated at link time (tools/gen_version.py, Makefile "build version"), so
 * these change with every link of the firmware, the bring-up image, the simulators and the web
 * pages. The version is "0.2.1" for a release build and "0.2.1+N-gHASH[-modified]" for any other
 * (tools/release_version.py; README.md "Versions and releases").
 */
#ifndef VERSION_H
#define VERSION_H

extern const char fw_version[48];    /* "0.2.1", "0.2.1+3-gabc1234-modified" */
extern const char fw_build_date[12]; /* "Oct 29 2026", the format of __DATE__ */
extern const char fw_build_time[12]; /* "14:05:31" */

#endif
