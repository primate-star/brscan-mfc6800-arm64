/* config.h for Brother MFC SANE backend - ARM64 */

#ifndef SANE_CONFIG_H
#define SANE_CONFIG_H

/* Version information */
#define V_MAJOR 1
#define V_MINOR 0
#define V_REV 7

/* Package information */
#define PACKAGE "sane-backends"
#define VERSION "1.0.7"

/* System includes */
#define HAVE_SYS_TYPES_H 1
#define HAVE_SYS_STAT_H 1
#define HAVE_STDLIB_H 1
#define HAVE_STRING_H 1
#define HAVE_MEMORY_H 1
#define HAVE_STRINGS_H 1
#define HAVE_INTTYPES_H 1
#define HAVE_STDINT_H 1
#define HAVE_UNISTD_H 1
#define HAVE_FCNTL_H 1
#define HAVE_ERRNO_H 1
#define HAVE_LIMITS_H 1
#define HAVE_SIGNAL_H 1
#define HAVE_SIGPROCMASK 1

/* USB support */
#define HAVE_LIBUSB 1
#define HAVE_USB_H 1

/* Path configuration */
#define PATH_SANE_CONFIG_DIR "/etc/sane.d"

#ifndef STRINGIFY
#define STRINGIFY(x) #x
#define STRINGIFY2(x) STRINGIFY(x)
#endif

/* Internationalization */
#ifndef SANE_I18N
#define SANE_I18N(text) text
#endif

/* Debug support - will be redefined by sanei_debug.h */
#ifndef DBG_LEVEL
#define DBG_LEVEL 0
#endif

#endif /* SANE_CONFIG_H */
