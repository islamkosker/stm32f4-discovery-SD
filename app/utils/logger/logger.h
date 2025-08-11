#ifndef LOGGER_H
#define LOGGER_H

#define DEVELOPMENT

#include <zephyr/logging/log.h>
#include <zephyr/logging/log_output.h>
#include <zephyr/kernel.h>
#include <zephyr/kernel/thread.h>

#ifdef DEVELOPMENT
#define APP_LOG_LEVEL LOG_LEVEL_DBG
#define __FILENAME__ (strrchr(__FILE__, '/') ? strrchr(__FILE__, '/') + 1 : __FILE__)
#define LOG_INFO(message, ...) LOG_INF("INFO [%s:%d] " message, __FILENAME__, __LINE__, ##__VA_ARGS__)
#define LOG_WARNING(message, ...) LOG_WRN("WARNING [%s:%d] " message, __FILENAME__, __LINE__, ##__VA_ARGS__)
#define LOG_ERROR(message, ...) LOG_ERR("ERROR [%s:%d] " message, __FILENAME__, __LINE__, ##__VA_ARGS__)
#define LOG_DEBUG(message, ...) LOG_DBG("DEBUG [%s:%d] " message, __FILENAME__, __LINE__, ##__VA_ARGS__)
#elif (PRODUCTION)
LOG_MODULE_DECLARE(app, LOG_LEVEL_INF);
#define LOG_INFO(message, ...) LOG_INF("INFO [%s:%d] " message, __FILENAME__, __LINE__, ##__VA_ARGS__)
#endif

#define LOG_STACK_INFO() do { \
    size_t unused; \
    int stack_err = k_thread_stack_space_get(k_current_get(), &unused); \
    if (stack_err == 0) { \
        const struct k_thread *thread = k_current_get(); \
        size_t total_size = thread->stack_info.size; \
        LOG_WRN("STACK INFO [%s:%d] stack_unused: %u.%02u KB, stack_total: %u.%02u KB, stack_used: %u.%02u KB", \
                __FILENAME__, __LINE__, \
                unused/1024, ((unused%1024)*100)/1024, \
                total_size/1024, ((total_size%1024)*100)/1024, \
                (total_size - unused)/1024, (((total_size - unused)%1024)*100)/1024); \
    } \
} while(0)

#endif // LOGGER_H

