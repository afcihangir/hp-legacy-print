#define _POSIX_C_SOURCE 200809L

#include "hplp/usb_discovery.h"
#include "hplp/usb_inspect.h"

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static volatile sig_atomic_t keep_running = 1;

typedef enum {
    WATCH_DISCONNECTED = 0,
    WATCH_CONNECTING = 1,
    WATCH_READY = 2
} watch_state_t;

static void on_signal(int signal_number)
{
    (void)signal_number;
    keep_running = 0;
}

static void print_device(const hplp_usb_device_t *d)
{
    printf("%s | %04x:%04x | bus=%u address=%u | protocol=%s",
           d->model->model,
           d->vendor_id,
           d->product_id,
           d->bus_number,
           d->device_address,
           hplp_protocol_name(d->model->protocol));

    if (d->serial_state == HPLP_SERIAL_READY) {
        printf(" | serial=%s", d->serial);
    } else if (d->serial_state == HPLP_SERIAL_PENDING) {
        printf(" | serial=pending(error=%d)", d->serial_error);
    } else {
        printf(" | serial=not-present");
    }

    printf(" | firmware=%s\n",
           d->model->firmware_required ? "required" : "not-required");
}

static int print_devices(void)
{
    hplp_usb_device_t *devices = NULL;
    size_t count = 0;

    int rc = hplp_usb_list(&devices, &count);
    if (rc != 0) {
        fprintf(stderr, "USB discovery failed: %d\n", rc);
        return 1;
    }

    if (count == 0) {
        puts("No supported HP LaserJet printer found.");
        hplp_usb_list_free(devices);
        return 0;
    }

    for (size_t i = 0; i < count; ++i) {
        print_device(&devices[i]);
    }

    hplp_usb_list_free(devices);
    return 0;
}

static void sleep_one_second(void)
{
    struct timespec duration = {1, 0};
    nanosleep(&duration, NULL);
}

static watch_state_t state_for_devices(
    const hplp_usb_device_t *devices,
    size_t count)
{
    if (count == 0) {
        return WATCH_DISCONNECTED;
    }

    for (size_t i = 0; i < count; ++i) {
        if (devices[i].serial_state == HPLP_SERIAL_PENDING) {
            return WATCH_CONNECTING;
        }
    }

    return WATCH_READY;
}

static void print_watch_devices(
    watch_state_t state,
    const hplp_usb_device_t *devices,
    size_t count)
{
    switch (state) {
    case WATCH_DISCONNECTED:
        puts("STATE DISCONNECTED");
        break;
    case WATCH_CONNECTING:
        puts("STATE CONNECTING");
        break;
    case WATCH_READY:
        puts("STATE READY");
        break;
    }

    for (size_t i = 0; i < count; ++i) {
        printf("  ");
        print_device(&devices[i]);
    }

    fflush(stdout);
}

static int watch_devices(void)
{
    int last_state = -1;

    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);

    puts("Watching supported HP LaserJet USB devices. Ctrl+C to stop.");

    while (keep_running) {
        hplp_usb_device_t *devices = NULL;
        size_t count = 0;

        int rc = hplp_usb_list(&devices, &count);
        if (rc != 0) {
            fprintf(stderr, "STATE ERROR discovery=%d\n", rc);
            sleep_one_second();
            continue;
        }

        watch_state_t state = state_for_devices(devices, count);

        if ((int)state != last_state) {
            print_watch_devices(state, devices, count);
            last_state = (int)state;
        }

        hplp_usb_list_free(devices);
        sleep_one_second();
    }

    return 0;
}

static void usage(const char *program)
{
    fprintf(stderr, "Usage: %s --list | --watch | --inspect\n", program);
}

int main(int argc, char **argv)
{
    if (argc != 2) {
        usage(argv[0]);
        return 2;
    }

    if (strcmp(argv[1], "--list") == 0) {
        return print_devices();
    }

    if (strcmp(argv[1], "--watch") == 0) {
        return watch_devices();
    }

    if (strcmp(argv[1], "--inspect") == 0) {
        return hplp_usb_inspect_supported();
    }

    usage(argv[0]);
    return 2;
}
