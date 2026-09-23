#include "hplp/usb_discovery.h"

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static volatile sig_atomic_t keep_running = 1;

static void on_signal(int signal_number)
{
    (void)signal_number;
    keep_running = 0;
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
        const hplp_usb_device_t *d = &devices[i];

        printf("%s | %04x:%04x | bus=%u address=%u | protocol=%s",
               d->model->model,
               d->vendor_id,
               d->product_id,
               d->bus_number,
               d->device_address,
               hplp_protocol_name(d->model->protocol));

        if (d->serial[0] != '\0') {
            printf(" | serial=%s", d->serial);
        }

        printf(" | firmware=%s\n",
               d->model->firmware_required ? "required" : "not-required");
    }

    hplp_usb_list_free(devices);
    return 0;
}

static void sleep_one_second(void)
{
    struct timespec duration = {1, 0};
    nanosleep(&duration, NULL);
}

static int watch_devices(void)
{
    int last_present = -1;

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

        int present = count > 0;

        if (present != last_present) {
            if (present) {
                puts("STATE READY");
                for (size_t i = 0; i < count; ++i) {
                    printf("  %s %04x:%04x",
                           devices[i].model->model,
                           devices[i].vendor_id,
                           devices[i].product_id);
                    if (devices[i].serial[0] != '\0') {
                        printf(" serial=%s", devices[i].serial);
                    }
                    putchar('\n');
                }
            } else {
                puts("STATE DISCONNECTED");
            }

            fflush(stdout);
            last_present = present;
        }

        hplp_usb_list_free(devices);
        sleep_one_second();
    }

    return 0;
}

static void usage(const char *program)
{
    fprintf(stderr, "Usage: %s --list | --watch\n", program);
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

    usage(argv[0]);
    return 2;
}
