#pragma once
#include <Arduino.h>

struct UsageData {
    float session_pct;       // 5-hour window utilization (0-100)
    int session_reset_mins;  // minutes until session resets
    float weekly_pct;        // 7-day window utilization (0-100)
    int weekly_reset_mins;   // minutes until weekly resets
    char status[16];         // "allowed" or "limited"
    int time_of_day;         // host local time, seconds since midnight (-1 = unknown)
    char date_str[24];       // host local date, e.g. "Sat, 17 May 2026" ("" = unknown)
    bool ok;                 // data parse succeeded
    bool valid;              // false until first successful parse
};
