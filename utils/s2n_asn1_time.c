/*
 * Copyright 2017 Amazon.com, Inc. or its affiliates. All Rights Reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License").
 * You may not use this file except in compliance with the License.
 * A copy of the License is located at
 *
 *  http://aws.amazon.com/apache2.0
 *
 * or in the "license" file accompanying this file. This file is distributed
 * on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either
 * express or implied. See the License for the specific language governing
 * permissions and limitations under the License.
 */

#include "utils/s2n_asn1_time.h"
#include "s2n_safety.h"

#include <time.h>
#include <ctype.h>

typedef enum parser_state {
    ON_YEAR_DIGIT_1 = 0,
    ON_YEAR_DIGIT_2,
    ON_YEAR_DIGIT_3,
    ON_YEAR_DIGIT_4,
    ON_MONTH_DIGIT_1,
    ON_MONTH_DIGIT_2,
    ON_DAY_DIGIT_1,
    ON_DAY_DIGIT_2,
    ON_HOUR_DIGIT_1,
    ON_HOUR_DIGIT_2,
    ON_MINUTE_DIGIT_1,
    ON_MINUTE_DIGIT_2,
    ON_SECOND_DIGIT_1,
    ON_SECOND_DIGIT_2,
    ON_SUBSECOND,
    ON_TIMEZONE,
    ON_OFFSET_HOURS_DIGIT_1,
    ON_OFFSET_HOURS_DIGIT_2,
    ON_OFFSET_MINUTES_DIGIT_1,
    ON_OFFSET_MINUTES_DIGIT_2,
    FINISHED,
    PARSE_ERROR
} parser_state;

static inline long get_gmt_offset(struct tm *time) {

#if defined(__USE_BSD) || defined(__APPLE__) && defined(__MACH__)
    return time->tm_gmtoff;
#else
    return time->__tm_gmtoff;
#endif
}

static inline void get_current_timesettings(long *gmt_offset, int *is_dst) {
    struct tm time_ptr;
    time_t raw_time;
    time(&raw_time);
    localtime_r(&raw_time, &time_ptr);
    *gmt_offset = get_gmt_offset(&time_ptr);
    *is_dst = time_ptr.tm_isdst;
}

struct parser_args {
    uint8_t offset_negative;
    uint8_t local_time_assumed;
    uint8_t current_digit;
    long offset_hours;
    long offset_minutes;
    struct tm time;
};

/* this is just a standard state machine for ASN1 date format... nothing special.
 * just do a character at a time and change the state per character encountered.
 * when finished the above time structure should be filled in along with some
 * crazy timezone info we'll need shortly afterwards.*/
static parser_state process_state(parser_state state, char current_char, struct parser_args *args) {
    switch (state) {
        case ON_YEAR_DIGIT_1:
            char_to_digit(current_char, args->current_digit);
            args->time.tm_year = args->current_digit;
            return ON_YEAR_DIGIT_2;
        case ON_YEAR_DIGIT_2:
            char_to_digit(current_char, args->current_digit);
            args->time.tm_year = args->time.tm_year * 10 + args->current_digit;
            return ON_YEAR_DIGIT_3;
        case ON_YEAR_DIGIT_3:
            char_to_digit(current_char, args->current_digit);
            args->time.tm_year = args->time.tm_year * 10 + args->current_digit;
            return ON_YEAR_DIGIT_4;
        case ON_YEAR_DIGIT_4:
            char_to_digit(current_char, args->current_digit);
            args->time.tm_year = args->time.tm_year * 10 + args->current_digit;
            args->time.tm_year -= 1900;
            if (args->time.tm_year < 0) {
                return PARSE_ERROR;
            }

            return ON_MONTH_DIGIT_1;
        case ON_MONTH_DIGIT_1:
            char_to_digit(current_char, args->current_digit);
            args->time.tm_mon = args->current_digit;
            return ON_MONTH_DIGIT_2;
        case ON_MONTH_DIGIT_2:
            char_to_digit(current_char, args->current_digit);
            args->time.tm_mon = args->time.tm_mon * 10 + args->current_digit;
            args->time.tm_mon -= 1;

            if (args->time.tm_mon < 0 || args->time.tm_mon > 11) {
                return PARSE_ERROR;
            }

            return ON_DAY_DIGIT_1;
        case ON_DAY_DIGIT_1:
            char_to_digit(current_char, args->current_digit);
            args->time.tm_mday = args->current_digit;
            return ON_DAY_DIGIT_2;
        case ON_DAY_DIGIT_2:
            char_to_digit(current_char, args->current_digit);
            args->time.tm_mday = args->time.tm_mday * 10 + args->current_digit;

            if (args->time.tm_mday < 0 || args->time.tm_mday > 31) {
                return PARSE_ERROR;
            }

            return ON_HOUR_DIGIT_1;
        case ON_HOUR_DIGIT_1:
            char_to_digit(current_char, args->current_digit);
            args->time.tm_hour = args->current_digit;
            return ON_HOUR_DIGIT_2;
        case ON_HOUR_DIGIT_2:
            char_to_digit(current_char, args->current_digit);
            args->time.tm_hour = args->time.tm_hour * 10 + args->current_digit;

            if (args->time.tm_hour < 0 || args->time.tm_hour > 23) {
                return PARSE_ERROR;
            }

            return ON_MINUTE_DIGIT_1;
        case ON_MINUTE_DIGIT_1:
            char_to_digit(current_char, args->current_digit);
            args->time.tm_min = args->current_digit;
            return ON_MINUTE_DIGIT_2;
        case ON_MINUTE_DIGIT_2:
            char_to_digit(current_char, args->current_digit);
            args->time.tm_min = args->time.tm_min * 10 + args->current_digit;

            if (args->time.tm_min < 0 || args->time.tm_min > 59) {
                return PARSE_ERROR;
            }

            return ON_SECOND_DIGIT_1;
        case ON_SECOND_DIGIT_1:
            char_to_digit(current_char, args->current_digit);
            args->time.tm_sec = args->current_digit;
            return ON_SECOND_DIGIT_2;
        case ON_SECOND_DIGIT_2:
            char_to_digit(current_char, args->current_digit);
            args->time.tm_sec = args->time.tm_sec * 10 + args->current_digit;

            if (args->time.tm_sec < 0 || args->time.tm_sec > 59) {
                return PARSE_ERROR;
            }

            return ON_SUBSECOND;
        case ON_SUBSECOND:
            if (current_char == '.' || isdigit(current_char)) {
                return ON_SUBSECOND;
            }
        /* this fallthrough is intentional */
        case ON_TIMEZONE:
            if (current_char == 'Z' || current_char == 'z') {
                args->local_time_assumed = 0;
                return FINISHED;
            } else if (current_char == '-') {
                args->local_time_assumed = 0;
                args->offset_negative = 1;
                return ON_OFFSET_HOURS_DIGIT_1;
            } else if (current_char == '+') {
                args->local_time_assumed = 0;
                args->offset_negative = 0;
                return ON_OFFSET_HOURS_DIGIT_1;
            }

            return PARSE_ERROR;
        case ON_OFFSET_HOURS_DIGIT_1:
            char_to_digit(current_char, args->current_digit);
            args->offset_hours = args->current_digit;
            return ON_OFFSET_HOURS_DIGIT_2;
        case ON_OFFSET_HOURS_DIGIT_2:
            char_to_digit(current_char, args->current_digit);
            args->offset_hours = args->offset_hours * 10 + args->current_digit;

            if (args->offset_hours < 0 || args->offset_hours > 23) {
                return PARSE_ERROR;
            }

            return ON_OFFSET_MINUTES_DIGIT_1;
        case ON_OFFSET_MINUTES_DIGIT_1:
            char_to_digit(current_char, args->current_digit);
            args->offset_minutes = args->current_digit;
            return ON_OFFSET_MINUTES_DIGIT_2;
        case ON_OFFSET_MINUTES_DIGIT_2:
            char_to_digit(current_char, args->current_digit);
            args->offset_minutes = args->offset_minutes * 10 + args->current_digit;

            if (args->offset_minutes < 0 || args->offset_minutes > 23) {
                return PARSE_ERROR;
            }

            return FINISHED;
        default:
            return PARSE_ERROR;
    }
}

int s2n_asn1_time_to_nano_since_epoch_ticks(const char *asn1_time, uint32_t len, uint64_t *ticks) {

    /* figure out if we are on something other than UTC since timegm is not supported everywhere. */
    long gmt_offset_current = 0;
    int is_dst = 0;
    get_current_timesettings(&gmt_offset_current, &is_dst);

    uint32_t str_len = len;
    parser_state state = ON_YEAR_DIGIT_1;

    struct parser_args args = {
        .time = {.tm_hour = 0, .tm_isdst = -1, .tm_mday = 0, .tm_min = 0, .tm_mon = 0,
                .tm_sec = 0, .tm_wday = 0, .tm_yday = 0, .tm_year = 0,
        },
            .current_digit = 0,
            .local_time_assumed = 1,
            .offset_hours = 0,
            .offset_minutes = 0,
            .offset_negative = 0
    };

    size_t current_pos = 0;

    while (state < FINISHED && current_pos < str_len) {
        char current_char = asn1_time[current_pos];
        state = process_state(state, current_char, &args);
        current_pos++;
    }

    /* state on subsecond means no timezone info was found and we assume local time */
    if (state == FINISHED || state == ON_SUBSECOND) {

        time_t clock_data = 0;
        /* ASN1 + and - is in format HHMM. We need to convert it to seconds for the adjustment */
        long gmt_offset = (args.offset_hours * 3600) + (args.offset_minutes * 60);

        if (args.offset_negative) {
            gmt_offset = 0 - gmt_offset;
        }

        clock_data = mktime(&args.time);

        /* if we detected UTC is being used (please always use UTC), we need to add the detected timezone on the local
         * machine back to the offset. Also, the offset includes an offset for daylight savings time. When the time being parsed
         * and the local time are on different sides of the dst barrier, the offset has to be adjusted to account for it. */
        if (!args.local_time_assumed) {
            gmt_offset -= gmt_offset_current;
            gmt_offset -= args.time.tm_isdst != is_dst ? (args.time.tm_isdst - is_dst) * 3600 : 0;
        }

        /* convert to nanoseconds and add the timezone offset. */
        if (clock_data > 0) {
            *ticks = ((uint64_t) clock_data - gmt_offset) * 1000000000;
            return 0;
        }
    }

    return -1;
}

