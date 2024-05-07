//
// Created by timothy on 5/4/24.
//

#ifndef REDIS_UTILS_H
#define REDIS_UTILS_H

#endif //REDIS_UTILS_H

#include <time.h>
#include <stdio.h>

long get_current_time_ms();
long convert_exp_time_to_timestamp(long);
int get_size_of_resp_simple(const char *);
int get_size_of_resp_command(const char *);
