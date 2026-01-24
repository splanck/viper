#pragma once
//===----------------------------------------------------------------------===//
// Clock time management
//===----------------------------------------------------------------------===//

#include <stdint.h>

namespace clockapp {

// Time structure
struct Time {
    int hours;
    int minutes;
    int seconds;
    int day;
    int month;
    int year;
};

// Get current time from system uptime (simplified - no RTC)
void getCurrentTime(Time &time);

// Format time as string
void formatTime12(const Time &time, char *buf, int bufSize);
void formatTime24(const Time &time, char *buf, int bufSize);
void formatDate(const Time &time, char *buf, int bufSize);

// Calculate angle for clock hands (in degrees, 0 = 12 o'clock)
int hourHandAngle(const Time &time);
int minuteHandAngle(const Time &time);
int secondHandAngle(const Time &time);

} // namespace clockapp
