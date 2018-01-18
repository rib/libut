/*
 * The interface from the pov of the server process, collecting data from
 * the clients.
 *
 * There are currently two sets of data exported by clients:
 * 1) a circular buffer containing small, high-resolution samples
 * 2) ancillary data buffers, containing larger descriptions of state
 *    which may be referenced by samples. The amount of ancillary
 *    data is expected to be bounded for a long running application
 *    such that we don't have to support reclaiming the associated
 *    buffers to avoid running out of memory.
 */

#pragma once

#include "ut.h"


#define UT_ABI_VERSION 0xf00baaa1


/*
 * A header page infront of each circular buffer of sample data
 */
struct ut_info_page {
    uint32_t abi_version;

    uint32_t pid;
    uint32_t tid;

    uint32_t sample_size;
    uint32_t n_samples_written;
};

enum ut_sample_type {
    UT_SAMPLE_TASK_PUSH = 1,
    UT_SAMPLE_TASK_POP,
    UT_SAMPLE_TASK_BACKTRACE
};

#define MAX_BACKTRACE_SIZE 10

/* Note: all samples in the circular buffer have the same size which allows us
 * to safely overwrite old data without damaging the integrity of old
 * samples.
 *
 * XXX: consider tracking a negative offset for being able to iterate through
 * samples in reverse order. This way the consumer would have to first iterate
 * backwards from the tail to find the head, but we could have variable sized
 * records without needing to worry about synchronizing head pointer updates.
 *
 *  Note: with the above, we'd have to define a maximum sample size so that
 * the comsumer would also skip over this much head data which the client
 * may have been in the middle of overwritting.
 */
struct ut_sample {
    uint16_t type;

    union {
        struct {
            /* A various tasks are described via side-band anciallary data buffers
             * and this is just the index into that ancillary array of task
             * descriptions */
            uint16_t task_desc_index;

            /* Tracking the stack size in samples accounts for having an incomplete
             * record of push/pop samples once the buffer starts being overwritten
             */
            uint16_t stack_pointer;
            uint8_t cpu;
            uint8_t padding;

            //uint64_t tsc;
            uint64_t timestamp;
        };
        void *addresses[MAX_BACKTRACE_SIZE];
    };
} __attribute__((aligned(8)));


enum ut_ancillary_record_type {
    UT_ANCILLARY_TASK_DESC = 1,
};

struct ut_ancillary_record {
    uint32_t type;
    uint16_t padding;
    uint16_t size;
} __attribute__((aligned(8)));

struct ut_shared_task_desc {
    uint16_t idx;
    char name[62];
}__attribute__((aligned(8)));




