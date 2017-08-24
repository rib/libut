#pragma once

#include <stdint.h>

struct ut_task_desc {
    const char *name;
    const char *desc;

    /* private */
    uint16_t idx;
};

void
ut_push_task(struct ut_task_desc *task_desc);

void
ut_pop_task(struct ut_task_desc *task_desc);
