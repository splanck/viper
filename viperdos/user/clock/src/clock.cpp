//===----------------------------------------------------------------------===//
// Clock time implementation
//===----------------------------------------------------------------------===//

#include "../include/clock.hpp"
#include <stdio.h>
#include <time.h>

namespace clockapp {

void getCurrentTime(Time &time) {
    // Get current time using standard C library
    time_t now = ::time(nullptr);
    struct tm *tm_info = localtime(&now);

    if (tm_info) {
        time.hours = tm_info->tm_hour;
        time.minutes = tm_info->tm_min;
        time.seconds = tm_info->tm_sec;
        time.day = tm_info->tm_mday;
        time.month = tm_info->tm_mon + 1;
        time.year = tm_info->tm_year + 1900;
    } else {
        // Fallback to midnight
        time.hours = 0;
        time.minutes = 0;
        time.seconds = 0;
        time.day = 1;
        time.month = 1;
        time.year = 2024;
    }
}

void formatTime12(const Time &time, char *buf, int bufSize) {
    int hour12 = time.hours % 12;
    if (hour12 == 0) {
        hour12 = 12;
    }
    const char *ampm = (time.hours < 12) ? "AM" : "PM";
    snprintf(buf, bufSize, "%2d:%02d:%02d %s", hour12, time.minutes, time.seconds, ampm);
}

void formatTime24(const Time &time, char *buf, int bufSize) {
    snprintf(buf, bufSize, "%02d:%02d:%02d", time.hours, time.minutes, time.seconds);
}

void formatDate(const Time &time, char *buf, int bufSize) {
    const char *months[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                            "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    int monthIdx = time.month - 1;
    if (monthIdx < 0 || monthIdx > 11) {
        monthIdx = 0;
    }
    snprintf(buf, bufSize, "%s %d, %d", months[monthIdx], time.day, time.year);
}

int hourHandAngle(const Time &time) {
    // 360 degrees / 12 hours = 30 degrees per hour
    // Plus additional movement based on minutes
    int angle = (time.hours % 12) * 30 + time.minutes / 2;
    return angle;
}

int minuteHandAngle(const Time &time) {
    // 360 degrees / 60 minutes = 6 degrees per minute
    int angle = time.minutes * 6 + time.seconds / 10;
    return angle;
}

int secondHandAngle(const Time &time) {
    // 360 degrees / 60 seconds = 6 degrees per second
    return time.seconds * 6;
}

} // namespace clockapp
