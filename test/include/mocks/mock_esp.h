/**
 * @file mock_esp.h
 * @brief Mock ESP-IDF base types and logging — replaces esp_err.h, esp_log.h,
 *        esp_timer.h, esp_heap_caps.h for host testing.
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* ---- esp_err.h ---- */

typedef int esp_err_t;

#define ESP_OK              0
#define ESP_FAIL           -1
#define ESP_ERR_NO_MEM      0x101
#define ESP_ERR_INVALID_ARG 0x102
#define ESP_ERR_INVALID_SIZE 0x104
#define ESP_ERR_NOT_FOUND       0x105
#define ESP_ERR_NOT_SUPPORTED   0x106
#define ESP_ERR_TIMEOUT         0x107
#define ESP_ERR_INVALID_RESPONSE 0x108
#define ESP_ERR_INVALID_CRC     0x109
#define ESP_ERR_INVALID_VERSION 0x10A
#define ESP_ERR_INVALID_MAC     0x10B
#define ESP_ERR_NOT_ALLOWED     0x10C
#define ESP_ERR_INVALID_STATE   0x103

#define ESP_ERR_NVS_NO_FREE_PAGES 0x1100
#define ESP_ERR_NVS_NEW_VERSION_FOUND 0x1101
#define ESP_ERR_NVS_NOT_FOUND 0x1102
#define ESP_ERR_NVS_INVALID_LENGTH 0x110d

/* ---- esp_log.h ---- */

typedef enum {
    ESP_LOG_NONE = 0,
    ESP_LOG_ERROR,
    ESP_LOG_WARN,
    ESP_LOG_INFO,
    ESP_LOG_DEBUG,
    ESP_LOG_VERBOSE
} esp_log_level_t;

#define ESP_LOGE(tag, fmt, ...) (void)0
#define ESP_LOGW(tag, fmt, ...) (void)0
#define ESP_LOGI(tag, fmt, ...) (void)0
#define ESP_LOGD(tag, fmt, ...) (void)0
#define ESP_LOGV(tag, fmt, ...) (void)0

static inline const char *esp_err_to_name(esp_err_t code) {
    (void)code;
    return "ESP_OK";
}

/* ---- esp_timer.h ---- */

static int64_t _mock_timer_us = 0;

static inline int64_t esp_timer_get_time(void) {
    return _mock_timer_us;
}

static inline void mock_timer_set(int64_t us) {
    _mock_timer_us = us;
}

static inline void mock_timer_advance(int64_t delta_us) {
    _mock_timer_us += delta_us;
}

typedef struct esp_timer {
    void (*callback)(void *arg);
    void *arg;
    int64_t fire_time_us;
    bool active;
    bool periodic;
    uint64_t period_us;
} *esp_timer_handle_t;

typedef struct {
    void (*callback)(void *arg);
    void *arg;
    const char *name;
    bool skip_unhandled_events;
} esp_timer_create_args_t;

static inline esp_err_t esp_timer_create(const esp_timer_create_args_t *args, esp_timer_handle_t *out_handle) {
    if (!args || !out_handle) return ESP_ERR_INVALID_ARG;
    esp_timer_handle_t t = (esp_timer_handle_t)calloc(1, sizeof(struct esp_timer));
    t->callback = args->callback;
    t->arg = args->arg;
    t->active = false;
    *out_handle = t;
    return ESP_OK;
}

static inline esp_err_t esp_timer_start_once(esp_timer_handle_t timer, uint64_t timeout_us) {
    if (!timer) return ESP_ERR_INVALID_ARG;
    timer->active = true;
    timer->periodic = false;
    timer->fire_time_us = _mock_timer_us + timeout_us;
    return ESP_OK;
}

static inline esp_err_t esp_timer_stop(esp_timer_handle_t timer) {
    if (!timer) return ESP_ERR_INVALID_ARG;
    timer->active = false;
    return ESP_OK;
}

static inline bool esp_timer_is_active(esp_timer_handle_t timer) {
    return timer ? timer->active : false;
}

static inline esp_err_t esp_timer_delete(esp_timer_handle_t timer) {
    if (!timer) return ESP_ERR_INVALID_ARG;
    free(timer);
    return ESP_OK;
}

/* ---- esp_heap_caps.h ---- */

#define MALLOC_CAP_SPIRAM   (1 << 0)
#define MALLOC_CAP_INTERNAL (1 << 1)
#define MALLOC_CAP_8BIT     (1 << 2)

static inline void *heap_caps_malloc(size_t size, uint32_t caps) {
    (void)caps;
    return malloc(size);
}

/* ---- strlcpy (not available on all platforms) ----
 * glibc >= 2.38 and the BSD/macOS libcs already declare it in <string.h>. */

#if !defined(strlcpy) && !(defined(__GLIBC__) && (__GLIBC__ > 2 || (__GLIBC__ == 2 && __GLIBC_MINOR__ >= 38))) \
    && !defined(__APPLE__) && !defined(__FreeBSD__) && !defined(__OpenBSD__) && !defined(__NetBSD__)
static inline size_t strlcpy(char *dst, const char *src, size_t siz) {
    size_t len = strlen(src);
    if (siz > 0) {
        size_t cp = len < siz - 1 ? len : siz - 1;
        memcpy(dst, src, cp);
        dst[cp] = '\0';
    }
    return len;
}
#endif
