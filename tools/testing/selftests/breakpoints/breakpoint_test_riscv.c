// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Qualcomm Technologies, Inc.
 *
 * Author: Himanshu Chauhan <himanshu.chauhan@oss.qualcomm.com>
 */

#define _GNU_SOURCE
#include <linux/perf_event.h>    /* Definition of PERF_* constants */
#include <linux/hw_breakpoint.h> /* Definition of HW_* constants */
#include <sys/syscall.h>         /* Definition of SYS_* constants */
#include <unistd.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <time.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/mman.h>
#include <string.h>
#include <semaphore.h>

static int gfd;
sem_t ib_mtx, wp_mtx;
static int bp_triggered, wp_triggered;

int setup_bp(bool is_x, void *addr, int sig)
{
	struct perf_event_attr pe;
	int fd;

	memset(&pe, 0, sizeof(struct perf_event_attr));
	pe.type = PERF_TYPE_BREAKPOINT;
	pe.size = sizeof(struct perf_event_attr);

	pe.config = 0;
	pe.bp_type = is_x ? HW_BREAKPOINT_X : HW_BREAKPOINT_W;
	pe.bp_addr = (unsigned long)addr;
	pe.bp_len = sizeof(long);

	pe.sample_period = 1;
	pe.sample_type = PERF_SAMPLE_IP;
	pe.wakeup_events = 1;

	pe.disabled = 1;
	pe.exclude_kernel = 1;
	pe.exclude_hv = 1;

	fd = syscall(SYS_perf_event_open, &pe, 0, -1, -1, 0);
	if (fd < 0) {
		printf("Failed to open event: %llx\n", pe.config);
		return -1;
	}

	fcntl(fd, F_SETFL, O_RDWR | O_NONBLOCK | O_ASYNC);
	fcntl(fd, F_SETSIG, sig);
	fcntl(fd, F_SETOWN, getpid());

	ioctl(fd, PERF_EVENT_IOC_RESET, 0);

	return fd;
}

static void sig_handler_bp(int signum, siginfo_t *oh, void *uc)
{
	int ret;

	bp_triggered++;

	printf("Breakpoint triggered!\n");
	ioctl(gfd, PERF_EVENT_IOC_DISABLE, 0);
	ret = sem_post(&ib_mtx);
	if (ret) {
		printf("Failed to report BP success\n");
		return;
	}
}

static void sig_handler_wp(int signum, siginfo_t *oh, void *uc)
{
	int ret;

	printf("Watchpoint triggered!\n");
	ioctl(gfd, PERF_EVENT_IOC_DISABLE, 0);
	wp_triggered++;

	ret = sem_post(&wp_mtx);

	if (ret) {
		printf("Failed to report WP success\n");
		return;
	}
}

static void test_func(void)
{
}

static int trigger_bp(void)
{
	struct sigaction sa;

	memset(&sa, 0, sizeof(struct sigaction));
	sa.sa_sigaction = (void *)sig_handler_bp;
	sa.sa_flags = SA_SIGINFO;

	if (sigaction(SIGIO, &sa, NULL) < 0) {
		printf("Failed to setup signal handler\n");
		return -1;
	}

	gfd = setup_bp(1, test_func, SIGIO);

	if (gfd < 0) {
		printf("Failed to setup breakpoint.\n");
		return -1;
	}

	ioctl(gfd, PERF_EVENT_IOC_ENABLE, 0);

	test_func();

	ioctl(gfd, PERF_EVENT_IOC_DISABLE, 0);

	close(gfd);
}

static int trigger_wp(void)
{
	struct sigaction sa;
	unsigned long test_data;

	memset(&sa, 0, sizeof(struct sigaction));
	sa.sa_sigaction = (void *)sig_handler_wp;
	sa.sa_flags = SA_SIGINFO;

	if (sigaction(SIGUSR1, &sa, NULL) < 0) {
		printf("Failed to setup signal handler\n");
		return -1;
	}

	gfd = setup_bp(0, &test_data, SIGUSR1);

	if (gfd < 0) {
		printf("Failed to setup watchpoint\n");
		return -1;
	}

	ioctl(gfd, PERF_EVENT_IOC_ENABLE, 0);
	test_data = 0xdeadbeef;
	ioctl(gfd, PERF_EVENT_IOC_DISABLE, 0);
}

int main(int argc, char *argv[])
{
	struct sigaction sa;

	sem_init(&ib_mtx, 0, 0);
	trigger_bp();
	sem_wait(&ib_mtx);

	if (bp_triggered)
		printf("Breakpoint test passed!\n");

	sem_init(&wp_mtx, 0, 0);
	trigger_wp();
	sem_wait(&wp_mtx);

	if (wp_triggered)
		printf("Watchpoint test passed!\n");

	return 0;
}
