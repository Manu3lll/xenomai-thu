#define _GNU_SOURCE // ZWINGEND GANZ OBEN, vor allen Includes!
#include <sched.h>
#include <pthread.h>
#include <sys/mman.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <signal.h>
#include <errno.h>
#include <string.h>
#include <evl/thread.h>
#include <evl/clock.h>
#include <evl/timer.h> // WICHTIG: Für den periodischen Timer
#include <pthread.h>
#include <sched.h>

#define GPIO_PIN 17
#define GPIO_BASE 0xFE200000
#define GPIO_LEN  0xB4

// Periodendauer in Nanosekunden (2000 us = 2.000.000 ns)
#define PERIOD_NS 10000

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
// =========================================================================
    // 1. MEMORY LOCKING (Verhindert Page Faults und In-Band Drops)
    // =========================================================================
    if (mlockall(MCL_CURRENT | MCL_FUTURE) < 0) {
        perror("mlockall failed");
        return 1;
    }

    // =========================================================================
    // 2. CORE AFFINITY (Task auf den isolierten Kern CPU 1 zwingen)
    // =========================================================================
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(1, &cpuset); // 1 = Euer isolierter Kern
    if (pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset) != 0) {
        perror("pthread_setaffinity_np failed");
        return 1;
    }

    // =========================================================================
    // 3. HÖCHSTE PRIORITÄT (SCHED_FIFO)
    // =========================================================================
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

    // =========================================================================
    // 4. KORREKTUR: ABSOLUTE STARTZEIT BERECHNEN!
    // =========================================================================
    struct timespec now;
    evl_read_clock(EVL_CLOCK_MONOTONIC, &now); // Aktuelle Uptime des Pi abfragen

    // Startzeitpunkt: Exakt 1 Sekunde in der Zukunft ab *jetzt*
    value.it_value.tv_sec = now.tv_sec + 1;
    value.it_value.tv_nsec = now.tv_nsec;

    // Periodendauer festlegen (eure PERIOD_NS)
    value.it_interval.tv_sec = 0;
    value.it_interval.tv_nsec = PERIOD_NS;

    // Timer scharfschalten (EVL nimmt den Wert jetzt als echte Zukunft wahr)
    ret = evl_set_timer(timer_fd, &value, NULL);
    if (ret < 0) {
        fprintf(stderr, "evl_set_timer failed\n");
        return 1;
    }


    printf("Periodischer Timer gestartet (%d ns).\n", PERIOD_NS);
    printf("Mit STRG+C beenden.\n");

    int toggle_state = 0;
    uint32_t overruns = 0;
    uint32_t loops = 0;

    while (running) {
        ret = oob_read(timer_fd, &ticks, sizeof(ticks));
        if (ret != sizeof(ticks)) break;

        if (ticks > 1) {
            overruns += (ticks - 1); // Zähle verpasste Deadlines!
        }
        loops++;

        toggle_state = !toggle_state;
        gpio_write(GPIO_PIN, toggle_state);
    }

    printf("\nTest beendet.\n");
    printf("Erfolgreiche Zyklen: %u\n", loops);
    printf("Verpasste Deadlines (Overruns): %u\n", overruns);

    gpio_write(GPIO_PIN, 0);
    close(timer_fd);
    munmap(map, GPIO_LEN);
    close(fd);

    return 0;
}
