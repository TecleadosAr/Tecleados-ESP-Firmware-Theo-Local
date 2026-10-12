#include "kb_macro.h"
#include <string.h>

static kb_report_blocked_stats_t s_blocked_stats;

void kb_macro_track_blocked_time(int64_t blocked_us, int retries, esp_err_t err) {
    (void)retries;
    s_blocked_stats.total_reports++;
    if (err != ESP_OK) {
        s_blocked_stats.failed_reports++;
    }
    s_blocked_stats.total_blocked_us += blocked_us;
    if (blocked_us > s_blocked_stats.max_blocked_us) {
        s_blocked_stats.max_blocked_us = blocked_us;
    }
    if (blocked_us < 100) {
        s_blocked_stats.buckets[0]++;
    } else if (blocked_us < 1000) {
        s_blocked_stats.buckets[1]++;
    } else if (blocked_us < 5000) {
        s_blocked_stats.buckets[2]++;
    } else if (blocked_us < 20000) {
        s_blocked_stats.buckets[3]++;
    } else if (blocked_us < 50000) {
        s_blocked_stats.buckets[4]++;
    } else if (blocked_us < 100000) {
        s_blocked_stats.buckets[5]++;
    } else {
        s_blocked_stats.buckets[6]++;
    }
}

void kb_macro_get_blocked_stats(kb_report_blocked_stats_t *out_stats) {
    if (out_stats) {
        *out_stats = s_blocked_stats;
    }
}

void kb_macro_reset_blocked_stats(void) {
    memset(&s_blocked_stats, 0, sizeof(s_blocked_stats));
}
