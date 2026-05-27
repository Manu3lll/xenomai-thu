#include <string.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <linux/gpio.h>
#include <evl/devices/gpio-abi.h>
#include <evl/thread.h>
#include <evl/clock.h>
#include <evl/proxy.h>
#include <stdio.h>
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/mman.h>

#define PIN 23
#define SHM_NAME "/evl_latency_shm"

int main(int argc, char *argv[]) {
    int shm_fd = shm_open(SHM_NAME, O_CREAT | O_RDWR, 0666);
    if (shm_fd < 0) {
        evl_printf("Failed to create shared memory\n");
        return 1;
    }
    ftruncate(shm_fd, sizeof(struct timespec));
    struct timespec *start_time = mmap(0, sizeof(struct timespec), PROT_WRITE, MAP_SHARED, shm_fd, 0);

    int gpio_fd = open("/dev/gpiochip0", O_RDWR);
    struct gpiohandle_request req = {
        .lineoffsets = { PIN },
        .lines = 1,
        .flags = GPIOHANDLE_REQUEST_OUTPUT | GPIOHANDLE_REQUEST_OOB,
        .default_values = { 0 }
    };
    strcpy(req.consumer_label, "latency-generator");
    if(ioctl(gpio_fd, GPIO_GET_LINEHANDLE_IOCTL, &req)) {
        evl_printf("Requesting line handle failed\n");
        return 1;
    }
    close(gpio_fd);

    if(evl_attach_self("latency-generator:%d", getpid()) < 0) {
        evl_printf("Unable to attach thread (did you sudo?)\n");
        return 1;
    }

    struct gpiohandle_data gpio_data;
    evl_printf("Generator gestartet auf Pin %d. Sende Ticks...\n", PIN);

    while(1) {
        evl_read_clock(EVL_CLOCK_MONOTONIC, start_time);

        gpio_data.values[0] = 1;
        oob_ioctl(req.fd, GPIOHANDLE_SET_LINE_VALUES_IOCTL, &gpio_data);

        evl_usleep(100);

        gpio_data.values[0] = 0;
        oob_ioctl(req.fd, GPIOHANDLE_SET_LINE_VALUES_IOCTL, &gpio_data);

        evl_usleep(500000);
    }
    return 0;
}
