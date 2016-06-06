/* The following file lists all the publicly accessable data and interfaces
 * needed when writing a plug-in for Mistral
 */

#ifndef MISTRAL_PLUGIN_H
#define MISTRAL_PLUGIN_H

#include <stdbool.h>
#include <stdint.h>
#include <sys/types.h>
#include <time.h>

/* Define the valid plug-in types */
#define PLUGIN(X)  \
    X(OUTPUT)      \
    X(UPDATE)

/* And construct an enum that contains an entry for each plug-in type */
enum mistral_plugin_type {
#define X(V) V ## _PLUGIN,
    PLUGIN(X)
#undef X
    MAX_PLUGIN
};

#define CONTRACTS(X)                              \
    X(MONITORING, "monitor", "monitortimeframe")  \
    X(THROTTLING, "throttle","throttletimeframe")

enum mistral_contract {
#define X(name, str, header) CONTRACT_ ## name,
    CONTRACTS(X)
#undef X
    MAX_CONTRACT
};

extern const char *const mistral_contract_name[];
extern const char *const mistral_contract_header[];

#define SCOPE(X)       \
    X(LOCAL,  "local") \
    X(GLOBAL, "global")

enum mistral_scope {
#define X(name, str) SCOPE_ ## name,
    SCOPE(X)
#undef X
    MAX_SCOPE
};

extern const char *const mistral_scope_name[];

#define MEASUREMENTS(X)               \
    X(BANDWIDTH,     "bandwidth")     \
    X(COUNT,         "count")         \
    X(SEEK_DISTANCE, "seek-distance") \
    X(MIN_LATENCY,   "min-latency")   \
    X(MAX_LATENCY,   "max-latency")   \
    X(MEAN_LATENCY,  "mean-latency")  \
    X(TOTAL_LATENCY, "total-latency")

enum mistral_measurement {
#define X(name, str) MEASUREMENT_ ## name,
    MEASUREMENTS(X)
#undef X
    MAX_MEASUREMENT
};

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

#define UNITS(X)                              \
    X(MICROSECS, "us", 1,   UNIT_CLASS_TIME)  \
    X(MILLISECS, "ms", 1e3, UNIT_CLASS_TIME)  \
    X(KILOBYTES, "kB", 1e3, UNIT_CLASS_SIZE)  \
    X(MEGABYTES, "MB", 1e6, UNIT_CLASS_SIZE)  \
    X(SECONDS,   "s",  1e6, UNIT_CLASS_TIME)  \
    X(BYTES,     "B",  1,   UNIT_CLASS_SIZE)  \
    X(THOUSAND,  "k",  1e3, UNIT_CLASS_COUNT) \
    X(MILLION,   "M",  1e6, UNIT_CLASS_COUNT) \
    X(COUNT,     "",   1,   UNIT_CLASS_COUNT)

enum mistral_unit {
#define X(name, suffix, scale, type) UNIT_ ## name,
    UNITS(X)
#undef X
    MAX_UNIT
};

extern const char *const mistral_unit_suffix[];
extern const uint32_t mistral_unit_scale[];
extern const uint32_t mistral_unit_type[];

#define CALL_TYPES(X)       \
    X(ACCEPT,   "accept")   \
    X(ACCESS,   "access")   \
    X(CONNECT,  "connect")  \
    X(CREATE,   "create")   \
    X(DELETE,   "delete")   \
    X(FSCHANGE, "fschange") \
    X(GLOB,     "glob")     \
    X(OPEN,     "open")     \
    X(READ,     "read")     \
    X(SEEK,     "seek")     \
    X(WRITE,    "write")

enum mistral_call_type {
#define X(name, str) CALL_TYPE_ ## name,
    CALL_TYPES(X)
#undef X
    MAX_CALL_TYPE
};

enum mistral_call_type_bitmask {
    CALL_BITMASK_NONE = 0u,
#define X(name, str) CALL_BITMASK_ ## name = 1u << (CALL_TYPE_ ## name),
    CALL_TYPES(X)
#undef X
};

extern const char *const mistral_call_type_name[];
extern const uint32_t mistral_call_type_mask[];
extern const char mistral_call_type_names[1u << MAX_CALL_TYPE][0
#define X(name, str) + sizeof(str) + 1
    CALL_TYPES(X)
#undef X
];

typedef struct mistral_plugin {
    uint64_t interval;
    enum mistral_plugin_type type;
    FILE *error_log;
} mistral_plugin;

typedef struct mistral_log {
    struct mistral_log *forward;
    struct mistral_log *backward;
    enum mistral_contract contract;
    enum mistral_scope scope;
    struct tm time;
    time_t epoch;
    const char *label;
    const char *path;
    uint32_t call_type_mask;
    bool call_types[MAX_CALL_TYPE];
    ssize_t size_min;
    enum mistral_unit size_min_unit;
    ssize_t size_max;
    enum mistral_unit size_max_unit;
    enum mistral_measurement measurement;
    uint64_t threshold;
    enum mistral_unit threshold_unit;
    uint64_t timeframe;
    enum mistral_unit timeframe_unit;
    uint64_t measured;
    enum mistral_unit measured_unit;
    uint64_t measured_time;
    enum mistral_unit measured_time_unit;
    int64_t pid;
    const char *command;
    const char *file;
    const char *job_group_id;
    const char *job_id;
} mistral_log;

typedef struct mistral_header {
    uint32_t contract_version;
    enum mistral_contract contract_type;
    uint64_t timeframe;
    enum mistral_unit timeframe_unit;
} mistral_header;

#define MISTRAL_HEADER_INITIALIZER {0, MAX_CONTRACT, 0, MAX_UNIT}

typedef struct mistral_rule {
    char *label;
    char *path;
    uint32_t call_types;
    ssize_t size_min;
    enum mistral_unit size_min_unit;
    ssize_t size_max;
    enum mistral_unit size_max_unit;
    enum mistral_measurement measurement;
    uint64_t threshold;
    enum mistral_unit threshold_unit;
} mistral_rule;

#define MISTRAL_RULE_INITIALIZER {0, 0, 0, 0, MAX_UNIT, 0, MAX_UNIT, \
                                  MAX_MEASURMENT, 0, MAX_UNIT}

extern uint64_t mistral_max_size;   /* Holds max value of ssize_t as defined */
                                    /* in plugin_control.o                   */

extern bool mistral_shutdown;       /* If set to true will cause the plugin  */
                                    /* to exit before reading the next line  */
                                    /* of input.                             */

extern char *mistral_call_type_str(uint32_t call_types);
extern void mistral_destroy_log_entry(mistral_log *log_entry);
extern int mistral_err(const char *format, ...);

#define UNUSED(param) ((void)(param))

/* Prototypes for stubs that exist within the plug-in framework */
void mistral_startup(mistral_plugin *plugin, int argc, char *argv[]) __attribute__((weak));
void mistral_received_interval(mistral_plugin *plugin) __attribute__((weak));
void mistral_received_data_start(uint64_t block_num) __attribute__((weak));
void mistral_received_data_end(uint64_t block_num) __attribute__((weak));
void mistral_received_shutdown(void) __attribute__((weak));
void mistral_received_log(mistral_log *log_entry) __attribute__((weak));
void mistral_received_bad_log(char *log_line) __attribute__((weak));
void mistral_exit(void) __attribute__((weak));

#endif
