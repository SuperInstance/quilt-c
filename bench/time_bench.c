/* Time the TimeCell operations on a fixed input.
 * Same input as the Python benchmark for direct comparison.
 */
#define _POSIX_C_SOURCE 199309L
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <math.h>
#include "quilt/time.h"

int main(void) {
    /* Deterministic input: cumulative sum of normal + 100.0 */
    const int N = 1000;
    double data[1000];
    srand(42);
    double v = 100.0;
    for (int i = 0; i < N; i++) {
        double u1 = (double)rand() / RAND_MAX;
        double u2 = (double)rand() / RAND_MAX;
        double z = sqrt(-2.0 * log(u1)) * cos(2.0 * 3.14159265 * u2);
        v += z;
        data[i] = v;
    }

    /* Run forecast on each step */
    quilt_time_cell_t cell;
    quilt_time_cell_init(&cell);
    quilt_time_set_horizon(&cell, 5);

    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    for (int i = 0; i < N; i++) {
        double ctx[64];
        int start = (i < 63) ? 0 : i - 63;
        int len = i - start + 1;
        for (int j = 0; j < len; j++) {
            ctx[j] = data[start + j];
        }
        quilt_time_bind_context(&cell, ctx, len, 1);
        quilt_time_forecast(&cell);
    }
    clock_gettime(CLOCK_MONOTONIC, &t1);

    double elapsed = (t1.tv_sec - t0.tv_sec) + (t1.tv_nsec - t0.tv_nsec) * 1e-9;
    double us_per_step = elapsed * 1e6 / N;
    printf("C:     %.3f us/step (%d steps in %.3f s)\n", us_per_step, N, elapsed);
    printf("  state_hash: ");
    for (int i = 0; i < 32; i++) printf("%02x", cell.state_hash[i]);
    printf("\n");
    return 0;
}
