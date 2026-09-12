#ifndef DOOMCUBE_GC_REGRESSION_H
#define DOOMCUBE_GC_REGRESSION_H

#include <stdbool.h>

typedef enum
{
    GC_REGRESSION_ACTION_NONE = 0,
    GC_REGRESSION_ACTION_EXIT,
    GC_REGRESSION_ACTION_SECRET_EXIT,
    GC_REGRESSION_ACTION_SAVE_PROBE,
    GC_REGRESSION_ACTION_LOAD_PROBE
} gc_regression_action_t;

typedef struct
{
    const char *name;
    const char *iwadPath;
    const char *pwadPath;

    int episode;
    int map;

    gc_regression_action_t action;
} gc_regression_case_t;

/*
 * Initializes the regression controller from Dolphin's custom RTC.
 *
 * Regression builds use fixed RTC slots to select a test case at runtime,
 * allowing every case to execute from the exact same DOL and ISO.
 */
bool GC_RegressionInit(void);

const gc_regression_case_t *GC_RegressionGetCase(void);

int GC_RegressionGetCaseIndex(void);
int GC_RegressionGetCaseCount(void);

#endif
