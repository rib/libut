/*
 * GPU Top
 *
 * Copyright (C) 2014 Intel Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#pragma once

#include <GL/glx.h>
#include <GL/glxext.h>

#include "gputop-list.h"

struct winsys_context
{
    int ref;

    GLXContext glx_ctx;
    /* TODO: Add EGL support */

    struct winsys_surface *read_wsurface;
    struct winsys_surface *draw_wsurface;

    bool gl_initialised;

    gputop_list_t queries;
    struct intel_query_info *current_query;

    pthread_rwlock_t query_obj_cache_lock;
    gputop_list_t query_obj_cache;

    bool try_create_new_context_failed;
    bool is_debug_context;
    bool khr_debug_enabled;

    GLint scissor_x;
    GLint scissor_y;
    GLsizei scissor_width;
    GLsizei scissor_height;
    bool scissor_enabled;

};

struct winsys_surface
{
    struct winsys_context *wctx;

    /* Ignore pixmaps for now */
    GLXWindow glx_window;
    /* TODO: Add EGL support */

    /* not pending until glEndPerfQueryINTEL is called... */
    struct gl_perf_query *open_query_obj;

    gputop_list_t pending_queries;

    /* Finished queries, waiting to be picked up by the server thread */
    pthread_rwlock_t finished_queries_lock;
    gputop_list_t finished_queries;
};

extern struct array gputop_gl_contexts;
extern struct array gputop_gl_surfaces;

