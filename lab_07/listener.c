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

#define PIN 24
#define SHM_NAME "/evl_latency_shm"

int main(int argc, char *argv[]) {
    int shm_fd = shm_open(SHM_NAME, O_RDONLY, 0666);
    if (shm_fd < 0) {
        evl_printf("Failed to open shared memory. Start generator first!\n");
        return 1;
    }
    struct timespec *start_time = mmap(0, sizeof(struct timespec), PROT_READ, MAP_SHARED, shm_fd, 0);

    int gpio_fd = open("/dev/gpiochip0", O_RDWR);
    struct gpioevent_request req = {
        .lineoffset = PIN,
        .handleflags = GPIOHANDLE_REQUEST_INPUT | GPIOHANDLE_REQUEST_OOB,
        .eventflags = GPIOEVENT_REQUEST_RISING_EDGE 
    };
    strcpy(req.consumer_label, "latency-listener");
    if(ioctl(gpio_fd, GPIO_GET_LINEEVENT_IOCTL, &req)) {
        evl_printf("Requesting line failed\n");
        return 1;
    }
    close(gpio_fd);

    if(evl_attach_self("latency-listener:%d", getpid()) < 0) {
        evl_printf("Unable to attach thread\n");
        return 1;
    }

    struct gpioevent_data event;
    struct timespec end_time;
    int i = 0;
    evl_printf("Listener gestartet auf Pin %d. Warte auf Interrupts...\n", PIN);

    while(1) {
        if (oob_read(req.fd, &event, sizeof(event)) < sizeof(event)) {
            evl_printf("oob_read failed\n");
            return 1;
        }

        evl_read_clock(EVL_CLOCK_MONOTONIC, &end_time);

        long long diff_ns = (end_time.tv_sec - start_time->tv_sec) * 1000000000LL + 
                            (end_time.tv_nsec - start_time->tv_nsec);
        
        evl_printf("Interrupt %d empfangen! Latenz: %lld us\n", ++i, diff_ns / 1000);
    }
    return 0;
}
