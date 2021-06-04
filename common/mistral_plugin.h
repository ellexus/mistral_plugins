/* This file lists all the publicly accessible data and interfaces needed when
 * writing a plug-in for Mistral
 */

#ifndef MISTRAL_PLUGIN_H
#define MISTRAL_PLUGIN_H

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/types.h>
#include <time.h>
#include <semaphore.h>

/* Define the valid plug-in types */
#define PLUGIN(X) \
    X(OUTPUT)     \
    X(UPDATE)

enum __attribute__((packed)) mistral_plugin_type {
    #define X(name) name ## _PLUGIN,
    PLUGIN(X)
    #undef X
    MAX_PLUGIN
};

#define CONTRACT(X)                               \
    X(MONITORING, "monitor",  "monitortimeframe") \
    X(THROTTLING, "throttle", "throttletimeframe")

enum __attribute__((packed)) mistral_contract {
    #define X(name, str, header) CONTRACT_ ## name,
    CONTRACT(X)
    #undef X
    CONTRACT_MAX
};

extern const char * const mistral_contract_name[];
extern const char * const mistral_contract_header[];

#define SCOPE(X)       \
    X(LOCAL,  "local") \
    X(GLOBAL, "global")

enum __attribute__((packed)) mistral_scope {
    #define X(name, str) SCOPE_ ## name,
    SCOPE(X)
    #undef X
    SCOPE_MAX
};

extern const char * const mistral_scope_name[];

#define MEASUREMENT(X)                       \
    X(BANDWIDTH,     "bandwidth")            \
    X(COUNT,         "count")                \
    X(SEEK_DISTANCE, "seek-distance")        \
    X(MIN_LATENCY,   "min-latency")          \
    X(MAX_LATENCY,   "max-latency")          \
    X(MEAN_LATENCY,  "mean-latency")         \
    X(TOTAL_LATENCY, "total-latency")        \
    X(MEMORY,        "memory")               \
    X(MEMORY_RSS,    "memory-rss")           \
    X(MEMORY_VSIZE,  "memory-vsize")         \
    X(USER_TIME,     "user-time")            \
    X(SYSTEM_TIME,   "system-time")          \
    X(CPU_TIME,      "cpu-time")             \
    X(HOST_USER,     "host-cpu-user-time")   \
    X(HOST_SYSTEM,   "host-cpu-system-time") \
    X(HOST_IOWAIT,   "host-cpu-iowait-time")

enum __attribute__((packed)) mistral_measurement {
    #define X(name, str) MEASUREMENT_ ## name,
    MEASUREMENT(X)
    #undef X
    MEASUREMENT_MAX
};

extern const char * const mistral_measurement_name[];

#define UNIT_CLASS(X) \
    X(TIME,  "time")  \
    X(SIZE,  "size")  \
    X(COUNT, "count")

enum __attribute__((packed)) mistral_unit_class {
    #define X(name, str) UNIT_CLASS_ ## name,
    UNIT_CLASS(X)
    #undef X
    UNIT_CLASS_MAX
};

extern const char * const mistral_unit_class_name[];

#define UNIT(X)                               \
    X(MICROSECS, "us", 1,   UNIT_CLASS_TIME)  \
    X(MILLISECS, "ms", 1e3, UNIT_CLASS_TIME)  \
    X(KILOBYTES, "kB", 1e3, UNIT_CLASS_SIZE)  \
    X(MEGABYTES, "MB", 1e6, UNIT_CLASS_SIZE)  \
    X(GIGABYTES, "GB", 1e9, UNIT_CLASS_SIZE)  \
    X(BYTES,     "B",  1,   UNIT_CLASS_SIZE)  \
    X(SECONDS,   "s",  1e6, UNIT_CLASS_TIME)  \
    X(THOUSAND,  "k",  1e3, UNIT_CLASS_COUNT) \
    X(MILLION,   "M",  1e6, UNIT_CLASS_COUNT) \
    X(COUNT,     "",   1,   UNIT_CLASS_COUNT)

enum __attribute__((packed)) mistral_unit {
    #define X(name, suffix, scale, type) UNIT_ ## name,
    UNIT(X)
    #undef X
    UNIT_MAX
};

extern const char * const mistral_unit_suffix[];
extern const uint32_t mistral_unit_scale[];
extern const uint32_t mistral_unit_type[];

#define CALL_TYPE(X)                \
    X(ACCEPT,       "accept")       \
    X(ACCESS,       "access")       \
    X(CONNECT,      "connect")      \
    X(CREATE,       "create")       \
    X(DELETE,       "delete")       \
    X(FSCHANGE,     "fschange")     \
    X(GLOB,         "glob")         \
    X(MPI_ACCESS,   "mpi_access")   \
    X(MPI_CREATE,   "mpi_create")   \
    X(MPI_DELETE,   "mpi_delete")   \
    X(MPI_FSCHANGE, "mpi_fschange") \
    X(MPI_OPEN,     "mpi_open")     \
    X(MPI_READ,     "mpi_read")     \
    X(MPI_SEEK,     "mpi_seek")     \
    X(MPI_SYNC,     "mpi_sync")     \
    X(MPI_WRITE,    "mpi_write")    \
    X(NONE,         "none")         \
    X(OPEN,         "open")         \
    X(READ,         "read")         \
    X(SEEK,         "seek")         \
    X(WRITE,        "write")        \
    X(MMAP,         "mmap")

#define BITMASK(type) (1u << type)

enum __attribute__((packed)) mistral_call_type {
    #define X(name, str) CALL_TYPE_ ## name,
    CALL_TYPE(X)
    #undef X
    CALL_TYPE_MAX
};

enum __attribute__((packed)) mistral_bitmask {
    #define X(name, str) CALL_TYPE_MASK_ ## name = BITMASK(CALL_TYPE_ ## name),
    CALL_TYPE(X)
    #undef X
    CALL_TYPE_MASK_MAX = BITMASK(CALL_TYPE_MAX)
};

extern const char * const mistral_call_type_name[];
extern const uint32_t mistral_call_type_mask[];
extern char mistral_call_type_names[CALL_TYPE_MASK_MAX][
    #define X(name, str) + sizeof(str) + 1
    CALL_TYPE(X)
#undef X
];

typedef struct mistral_plugin {
    sem_t lock;
    uint64_t interval;
    enum mistral_plugin_type type;
    FILE *error_log;
    char *error_log_name;
    mode_t error_log_mode;
    uint32_t flags;
} mistral_plugin;

#define PLUGIN_ERRLOG_INIT    1

typedef struct mistral_log {
    struct mistral_log *forward;
    struct mistral_log *backward;
    enum mistral_contract contract_type;
    enum mistral_scope scope;
    struct tm time;
    struct timespec epoch;
    uint32_t microseconds;
    const char *label;
    const char *path;
    const char *fstype;
    const char *fsname;
    const char *fshost;
    uint32_t call_type_mask;
    bool call_types[CALL_TYPE_MAX];
    const char *call_type_names;
    const char *size_range;
    int64_t size_min;
    enum mistral_unit size_min_unit;
    int64_t size_max;
    enum mistral_unit size_max_unit;
    enum mistral_measurement measurement;
    const char *threshold_str;
    uint64_t threshold;
    enum mistral_unit threshold_unit;
    uint64_t timeframe;
    enum mistral_unit timeframe_unit;
    const char *measured_str;
    uint64_t measured;
    enum mistral_unit measured_unit;
    uint64_t measured_time;
    enum mistral_unit measured_time_unit;
    int64_t pid;
    const char *command;
    const char *file;
    const char *job_group_id;
    const char *job_id;
    const char *hostname;
    const char *full_hostname;
    uint32_t cpu;
    int32_t mpi_rank;
    int64_t sequence;
} mistral_log;

typedef struct mistral_header {
    uint32_t contract_version;
    enum mistral_contract contract_type;
    uint64_t timeframe;
    enum mistral_unit timeframe_unit;
} mistral_header;

#define MISTRAL_HEADER_INITIALIZER {.contract_type = CONTRACT_MAX, \
                                    .timeframe_unit = UNIT_MAX}

typedef struct mistral_rule {
    char *label;
    char *path;
    uint32_t call_types;
    int64_t size_min;
    enum mistral_unit size_min_unit;
    int64_t size_max;
    enum mistral_unit size_max_unit;
    enum mistral_measurement measurement;
    uint64_t threshold;
    enum mistral_unit threshold_unit;
} mistral_rule;

#define MISTRAL_RULE_INITIALIZER {.size_min_unit = UNIT_MAX,      \
                                  .size_max_unit = UNIT_MAX,      \
                                  .measurement = MEASUREMENT_MAX, \
                                  .threshold_unit = MAX_UNIT}

extern const int64_t mistral_max_size;    /* Holds max value of ssize_t as
                                           * defined in plugin_control.o
                                           */

extern void mistral_destroy_log_entry(mistral_log *log_entry);
__attribute__((__format__(printf, 1, 2)))
extern int mistral_err(const char *format, ...);
extern void mistral_shutdown(void); /* Function that, if called, will cause
                                     * the plug-in to exit before reading
                                     * the next line of input.
                                     */
extern const char *mistral_get_call_type_name(uint32_t mask);

#define UNUSED(param) ((void)(param))

/* Prototypes for stubs that exist within the plug-in framework */
void mistral_startup(mistral_plugin *plugin, int argc, char *argv[]);
void mistral_received_interval(mistral_plugin *plugin) __attribute__((weak));
void mistral_received_data_start(uint64_t block_num, bool block_error) __attribute__((weak));
void mistral_received_data_end(uint64_t block_num, bool block_error) __attribute__((weak));
void mistral_received_shutdown(void) __attribute__((weak));
void mistral_received_log(mistral_log *log_entry) __attribute__((weak));
void mistral_received_bad_log(const char *log_line) __attribute__((weak));
void mistral_exit(void) __attribute__((weak));

#endif
