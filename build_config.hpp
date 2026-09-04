#pragma once

// #define SKIP_OPCODE // remove _____ to enable

#define RUN_PROFILE_FAST 1
#define RUN_PROFILE_STRICT 2
#define RUN_PROFILE_FULL 3

#ifndef RUN_PROFILE
#define RUN_PROFILE RUN_PROFILE_FAST
#endif

#if RUN_PROFILE == RUN_PROFILE_FAST
    #define SELF_CHECK 1
    #define TIMING_SELF_TEST 0
    #define VICII_CHECKLIST_TEST 0
    #define FULL_REGRESSION_TEST 0
    #define CPU_EXTERNAL_ROM_TEST 1
    #define DRIVE1541_SMOKE_TEST 1
#elif RUN_PROFILE == RUN_PROFILE_STRICT
    #define SELF_CHECK 1
    #define TIMING_SELF_TEST 1
    #define VICII_CHECKLIST_TEST 1
    #define FULL_REGRESSION_TEST 0
    #define CPU_EXTERNAL_ROM_TEST 1
    #define DRIVE1541_SMOKE_TEST 1
#elif RUN_PROFILE == RUN_PROFILE_FULL
    #define SELF_CHECK 1
    #define TIMING_SELF_TEST 0
    #define VICII_CHECKLIST_TEST 0
    #define FULL_REGRESSION_TEST 1
    #define CPU_EXTERNAL_ROM_TEST 1
    #define DRIVE1541_SMOKE_TEST 1
#else
    #error Unsupported RUN_PROFILE value
#endif

#ifndef CPU_TRACE_VERBOSE
#define CPU_TRACE_VERBOSE 0
#endif
