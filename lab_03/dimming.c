#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <linux/gpio.h>
#include <evl/devices/gpio-abi.h>
#include <evl/thread.h>
#include <evl/timer.h>
#include <evl/clock.h>
#include <evl/syscall.h>
#include <evl/proxy.h>

#define GPIO_PIN 18
#define TOTAL_PERIOD_US 10000 
#define NSTEPS 100
#define STEP_PERIOD_US (TOTAL_PERIOD_US / NSTEPS)

uint64_t on_time_us;
uint64_t total_period_us;
int gpio_fd;
int timer_fd;

enum DIRECTION { DOWN = 0, UP = 1 };

void set_gpio_value(int value) {
    struct gpiohandle_data data;
    data.values[0] = value;
    oob_ioctl(gpio_fd, GPIOHANDLE_SET_LINE_VALUES_IOCTL, &data);
}

static void pwm_task() {
    int step = 0;
    enum DIRECTION dir = UP;
    uint64_t ticks;

    while (1) {
        set_gpio_value(1);

        if (on_time_us > 0) {
            evl_usleep(on_time_us);
        }

        set_gpio_value(0);

        if (step == NSTEPS) {
            dir = !dir;
            step = 0;
        }
        step++;

        if (dir == UP) {
            on_time_us = step * STEP_PERIOD_US;
        } else if (dir == DOWN) {
            on_time_us = total_period_us - (step * STEP_PERIOD_US);
        }

        if (oob_read(timer_fd, &ticks, sizeof(ticks)) != sizeof(ticks)) {
            evl_printf("oob_read failed\n");
            break;
        }
    }
}
int main(int argc, char *argv[]) {
    int chip_fd = open("/dev/gpiochip0", O_RDWR);
    struct gpiohandle_request req = {
        .lineoffsets = { GPIO_PIN },
        .flags = GPIOHANDLE_REQUEST_OUTPUT | GPIOHANDLE_REQUEST_OOB,
        .lines = 1
    };
    ioctl(chip_fd, GPIO_GET_LINEHANDLE_IOCTL, &req);
    gpio_fd = req.fd;
    close(chip_fd);

    evl_attach_self("dimming-task:%d", getpid());
    timer_fd = evl_new_timer(EVL_CLOCK_MONOTONIC);

    struct itimerspec timer_conf = {
        .it_interval = { 0, TOTAL_PERIOD_US * 1000 },
        .it_value = { 0, TOTAL_PERIOD_US * 1000 }
    };
    evl_set_timer(timer_fd, &timer_conf, NULL);

    on_time_us = 0;
    total_period_us = TOTAL_PERIOD_US;
    
    evl_printf("Starting LED dimming...\n");
    pwm_task(); 

    close(gpio_fd);
    close(timer_fd);
    return 0;
}
