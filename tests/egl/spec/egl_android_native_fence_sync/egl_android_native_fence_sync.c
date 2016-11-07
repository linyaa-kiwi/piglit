/*
 * Copyright 2016 Intel Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

/**
 * \file Tests for EGL_ANDROID_native_fence_sync.
 *
 * This file attempts to exhaustively test the EGL_ANDROID_native_fence_sync
 */

#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include <unistd.h>

#include "piglit-util-egl.h"
#include "piglit-util-gl.h"

/* Extension function pointers.
 *
 * Use prefix 'pegl' (piglit egl) instead of 'egl' to avoid collisions with
 * prototypes in eglext.h. */
EGLSyncKHR (*peglCreateSyncKHR)(EGLDisplay dpy, EGLenum type, const EGLint *attrib_list);
EGLBoolean (*peglDestroySyncKHR)(EGLDisplay dpy, EGLSyncKHR sync);
EGLint (*peglClientWaitSyncKHR)(EGLDisplay dpy, EGLSyncKHR sync, EGLint flags, EGLTimeKHR timeout);
EGLint (*peglWaitSyncKHR)(EGLDisplay dpy, EGLSyncKHR sync, EGLint flags);
EGLint (*peglDupNativeFenceFDANDROID)(EGLDisplay dpy, EGLSyncKHR sync);
EGLBoolean (*peglGetSyncAttribKHR)(EGLDisplay dpy, EGLSyncKHR sync, EGLint attribute, EGLint *value);

static const EGLint canary = 0x31415926;
static EGLDisplay g_dpy = 0;
static EGLContext g_ctx = 0;

static enum piglit_result
init_display(EGLenum platform, EGLDisplay *out_dpy)
{
	enum piglit_result result = PIGLIT_PASS;
	EGLDisplay dpy;
	EGLint egl_major, egl_minor;
	bool ok;

	dpy = piglit_egl_get_default_display(platform);
	if (!dpy) {
		result = PIGLIT_SKIP;
		goto error;
	}

	ok = eglInitialize(dpy, &egl_major, &egl_minor);
	if (!ok) {
		result = PIGLIT_SKIP;
		goto error;
	}

	if (!piglit_is_egl_extension_supported(dpy, "EGL_ANDROID_native_fence_sync")) {
		piglit_loge("display does not support EGL_ANDROID_native_fence_sync");
		result = PIGLIT_SKIP;
		goto error;

	}

	*out_dpy = dpy;
	return result;

error:
	if (dpy) {
		eglTerminate(dpy);
	}
	return result;
}

/**
 * Create OpenGL ES 2.0 context, make it current, and verify that it supports
 * GL_OES_EGL_sync.
 */
static enum piglit_result
init_context(EGLDisplay dpy, EGLContext *out_ctx)
{
	enum piglit_result result = PIGLIT_PASS;
	bool ok = false;
	EGLConfig config = 0;
	EGLint num_configs = 0;
	EGLContext ctx = 0;

	/* Create OpenGL ES 2.0 or backwards-compatible context. */
	static const EGLint config_attribs[] = {
		EGL_RED_SIZE,		EGL_DONT_CARE,
		EGL_GREEN_SIZE,		EGL_DONT_CARE,
		EGL_BLUE_SIZE,		EGL_DONT_CARE,
		EGL_ALPHA_SIZE,		EGL_DONT_CARE,
		EGL_DEPTH_SIZE,		EGL_DONT_CARE,
		EGL_STENCIL_SIZE,	EGL_DONT_CARE,
		EGL_RENDERABLE_TYPE,	EGL_OPENGL_ES2_BIT
				        | EGL_OPENGL_ES3_BIT_KHR,
		EGL_NONE,
	};

	static const EGLint context_attribs[] = {
		EGL_CONTEXT_CLIENT_VERSION, 2,
		EGL_NONE,
	};

	ok = eglChooseConfig(dpy, config_attribs, &config, 1,
			     &num_configs);
	if (!ok || !config || num_configs == 0) {
		EGLint egl_error = eglGetError();
		piglit_loge("failed to get EGLConfig: %s(0x%x)",
			    piglit_get_egl_error_name(egl_error), egl_error);
		result = PIGLIT_SKIP;
		goto error;
	}

	ok = piglit_egl_bind_api(EGL_OPENGL_ES_API);
	if (!ok) {
		piglit_loge("failed to bind EGL_OPENGL_ES_API");
		result = PIGLIT_FAIL;
		goto error;

	}

	ctx = eglCreateContext(dpy, config, EGL_NO_CONTEXT, context_attribs);
	if (!ctx) {
		EGLint egl_error = eglGetError();
		piglit_loge("failed to create EGLContext: %s(0x%x)",
			    piglit_get_egl_error_name(egl_error), egl_error);
		result = PIGLIT_FAIL;
		goto error;
	}

	ok = eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, ctx);
	if (!ok) {
		/* Skip, don't fail. Assume the context doesn't support
		 * GL_OES_surfaceless_context or equivalent.
		 */
		piglit_loge("failed to make context current without surface");
		result = PIGLIT_SKIP;
		goto error;
	}

	piglit_dispatch_default_init(PIGLIT_DISPATCH_ES2);

	/* From the EGL_KHR_fence_sync spec:
	 *
	 *     Each client API which supports fence commands indicates this
	 *     support in the form of a client API extension. If the
	 *     GL_OES_EGL_sync extension is supported by OpenGL ES (either
	 *     version 1.x or 2.0), a fence sync object may be created when the
	 *     currently bound API is OpenGL ES.
	 */
	if (!piglit_is_extension_supported("GL_OES_EGL_sync")) {
		piglit_loge("context does not support GL_OES_EGL_sync; "
			    "skipping test");
		result = PIGLIT_SKIP;
		goto error;
	}

	*out_ctx = ctx;
	return result;

error:
	if (ctx) {
		eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE,
			       EGL_NO_CONTEXT);
		eglDestroyContext(dpy, ctx);
	}
	return result;
}

/**
 * Teardown state after each subtest completes.
 */
static void
test_cleanup(EGLSyncKHR sync, enum piglit_result *inout_result)
{
	bool ok = false;

	if (sync) {
		/* From the EGL_KHR_fence_sync spec:
		 *
		 *     If no errors are generated, EGL_TRUE is returned, and
		 *     <sync> will no longer be the handle of a valid sync
		 *     object.
		 */
		ok = peglDestroySyncKHR(g_dpy, sync);
		if (!ok) {
			piglit_loge("eglDestroySyncKHR failed");
			*inout_result = PIGLIT_FAIL;
		}
		if (!piglit_check_egl_error(EGL_SUCCESS)) {
			piglit_loge("eglDestroySyncKHR emitted an error");
			*inout_result = PIGLIT_FAIL;
		}
	}

	/* Ensure that no leftover GL commands impact the next test. */
	if (eglGetCurrentContext()) {
		glFinish();
	}

	if (g_dpy) {
		eglMakeCurrent(g_dpy, 0, 0, 0);
		ok = eglTerminate(g_dpy);
		if (!ok) {
			piglit_loge("failed to terminate EGLDisplay");
			*inout_result = PIGLIT_FAIL;
		}
	}

	g_dpy = EGL_NO_DISPLAY;
	g_ctx = EGL_NO_CONTEXT;
}

/**
 * Setup state before each subtest begins.
 */
static enum piglit_result
test_setup(void)
{
	enum piglit_result result = PIGLIT_PASS;

	/* Just in case the previous test forgot to unset these pointers... */
	g_dpy = EGL_NO_DISPLAY;
	g_ctx = EGL_NO_CONTEXT;

	result = init_display(EGL_NONE, &g_dpy);
	if (result != PIGLIT_PASS) {
		goto cleanup;
	}


	result = init_context(g_dpy, &g_ctx);
	if (result != PIGLIT_PASS) {
		goto cleanup;
	}
	/* Ensure that a context is bound so that the test can create syncs. */
	eglMakeCurrent(g_dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, g_ctx);

cleanup:
	if (result != PIGLIT_PASS) {
		test_cleanup(EGL_NO_SYNC_KHR, &result);
	}
	return result;
}

static enum piglit_result
test_eglCreateSyncKHR_native_default_attributes(void *test_data)
{
	enum piglit_result result = PIGLIT_PASS;
	EGLSyncKHR sync = 0;
	EGLint sync_type = canary,
	       sync_status = canary,
	       sync_condition = canary;
	bool ok = false;

	result = test_setup();
	if (result != PIGLIT_PASS) {
		return result;
	}

	sync = peglCreateSyncKHR(g_dpy, EGL_SYNC_NATIVE_FENCE_ANDROID, NULL);
	if (sync == EGL_NO_SYNC_KHR) {
		piglit_loge("eglCreateSyncKHR(EGL_SYNC_NATIVE_FENCE_ANDROID) failed");
		result = PIGLIT_FAIL;
		goto cleanup;
	}

	ok = peglGetSyncAttribKHR(g_dpy, sync, EGL_SYNC_TYPE_KHR, &sync_type);
	if (!ok) {
		piglit_loge("eglGetSyncAttribKHR(EGL_SYNC_TYPE_KHR) failed");
		result = PIGLIT_FAIL;
	}
	if (!piglit_check_egl_error(EGL_SUCCESS)) {
		piglit_loge("eglGetSyncAttribKHR(EGL_SYNC_TYPE_KHR) emitted "
			    "an error");
		result = PIGLIT_FAIL;
	}
	if (sync_type != EGL_SYNC_NATIVE_FENCE_ANDROID) {
		piglit_loge("eglGetSyncAttribKHR(EGL_SYNC_TYPE_KHR) returned "
			    "0x%x but expected EGL_SYNC_NATIVE_FENCE_ANDROID(0x%x)",
			    sync_type, EGL_SYNC_NATIVE_FENCE_ANDROID);
		result = PIGLIT_FAIL;
	}

	ok = peglGetSyncAttribKHR(g_dpy, sync, EGL_SYNC_STATUS_KHR, &sync_status);
	if (!ok) {
		piglit_loge("eglGetSyncAttribKHR(EGL_SYNC_STATUS_KHR) failed");
		result = PIGLIT_FAIL;
	}
	if (!piglit_check_egl_error(EGL_SUCCESS)) {
		piglit_loge("eglGetSyncAttribKHR(EGL_SYNC_STATUS_KHR) emitted "
			    "an error");
		result = PIGLIT_FAIL;
	}

	ok = peglGetSyncAttribKHR(g_dpy, sync, EGL_SYNC_CONDITION_KHR, &sync_condition);
	if (!ok) {
		piglit_loge("eglGetSyncAttribKHR(EGL_SYNC_CONDITION_KHR) failed");
		result = PIGLIT_FAIL;
	}
	if (!piglit_check_egl_error(EGL_SUCCESS)) {
		piglit_loge("eglGetSyncAttribKHR(EGL_SYNC_CONDITION_KHR) "
			    "emitted an error");
		result = PIGLIT_FAIL;
	}
	if (sync_condition != EGL_SYNC_PRIOR_COMMANDS_COMPLETE_KHR) {
		piglit_loge("eglGetSyncAttribKHR(EGL_SYNC_CONDITION_KHR) "
			    "returned 0x%x but expected "
			    "EGL_SYNC_PRIOR_COMMANDS_COMPLETE_KHR(0x%x)",
			    sync_condition, EGL_SYNC_PRIOR_COMMANDS_COMPLETE_KHR);
		result = PIGLIT_FAIL;
	}

cleanup:
	test_cleanup(sync, &result);
	return result;
}

static enum piglit_result
test_eglCreateSyncKHR_native_from_fd(void *test_data)
{
	return PIGLIT_PASS;
}

static const struct piglit_subtest fence_sync_subtests[] = {
	{
		"eglCreateSyncKHR_native_no_fence",
		"eglCreateSyncKHR_native_no_fence",
		test_eglCreateSyncKHR_native_default_attributes,
	},
	{
		"eglCreateSyncKHR_native_from_fd",
		"eglCreateSyncKHR_native_from_fd",
		test_eglCreateSyncKHR_native_from_fd,
	},
	{0},
};

static void
init_egl_extension_funcs(void)
{
	peglCreateSyncKHR = (void*) eglGetProcAddress("eglCreateSyncKHR");
	peglDestroySyncKHR = (void*) eglGetProcAddress("eglDestroySyncKHR");
	peglClientWaitSyncKHR = (void*) eglGetProcAddress("eglClientWaitSyncKHR");
	peglWaitSyncKHR = (void*) eglGetProcAddress("eglWaitSyncKHR");
	peglDupNativeFenceFDANDROID = (void*) eglGetProcAddress("eglDupNativeFenceFDANDROID");
	peglGetSyncAttribKHR = (void*) eglGetProcAddress("eglGetSyncAttribKHR");
}

int
main(int argc, char **argv)
{
	enum piglit_result result = PIGLIT_SKIP;
	const char **selected_subtests = NULL;
	size_t num_selected_subtests = 0;
	const struct piglit_subtest *subtests = fence_sync_subtests;

	/* Strip common piglit args. */
	piglit_strip_arg(&argc, argv, "-fbo");
	piglit_strip_arg(&argc, argv, "-auto");

	piglit_parse_subtest_args(&argc, argv, subtests, &selected_subtests,
				  &num_selected_subtests);

	if (argc > 1) {
		fprintf(stderr, "usage error\n");
		piglit_report_result(PIGLIT_FAIL);
	}

	init_egl_extension_funcs();
	result = piglit_run_selected_subtests(subtests, selected_subtests,
					      num_selected_subtests, result);
	piglit_report_result(result);
	assert(!"unreachable");
	return EXIT_FAILURE;
}