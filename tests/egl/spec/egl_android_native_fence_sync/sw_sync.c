/*
 * Copyright 2012 Google, Inc
 * Copyright © 2016 Collabora, Ltd.
 *
 * Based on the implementation from the Android Open Source Project
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
 *
 * Authors:
 *    Robert Foss <robert.foss@collabora.com>
 */

#include <fcntl.h>
#include <poll.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>
#include <linux/sync_file.h>
#include <sys/ioctl.h>

#include "sw_sync.h"

#ifndef SW_SYNC_IOC_INC
struct sw_sync_create_fence_data {
	__u32	value;
	char	name[32];
	__s32	fence;
};

#define SW_SYNC_IOC_MAGIC		'W'
#define SW_SYNC_IOC_CREATE_FENCE	_IOWR(SW_SYNC_IOC_MAGIC, 0,\
						struct sw_sync_create_fence_data)
#define SW_SYNC_IOC_INC			_IOW(SW_SYNC_IOC_MAGIC, 1, __u32)
#endif

#define DEVFS_SW_SYNC   "/dev/sw_sync"
#define DEBUGFS_SW_SYNC "/sys/kernel/debug/sync/sw_sync"

bool sw_sync_is_supported(void)
{
	if(access(DEVFS_SW_SYNC, R_OK | W_OK) != -1) {
		return true;
	} else if (access(DEBUGFS_SW_SYNC, R_OK | W_OK) != -1 ) {
		return true;
	}

	return false;
}

int sw_sync_fd_is_valid(int fd)
{
	int status;

	if (fd < 0)
		return 0;

	status = fcntl(fd, F_GETFD, 0);
	return status >= 0;
}

static
void sw_sync_fd_close(int fd)
{
	if (!sw_sync_fd_is_valid(fd))
		return;

	close(fd);
}

int sw_sync_timeline_create(void)
{
	int fd = open(DEVFS_SW_SYNC, O_RDWR);

	if (!sw_sync_fd_is_valid(fd))
		fd = open(DEBUGFS_SW_SYNC, O_RDWR);

	return fd;
}

void sw_sync_timeline_destroy(int fd)
{
	return sw_sync_fd_close(fd);
}

void sw_sync_fence_destroy(int fd)
{
	return sw_sync_fd_close(fd);
}

int sw_sync_fence_create(int fd, int32_t seqno)
{
	struct sw_sync_create_fence_data data = {};
	data.value = seqno;

	if (fd >= 0) {
		ioctl(fd, SW_SYNC_IOC_CREATE_FENCE, &data);
		return data.fence;
	} else {
		ioctl(fd, SW_SYNC_IOC_CREATE_FENCE, &data);
		return -1;
	}
}

void sw_sync_timeline_inc(int fd, uint32_t count)
{
	uint32_t arg = count;

	if (fd == 0 || fd == -1)
		return;

	ioctl(fd, SW_SYNC_IOC_INC, &arg);
}

static struct sync_file_info *sync_file_info(int fd)
{
	struct sync_file_info *info;
	struct sync_fence_info *fence_info;
	int err, num_fences;

	info = calloc(1, sizeof(*info));
	if (info == NULL)
		return NULL;

	err = ioctl(fd, SYNC_IOC_FILE_INFO, info);
	if (err < 0) {
		free(info);
		return NULL;
	}

	num_fences = info->num_fences;

	if (num_fences) {
		info->flags = 0;
		info->num_fences = num_fences;

		fence_info = calloc(num_fences, sizeof(*fence_info));
		if (!fence_info) {
			free(info);
			return NULL;
		}

		info->sync_fence_info = (uint64_t)(unsigned long) (fence_info);

		err = ioctl(fd, SYNC_IOC_FILE_INFO, info);
		if (err < 0) {
			free(fence_info);
			free(info);
			return NULL;
		}
	}

	return info;
}

static void sync_file_info_free(struct sync_file_info *info)
{
	free((void *)(uintptr_t)info->sync_fence_info);
	free(info);
}

int sw_sync_fence_size(int fd)
{
	int count;
	struct sync_file_info *info = sync_file_info(fd);

	if (!info)
		return 0;

	count = info->num_fences;

	sync_file_info_free(info);

	return count;
}

int sw_sync_fence_count_status(int fd, int status)
{
	int i, count = 0;
	struct sync_fence_info *fence_info = NULL;
	struct sync_file_info *info = sync_file_info(fd);

	if (!info)
		return -1;

	fence_info = (struct sync_fence_info *)(uintptr_t)info->sync_fence_info;
	for (i = 0 ; i < info->num_fences ; i++) {
		if (fence_info[i].status == status)
			count++;
	}

	sync_file_info_free(info);

	return count;
}
