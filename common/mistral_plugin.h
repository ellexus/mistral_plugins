/* This file lists all the publicly accessible data and interfaces needed when
 * writing a plug-in for Mistral
 */

#ifndef MISTRAL_PLUGIN_H
#define MISTRAL_PLUGIN_H

#include <stdbool.h>
#include <stdint.h>
#include <sys/types.h>
#include <time.h>

/* Define the valid plug-in types */
#define PLUGIN(X)  \
    X(0, OUTPUT)   \
    X(1, UPDATE)   \
    X(2, MAX)


#define X(N, V) extern const uint8_t V ## _PLUGIN;
    PLUGIN(X)
#undef X

#define CONTRACT(X)                                  \
    X(0, MONITORING, "monitor", "monitortimeframe")  \
    X(1, THROTTLING, "throttle","throttletimeframe") \
    X(2, MAX,        0x0,       0x0)

#define X(num, name, str, header) extern const uint8_t CONTRACT_ ## name;
    CONTRACT(X)
#undef X

extern const char *const mistral_contract_name[];
extern const char *const mistral_contract_header[];

#define SCOPE(X)           \
    X(0, LOCAL,  "local")  \
    X(1, GLOBAL, "global") \
    X(2, MAX,    0x0)

#define X(num, name, str) extern const uint8_t SCOPE_ ## name;
    SCOPE(X)
#undef X

extern const char *const mistral_scope_name[];

#define MEASUREMENT(X)                   \
    X(0, BANDWIDTH,     "bandwidth")     \
    X(1, COUNT,         "count")         \
    X(2, SEEK_DISTANCE, "seek-distance") \
    X(3, MIN_LATENCY,   "min-latency")   \
    X(4, MAX_LATENCY,   "max-latency")   \
    X(5, MEAN_LATENCY,  "mean-latency")  \
    X(6, TOTAL_LATENCY, "total-latency") \
    X(7, MAX,           0x0)

#define X(num, name, str) extern const uint8_t MEASUREMENT_ ## name;
    MEASUREMENT(X)
#undef X

extern const char *const mistral_measurement_name[];

#define UNIT_CLASS(X) \
    X(TIME,  "time")  \
    X(SIZE,  "size")  \
    X(COUNT, "count")

enum mistral_unit_class {
#define X(name, str) UNIT_CLASS_ ## name,
    UNIT_CLASS(X)
#undef X
    MAX_UNIT_CLASS
};

extern const char *const mistral_unit_class_name[];

#define UNIT(X)                                  \
    X(0, MICROSECS, "us", 1,   UNIT_CLASS_TIME)  \
    X(1, MILLISECS, "ms", 1e3, UNIT_CLASS_TIME)  \
    X(2, KILOBYTES, "kB", 1e3, UNIT_CLASS_SIZE)  \
    X(3, MEGABYTES, "MB", 1e6, UNIT_CLASS_SIZE)  \
    X(4, SECONDS,   "s",  1e6, UNIT_CLASS_TIME)  \
    X(5, BYTES,     "B",  1,   UNIT_CLASS_SIZE)  \
    X(6, THOUSAND,  "k",  1e3, UNIT_CLASS_COUNT) \
    X(7, MILLION,   "M",  1e6, UNIT_CLASS_COUNT) \
    X(8, COUNT,     "",   1,   UNIT_CLASS_COUNT) \
    X(9, MAX,       0x0,  0,   MAX_UNIT_CLASS)

#define X(num, name, suffix, scale, type) extern const uint8_t UNIT_ ## name;
    UNIT(X)
#undef X

extern const char *const mistral_unit_suffix[];
extern const uint32_t mistral_unit_scale[];
extern const uint32_t mistral_unit_type[];

#define CALL_TYPE(X)            \
    X(0,  1u << 0,  ACCEPT,   "accept")   \
    X(1,  1u << 1,  ACCESS,   "access")   \
    X(2,  1u << 2,  CONNECT,  "connect")  \
    X(3,  1u << 3,  CREATE,   "create")   \
    X(4,  1u << 4,  DELETE,   "delete")   \
    X(5,  1u << 5,  FSCHANGE, "fschange") \
    X(6,  1u << 6,  GLOB,     "glob")     \
    X(7,  1u << 7,  OPEN,     "open")     \
    X(8,  1u << 8,  READ,     "read")     \
    X(9,  1u << 9,  SEEK,     "seek")     \
    X(10, 1u << 10, WRITE,    "write")    \
    X(11, 1u << 11, MAX,      0x0)

/* The following definition is needed for declarations and should be kept in
 * sync with the number of entries in the CALL_TYPE() definition above.
 */
#define NUM_CALL_TYPES 11
#define MAX_CALL_TYPE_BITMASK (1u << NUM_CALL_TYPES)

#define X(num, mask, name, str) extern const uint8_t CALL_TYPE_ ## name;
    CALL_TYPE(X)
#undef X

extern const char *const mistral_call_type_name[];
extern const uint32_t mistral_call_type_mask[];
extern const char mistral_call_type_names[MAX_CALL_TYPE_BITMASK][
#define X(num, mask, name, str) + sizeof(str) + 1
    CALL_TYPE(X)
#undef X
];

typedef struct mistral_plugin {
    uint64_t interval;
    uint8_t type;
    FILE *error_log;
} mistral_plugin;

typedef struct mistral_log {
    struct mistral_log *forward;
    struct mistral_log *backward;
    uint8_t contract_type;
    uint8_t scope;
    struct tm time;
    struct timespec epoch;
    const char *label;
    const char *path;
    uint32_t call_type_mask;
    bool call_types[NUM_CALL_TYPES];
    ssize_t size_min;
    uint8_t size_min_unit;
    ssize_t size_max;
    uint8_t size_max_unit;
    uint8_t measurement;
    uint64_t threshold;
    uint8_t threshold_unit;
    uint64_t timeframe;
    uint8_t timeframe_unit;
    uint64_t measured;
    uint8_t measured_unit;
    uint64_t measured_time;
    uint8_t measured_time_unit;
    int64_t pid;
    const char *command;
    const char *file;
    const char *job_group_id;
    const char *job_id;
} mistral_log;

typedef struct mistral_header {
    uint32_t contract_version;
    uint8_t contract_type;
    uint64_t timeframe;
    uint8_t timeframe_unit;
} mistral_header;

#define MISTRAL_HEADER_INITIALIZER {.contract_type = CONTRACT_MAX, \
                                    .timeframe_unit = UNIT_MAX}

typedef struct mistral_rule {
    char *label;
    char *path;
    uint32_t call_types;
    ssize_t size_min;
    uint8_t size_min_unit;
    ssize_t size_max;
    uint8_t size_max_unit;
    uint8_t measurement;
    uint64_t threshold;
    uint8_t threshold_unit;
} mistral_rule;

#define MISTRAL_RULE_INITIALIZER {.size_min_unit = UNIT_MAX, \
                                  .size_max_unit = UNIT_MAX, \
                                  .measurement = MEASURMENT_MAX, \
                                  .threshold_unit = MAX_UNIT}

extern const uint64_t mistral_max_size;   /* Holds max value of ssize_t as   */
                                          /* defined in plugin_control.o     */

extern bool mistral_shutdown;       /* If set to true will cause the plugin  */
                                    /* to exit before reading the next line  */
                                    /* of input.                             */

extern char *mistral_call_type_str(uint32_t call_types);
extern void mistral_destroy_log_entry(mistral_log *log_entry);
extern int mistral_err(const char *format, ...);

#define UNUSED(param) ((void)(param))

/* Prototypes for stubs that exist within the plug-in framework */
void mistral_startup(mistral_plugin *plugin, int argc, char *argv[]);
void mistral_received_interval(mistral_plugin *plugin) __attribute__((weak));
void mistral_received_data_start(uint64_t block_num) __attribute__((weak));
void mistral_received_data_end(uint64_t block_num) __attribute__((weak));
void mistral_received_shutdown(void) __attribute__((weak));
void mistral_received_log(mistral_log *log_entry) __attribute__((weak));
void mistral_received_bad_log(const char *log_line) __attribute__((weak));
void mistral_exit(void) __attribute__((weak));

#endif
