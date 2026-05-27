#include <stdio.h>
#include <unistd.h>
#include <evl/thread.h>
#include <evl/proxy.h>

int main(int argc, char *argv[]) {
	if(evl_attach_self("hello-world:%d", getpid()) < 0) {
        	evl_printf("Unable to attach thread (did you sudo?)\n");
        	return 1;
    	}
	
	evl_printf("Hello World! Greetings from Xenomai 4 Real-Time core running on a Raspberry Pi!\n");

    	return 0;
}
