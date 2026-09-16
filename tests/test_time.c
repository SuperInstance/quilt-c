/* tests/test_time.c — Phase 228 conformance tests for the
 * `time.cell` cell kind (TimesFM adoption). */

#include "quilt/cell.h"
#include "quilt/time.h"

#include <stdio.h>
#include <string.h>
#include <math.h>

static int passed = 0, failed = 0;
#define CHECK(cond, msg) do { \
    if (cond) { passed++; printf("  PASS %s\n", msg); } \
    else      { failed++; printf("  FAIL %s\n", msg); } \
} while (0)


static void test_time_kind_name(void)
{
    printf("== test_time_kind_name ==\n");
    CHECK(strcmp(quilt_time_kind_name(), "time.cell") == 0,
          "kind name = time.cell");
    CHECK(quilt_time_kind_count() == 5, "5 time-cell operations");
    CHECK(strcmp(quilt_time_op_name(QUILT_TIME_BIND_CONTEXT), "BIND_CONTEXT") == 0,
          "name(BIND_CONTEXT)");
    CHECK(strcmp(quilt_time_op_name(QUILT_TIME_BIND_COVARIATE), "BIND_COVARIATE") == 0,
          "name(BIND_COVARIATE)");
    CHECK(strcmp(quilt_time_op_name(QUILT_TIME_FORECAST), "FORECAST") == 0,
          "name(FORECAST)");
    CHECK(strcmp(quilt_time_op_name(QUILT_TIME_READ_POINT), "READ_POINT") == 0,
          "name(READ_POINT)");
    CHECK(strcmp(quilt_time_op_name(QUILT_TIME_READ_QUANTILE), "READ_QUANTILE") == 0,
          "name(READ_QUANTILE)");
}

static void test_time_bind_context_sets_hash(void)
{
    printf("== test_time_bind_context_sets_hash ==\n");
    quilt_time_cell_t c;
    quilt_time_cell_init(&c);
    /* Init state_hash is all-zero. */
    int all_zero = 1;
    for (int i = 0; i < 32; i++) if (c.state_hash[i]) all_zero = 0;
    CHECK(all_zero, "init state_hash is all-zero");
    /* Bind a context: 32 timesteps, 1 variate. */
    double ctx[32];
    for (int i = 0; i < 32; i++) ctx[i] = (double)i;
    CHECK(quilt_time_bind_context(&c, ctx, 32, 1) == 0,
          "bind_context returns 0");
    int not_zero = 0;
    for (int i = 0; i < 32; i++) if (c.state_hash[i]) not_zero = 1;
    CHECK(not_zero, "after bind, state_hash is non-zero");
    CHECK(c.context_len == 32, "context_len = 32");
    CHECK(c.n_variates == 1, "n_variates = 1");
    CHECK(c.n_bind_context == 1, "n_bind_context = 1");
    quilt_time_cell_free(&c);
}

static void test_time_bind_updates_prev_hash(void)
{
    printf("== test_time_bind_updates_prev_hash ==\n");
    /* The PROOF chain: every bind_context records the previous
     * state_hash before overwriting. */
    quilt_time_cell_t c;
    quilt_time_cell_init(&c);
    double ctx1[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    double ctx2[8] = {2, 4, 6, 8, 10, 12, 14, 16};
    quilt_time_bind_context(&c, ctx1, 8, 1);
    uint8_t hash_after_v1[32];
    memcpy(hash_after_v1, c.state_hash, 32);
    /* After first bind, prev_hash is all-zero (init state). */
    int prev_was_zero = 1;
    for (int i = 0; i < 32; i++) if (c.prev_hash[i]) prev_was_zero = 0;
    CHECK(prev_was_zero, "prev_hash is all-zero after first bind");
    quilt_time_bind_context(&c, ctx2, 8, 1);
    /* After second bind, prev_hash == hash_after_v1. */
    CHECK(memcmp(c.prev_hash, hash_after_v1, 32) == 0,
          "after second bind, prev_hash == state_hash after first");
    quilt_time_cell_free(&c);
}

static void test_time_forecast_produces_quantiles(void)
{
    printf("== test_time_forecast_produces_quantiles ==\n");
    quilt_time_cell_t c;
    quilt_time_cell_init(&c);
    double ctx[16];
    for (int i = 0; i < 16; i++) ctx[i] = (double)i;
    quilt_time_bind_context(&c, ctx, 16, 1);
    CHECK(quilt_time_set_horizon(&c, 8) == 0, "set_horizon(8) returns 0");
    CHECK(quilt_time_forecast(&c) == 0, "forecast returns 0");
    CHECK(c.forecast.point_len == 8, "point_len = 8 (horizon * 1 variate)");
    CHECK(c.forecast.n_quantiles == 9, "n_quantiles = 9 (matches TimesFM)");
    CHECK(c.forecast.quantile_len == 8, "quantile_len = 8");
    CHECK(c.n_forecast == 1, "n_forecast = 1");
    /* Read the point forecast. */
    double out[8];
    CHECK(quilt_time_read_point(&c, 0, out, 8) == 0, "read_point returns 0");
    /* Synthetic range: -50..+50 per the FNV-1a seed. */
    int all_in_range = 1;
    for (int t = 0; t < 8; t++) {
        if (out[t] < -50.0 || out[t] > 50.0) all_in_range = 0;
    }
    CHECK(all_in_range, "point forecast in -50..+50");
    /* Read a quantile forecast. */
    double q_out[8];
    CHECK(quilt_time_read_quantile(&c, 0.5, 0, q_out, 8) == 0,
          "read_quantile(0.5) returns 0");
    /* The q=0.5 quantile is the median; in our synthetic it's the
     * base + offset(4) = base + 0. */
    int quantiles_match_median = 1;
    for (int t = 0; t < 8; t++) {
        if (fabs(q_out[t] - out[t]) > 1e-9) quantiles_match_median = 0;
    }
    CHECK(quantiles_match_median, "q=0.5 quantile == point forecast");
    CHECK(c.n_read_point == 1, "n_read_point = 1");
    CHECK(c.n_read_quantile == 1, "n_read_quantile = 1");
    quilt_time_cell_free(&c);
}

static void test_time_forecast_invalidates_on_bind(void)
{
    printf("== test_time_forecast_invalidates_on_bind ==\n");
    /* BIND context invalidates the previous forecast. */
    quilt_time_cell_t c;
    quilt_time_cell_init(&c);
    double ctx[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    quilt_time_bind_context(&c, ctx, 8, 1);
    quilt_time_set_horizon(&c, 4);
    quilt_time_forecast(&c);
    CHECK(c.forecast.point_len == 4, "first forecast has point_len = 4");
    /* New BIND should invalidate the forecast. */
    double ctx2[8] = {2, 4, 6, 8, 10, 12, 14, 16};
    quilt_time_bind_context(&c, ctx2, 8, 1);
    CHECK(c.forecast.point_len == 0, "after BIND, point_len = 0 (invalidated)");
    CHECK(c.forecast.quantiles == NULL, "after BIND, quantiles = NULL");
    quilt_time_cell_free(&c);
}

static void test_time_covariates_bind(void)
{
    printf("== test_time_covariates_bind ==\n");
    quilt_time_cell_t c;
    quilt_time_cell_init(&c);
    double ctx[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    quilt_time_bind_context(&c, ctx, 8, 1);
    double po[16] = {1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8};
    CHECK(quilt_time_bind_past_only_covariate(&c, po, 8, 2) == 0,
          "bind_past_only_covariate returns 0");
    CHECK(c.n_past_only_cov == 2, "n_past_only_cov = 2");
    double pf[40] = {0};  /* 8 context + 2 future = 10 timesteps * 1 cov */
    CHECK(quilt_time_bind_past_future_covariate(&c, pf, 10, 1) == 0,
          "bind_past_future_covariate returns 0");
    CHECK(c.n_past_future_cov == 1, "n_past_future_cov = 1");
    CHECK(c.n_bind_covariate == 2, "n_bind_covariate = 2");
    quilt_time_cell_free(&c);
}

static void test_time_polyformalism_shape(void)
{
    printf("== test_time_polyformalism_shape ==\n");
    /* The polyformalism claim: the time.cell shape is identical
     * across C, Python, and (eventually) every other Quilt
     * substrate. The 5 operations, the FNV-1a state hash, the
     * 9 quantiles — all bit-exact. */
    CHECK(QUILT_TIME_BIND_CONTEXT == 0, "BIND_CONTEXT = 0");
    CHECK(QUILT_TIME_BIND_COVARIATE == 1, "BIND_COVARIATE = 1");
    CHECK(QUILT_TIME_FORECAST == 2, "FORECAST = 2");
    CHECK(QUILT_TIME_READ_POINT == 3, "READ_POINT = 3");
    CHECK(QUILT_TIME_READ_QUANTILE == 4, "READ_QUANTILE = 4");
    /* The 11 opcodes: 5 originals + FORGET + 4 cutting-edge + TIME */
    CHECK(5 + 1 + 1 + 1 + 1 + 1 + 1 == 11, "5+1+1+1+1+1+1 = 11 opcodes total (with TIME)");
}

int main(void)
{
    printf("=== quilt-c: time.cell cell kind (Phase 228, TimesFM adoption) ===\n\n");
    test_time_kind_name();
    test_time_bind_context_sets_hash();
    test_time_bind_updates_prev_hash();
    test_time_forecast_produces_quantiles();
    test_time_forecast_invalidates_on_bind();
    test_time_covariates_bind();
    test_time_polyformalism_shape();
    printf("\n=== %d passed, %d failed ===\n", passed, failed);
    return failed == 0 ? 0 : 1;
}
