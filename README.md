# Xenomai 4 (EVL) - Real-Time Systems Lab Exercises

This archive contains the source code and build files for the updated real-time laboratory exercises running on a Raspberry Pi 4B under Xenomai 4 (EVL core). Each exercise is fully self-contained, utilizing a `Makefile` to handle dependencies and automated linking against `libevl` and POSIX real-time extensions.

## Repository Structure

```text
.
├── hello_world/
│   ├── helloworld.c      # Minimal real-time thread attachment test
│   └── Makefile          # Compiles the hello_world binary
├── lab_03/
│   ├── dimming.c         # Smooth LED PWM dimmer (Triangle-wave algorithm)
│   └── Makefile          # Compiles the dimming binary
└── lab_07/
    ├── generator.c       # OOB pulse generator (Signal Output on GPIO 23)
    ├── listener.c        # OOB interrupt handler & latency benchmark (Input on GPIO 24)
    └── Makefile          # Compiles both generator and listener binaries
