#include <sys/time.h>
#include "osm.h"
#include <iostream>
#include <math.h>

const int ERROR_CODE = -1;
const int NUM_PER_ITERATION = 10;


double calculate_time(const timeval& start, const timeval& end, unsigned int iterations)
{
    double time = 1000L * ((((double) end.tv_sec - (double) start.tv_sec) * 1000000L) +
            ((double) end.tv_usec - (double) start.tv_usec));
    double rounding = (ceil((double) iterations / NUM_PER_ITERATION)) * NUM_PER_ITERATION;
    return time / rounding;
}

void do_nothing(){}


/* Time measurement function for a simple arithmetic operation.
   returns time in nano-seconds upon success,
   and -1 upon failure.
   */
double osm_operation_time(unsigned int iterations)
{
    if (iterations == 0)
    {
        return ERROR_CODE;
    }
    int a1, a2, a3, a4, a5, a6, a7, a8, a9, a10;
    timeval start, end;
    gettimeofday(&start, nullptr);
    for (unsigned int i = 0; i < iterations; i += NUM_PER_ITERATION)
    {
        a1++;
        a2++;
        a3++;
        a4++;
        a5++;
        a6++;
        a7++;
        a8++;
        a9++;
        a10++;
    }
    gettimeofday(&end, nullptr);
    return calculate_time(start, end, iterations);
}


/* Time measurement function for an empty function call.
   returns time in nano-seconds upon success,
   and -1 upon failure.
   */
double osm_function_time(unsigned int iterations)
{
    if (iterations == 0)
    {
        return ERROR_CODE;
    }
    timeval start, end;
    gettimeofday(&start, nullptr);
    for (unsigned int i = 0; i < iterations; i += NUM_PER_ITERATION)
    {
        do_nothing();
        do_nothing();
        do_nothing();
        do_nothing();
        do_nothing();
        do_nothing();
        do_nothing();
        do_nothing();
        do_nothing();
        do_nothing();
    }
    gettimeofday(&end, nullptr);
    return calculate_time(start, end, iterations);
}


/* Time measurement function for an empty trap into the operating system.
   returns time in nano-seconds upon success,
   and -1 upon failure.
   */
double osm_syscall_time(unsigned int iterations)
{
    if (iterations == 0)
    {
        return ERROR_CODE;
    }
    timeval start, end;
    gettimeofday(&start, nullptr);
    for (unsigned int i = 0; i < iterations; i += NUM_PER_ITERATION)
    {
        OSM_NULLSYSCALL;
        OSM_NULLSYSCALL;
        OSM_NULLSYSCALL;
        OSM_NULLSYSCALL;
        OSM_NULLSYSCALL;
        OSM_NULLSYSCALL;
        OSM_NULLSYSCALL;
        OSM_NULLSYSCALL;
        OSM_NULLSYSCALL;
        OSM_NULLSYSCALL;
    }
    gettimeofday(&end, nullptr);
    return calculate_time(start, end, iterations);
}

