#define _GNU_SOURCE
#include <sched.h>
#include <pthread.h>
#include <sys/mman.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <string.h>
#include <evl/thread.h>
#include <evl/clock.h>
#include <evl/timer.h>

#define GPIO_PIN 17
#define GPIO_BASE 0xFE200000
#define GPIO_LEN  0xB4

#define HALF_PERIOD_NS 15000

volatile uint32_t *gpio;
volatile int running = 1;

void handle_signal(int sig)
{
    running = 0;
}

void gpio_set_output(int pin)
{
    int reg = pin / 10;
    int shift = (pin % 10) * 3;
    gpio[reg] &= ~(7 << shift);
    gpio[reg] |=  (1 << shift);
}

void gpio_write(int pin, int value)
{
    if (value) {
        gpio[7] = 1 << pin;
    } else {
        gpio[10] = 1 << pin;
    }
}

int main(void)
{
    if (mlockall(MCL_CURRENT | MCL_FUTURE) < 0) {
        perror("mlockall failed");
        return 1;
    }
    
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(1, &cpuset); // 1 = isolated core
    if (pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset) != 0) {
        perror("pthread_setaffinity_np failed");
        return 1;
    }

    struct sched_param param;
    param.sched_priority = 99;
    if (pthread_setschedparam(pthread_self(), SCHED_FIFO, &param) != 0) {
        perror("pthread_setschedparam failed");
        return 1;
    }

    int fd, timer_fd;
    void *map;
    int ret;
    struct itimerspec value;
    uint64_t ticks;

    signal(SIGINT, handle_signal);

    fd = open("/dev/mem", O_RDWR | O_SYNC);
    if (fd < 0) {
        perror("open /dev/mem");
        return 1;
    }
    map = mmap(NULL, GPIO_LEN, PROT_READ | PROT_WRITE, MAP_SHARED, fd, GPIO_BASE);
    if (map == MAP_FAILED) {
        perror("mmap");
        close(fd);
        return 1;
    }
    gpio = (volatile uint32_t *)map;
    gpio_set_output(GPIO_PIN);
    gpio_write(GPIO_PIN, 0);

    ret = evl_attach_self("gpio-periodic");
    if (ret < 0) {
        fprintf(stderr, "evl_attach_self failed: %s\n", strerror(-ret));
        return 1;
    }


timer_fd = evl_new_timer(EVL_CLOCK_MONOTONIC);
    if (timer_fd < 0) {
        fprintf(stderr, "evl_new_timer failed\n");
        return 1;
    }

    struct timespec now;
    evl_read_clock(EVL_CLOCK_MONOTONIC, &now); 

    value.it_value.tv_sec = now.tv_sec + 1;
    value.it_value.tv_nsec = now.tv_nsec;

    value.it_interval.tv_sec = 0;
    value.it_interval.tv_nsec = HALF_PERIOD_NS;

    ret = evl_set_timer(timer_fd, &value, NULL);
    if (ret < 0) {
        fprintf(stderr, "evl_set_timer failed\n");
        return 1;
    }

    printf("Periodic Timer started (%d ns).\n", HALF_PERIOD_NS*2);
    printf("CTRL+C to stop.\n");

    int toggle_state = 0;
    uint32_t overruns = 0;
    uint32_t loops = 0;

    while (running) {
        ret = oob_read(timer_fd, &ticks, sizeof(ticks));
        if (ret != sizeof(ticks)) break;

        if (ticks > 1) {
            overruns += (ticks - 1); 
        }
        loops++;

        toggle_state = !toggle_state;
        gpio_write(GPIO_PIN, toggle_state);
    }

    printf("\nTest finished.\n");
    printf("Successful cycles: %u\n", loops);
    printf("Missede Deadlines (Overruns): %u\n", overruns);

    gpio_write(GPIO_PIN, 0);
    close(timer_fd);
    munmap(map, GPIO_LEN);
    close(fd);

    return 0;
}
