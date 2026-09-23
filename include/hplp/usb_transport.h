#ifndef HPLP_USB_TRANSPORT_H
#define HPLP_USB_TRANSPORT_H

#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

typedef struct hplp_usb_session hplp_usb_session_t;

int hplp_usb_session_open(uint16_t vendor_id,
                          uint16_t product_id,
                          const char *serial,
                          hplp_usb_session_t **session_out);
ssize_t hplp_usb_session_write(hplp_usb_session_t *session,
                               const void *buffer,
                               size_t bytes);
void hplp_usb_session_close(hplp_usb_session_t *session);
size_t hplp_usb_session_bytes_accepted(const hplp_usb_session_t *session);
int hplp_usb_session_last_error(const hplp_usb_session_t *session);
const char *hplp_usb_error_string(int error_code);

int hplp_usb_transport_watch(void);

#endif
