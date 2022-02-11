/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2022 Corey Hinshaw <corey@electrickite.org>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <libutil.h>
#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <dev/pwm/pwmc.h>
#include <sys/ioctl.h>
#include <sys/sysctl.h>
#include <sys/types.h>

#define PROGNAME "fand"
#define VERSION "0.2.2"
#define KELVIN_OFFSET -273.15

static struct pidfh *pfh = NULL;
static char *pid_path = "/var/run/" PROGNAME ".pid";
static char pwm_device[PATH_MAX];
static char *temp_name = NULL;
static int fd = -1;
static bool inverted = false;
static bool daemonize = true;
static bool high_temp_set = false;
static bool show_status = false;
static float multiplier = 1.0;
static float offset = 0.0;
static int off_temp = 50;
static int low_temp = 50;
static int high_temp = 0;
static unsigned int interval = 60;
static unsigned int low_period = 0;
static unsigned int low_duty = 0;
static bool low_duty_percent = false;
static unsigned int high_period = 0;
static unsigned int high_duty = 0;
static bool high_duty_percent = false;

static void usage(void)
{
	fprintf(stderr, "Usage:\n");
	fprintf(stderr, "\t%s [options...] sensor fan_device\n", PROGNAME);
	exit(EXIT_FAILURE);
}

static void print_version(void)
{
	printf("%s %s\n", PROGNAME, VERSION);
}

static void set_temp_name(char *name)
{
	if (name[0] == '\0')
		errx(EXIT_FAILURE, "Temperature sensor sysctl name not specified");
	temp_name = name;
}

static void set_pwm_device(const char *dev)
{
	if (dev[0] == '\0')
		errx(EXIT_FAILURE, "PWM device not specified");
	if (dev[0] == '/')
		strlcpy(pwm_device, dev, sizeof(pwm_device));
	else
		snprintf(pwm_device, sizeof(pwm_device), "/dev/pwm/%s", dev);
}

static void set_duty(char *arg, unsigned int *duty, bool *percent)
{
	char *end;

	*duty = strtoul(arg, &end, 10);
	if (*end == '%') {
		*percent = true;
		if (*duty > 100) {
			fprintf(stderr, "Invalid duty percentage\n");
			usage();
		}
	} else if (*end != '\0')
		usage();
}

unsigned int calculate_duty(unsigned int duty, unsigned int period, bool percent)
{
	if (percent)
		return (uint64_t)period * duty / 100;
	else
		return duty;
}

static float current_temperature(void)
{
	int raw;
	size_t len = sizeof(int);

	sysctlbyname(temp_name, &raw, &len, NULL, 0);
	return raw * multiplier + offset;
}

static void print_status(struct pwm_state state, float temp)
{
	printf("Sensor: %s\n  Temperature: %.1f\n", temp_name, temp);
	printf("Fan PWM Device: %s\n  Period: %u\n  Duty: %u\n  Enabled: %d\n  Inverted: %d\n",
		pwm_device,
		state.period,
		state.duty,
		state.enable,
		state.flags & PWM_POLARITY_INVERTED);
}


void cleanup(void)
{
	if (fd) close(fd);
	pidfile_remove(pfh);
}

int main(int argc, char *argv[])
{
	struct pwm_state current_state, new_state;
	pid_t otherpid;
	int ch;
	float temp;

	pwm_device[0] = '\0';
	memset(&current_state, 0, sizeof(current_state));
	memset(&new_state, 0, sizeof(new_state));

	atexit(cleanup);

	while ((ch = getopt(argc, argv, ":t:T:o:i:IKm:p:P:d:D:r:fsv")) != -1) {
		switch (ch) {
		case 't':
			low_temp = strtol(optarg, NULL, 10);
			break;
		case 'T':
			high_temp_set = true;
			high_temp = strtol(optarg, NULL, 10);
			break;
		case 'o':
			off_temp = strtol(optarg, NULL, 10);
			break;
		case 'i':
			interval = strtol(optarg, NULL, 10);
			break;
		case 'I':
			inverted = true;
			break;
		case 'K':
			offset = KELVIN_OFFSET;
			break;
		case 'm':
			multiplier = strtof(optarg, NULL);
			break;
		case 'p':
			low_period = strtoul(optarg, NULL, 10);
			break;
		case 'P':
			high_period = strtoul(optarg, NULL, 10);
			break;
		case 'd':
			set_duty(optarg, &low_duty, &low_duty_percent);
			break;
		case 'D':
			set_duty(optarg, &high_duty, &high_duty_percent);
			break;
		case 'f':
			daemonize = false;
			break;
		case 'r':
			pid_path = optarg;
			break;
		case 's':
			show_status = true;
			break;
		case 'v':
			print_version();
			exit(EXIT_SUCCESS);
			break;
		case '?':
			fprintf(stderr, "Unknown option `-%c'\n", optopt);
			usage();
			break;
		case ':':
			errx(EXIT_FAILURE, "Option -%c requires an argument.", optopt);
			break;
		}
	}

	if (argc - optind != 2) {
		fprintf(stderr, "Wrong number of arguments!\n");
		usage();
	}
	set_temp_name(argv[optind]);
	set_pwm_device(argv[optind + 1]);

	if (off_temp > low_temp)
		off_temp = low_temp;
	if (high_temp <= low_temp)
		high_temp_set = false;

	if ((fd = open(pwm_device, O_RDWR)) == -1)
		err(EXIT_FAILURE, "Cannot open %s", pwm_device);
	if (ioctl(fd, PWMGETSTATE, &current_state) == -1)
		err(EXIT_FAILURE, "Cannot read state of the PWM controller");

	if (show_status) {
		print_status(current_state, current_temperature());
		exit(EXIT_SUCCESS);
	}

  	if (daemonize) {
		pfh = pidfile_open(pid_path, 0600, &otherpid);
		if (pfh == NULL) {
			if (errno == EEXIST)
				errx(EXIT_FAILURE, "already running, pid: %jd.", (intmax_t)otherpid);
			warn("Cannot open or create pidfile");
		}
  		if (daemonize && daemon(1, 1) < 0) {
  			err(EXIT_FAILURE, "Failed to daemonize!");
		}
		pidfile_write(pfh);
	} else
		print_status(current_state, current_temperature());

	for(;;) {
		if ((fd = open(pwm_device, O_RDWR)) == -1)
			err(EXIT_FAILURE, "Cannot open %s", pwm_device);
		if (ioctl(fd, PWMGETSTATE, &current_state) == -1)
			err(EXIT_FAILURE, "Cannot read state of the PWM controller");
		new_state = current_state;
		temp = current_temperature();

		if (temp >= low_temp)
			new_state.enable = true;
		else if (temp < off_temp)
			new_state.enable = false;

		if (high_temp_set && temp >= high_temp) {
			new_state.period = high_period;
			new_state.duty = calculate_duty(high_duty, new_state.period, high_duty_percent);
		} else {
			new_state.period = low_period;
			new_state.duty = calculate_duty(low_duty, new_state.period, low_duty_percent);
		}

		if (inverted)
			new_state.flags |= PWM_POLARITY_INVERTED;
		else
			new_state.flags &= ~PWM_POLARITY_INVERTED;

		if (memcmp(&current_state, &new_state, sizeof(current_state)) != 0) {
			if (ioctl(fd, PWMSETSTATE, &new_state) == -1)
				err(EXIT_FAILURE, "Cannot configure the PWM controller");
			if (!daemonize) {
				printf(">> Fan state change <<\n");
				print_status(new_state, temp);
			}
		}

		close(fd);
		sleep(interval);
	}

	return EXIT_SUCCESS;
}
