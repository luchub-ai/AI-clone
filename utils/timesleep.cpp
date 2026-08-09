#include "timesleep.h"

// Hàm sleep nhận vào số mili-giây (milliseconds)
void time_sleep(int milliseconds) {
    if (milliseconds <= 0) return;
    std::this_thread::sleep_for(std::chrono::milliseconds(milliseconds));
}