/* quilt/time.c — the `time.cell` cell kind runtime.
 *
 * Synthetic stub: the real forecast is produced by TimesFM (the
 * substrate binding). This C port provides the polyformalism shape.
 */
#include "quilt/time.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* FNV-1a 64-bit (matches proof.c) */
static uint64_t fnv1a64(const void *data, size_t n)
{
    const uint8_t *p = (const uint8_t *)data;
    uint64_t h = 0xcbf29ce484222325ULL;
    for (size_t i = 0; i < n; i++) {
        h ^= p[i];
        h *= 0x100000001b3ULL;
    }
    return h;
}

/* Hash a 2D tensor (context_len * n_variates doubles) into 32 bytes. */
static void hash_tensor(const double *t, size_t len, uint8_t out[32])
{
    uint64_t h = fnv1a64(t, len * sizeof(double));
    memset(out, 0, 32);
    for (int i = 0; i < 4; i++) {
        uint64_t slice = h + (uint64_t)i * 0x9e3779b97f4a7c15ULL;
        memcpy(&out[i * 8], &slice, 8);
    }
}

const char *quilt_time_op_name(quilt_time_op_t op)
{
    switch (op) {
        case QUILT_TIME_BIND_CONTEXT:     return "BIND_CONTEXT";
        case QUILT_TIME_BIND_COVARIATE:   return "BIND_COVARIATE";
        case QUILT_TIME_FORECAST:         return "FORECAST";
        case QUILT_TIME_READ_POINT:       return "READ_POINT";
        case QUILT_TIME_READ_QUANTILE:    return "READ_QUANTILE";
        default: return "?";
    }
}

void quilt_forecast_init(quilt_forecast_t *f)
{
    if (!f) return;
    f->point = NULL;
    f->point_len = 0;
    f->quantiles = NULL;
    f->n_quantiles = 0;
    f->quantile_len = 0;
    f->model_version = 0;
    f->model_variant = 0;
}

void quilt_forecast_free(quilt_forecast_t *f)
{
    if (!f) return;
    if (f->point) { free(f->point); f->point = NULL; }
    if (f->quantiles) { free(f->quantiles); f->quantiles = NULL; }
    f->point_len = 0;
    f->n_quantiles = 0;
    f->quantile_len = 0;
}

void quilt_time_cell_init(quilt_time_cell_t *c)
{
    if (!c) return;
    c->context = NULL;
    c->context_len = 0;
    c->n_variates = 0;
    c->past_only = NULL;
    c->n_past_only_cov = 0;
    c->past_future = NULL;
    c->n_past_future_cov = 0;
    c->horizon = 0;
    memset(c->prev_hash, 0, 32);
    memset(c->state_hash, 0, 32);
    quilt_forecast_init(&c->forecast);
    c->model_version = 1;  /* default: 3.0 */
    c->n_bind_context = 0;
    c->n_bind_covariate = 0;
    c->n_forecast = 0;
    c->n_read_point = 0;
    c->n_read_quantile = 0;
}

void quilt_time_cell_free(quilt_time_cell_t *c)
{
    if (!c) return;
    if (c->context) { free(c->context); c->context = NULL; }
    if (c->past_only) { free(c->past_only); c->past_only = NULL; }
    if (c->past_future) { free(c->past_future); c->past_future = NULL; }
    quilt_forecast_free(&c->forecast);
    c->context_len = 0;
    c->n_variates = 0;
    c->horizon = 0;
}

/* Helper: ensure a tensor buffer is large enough. */
static int ensure_capacity(double **buf, size_t *cap, size_t need)
{
    if (*cap >= need) return 0;
    size_t new_cap = *cap ? *cap : 64;
    while (new_cap < need) new_cap *= 2;
    double *new_buf = realloc(*buf, new_cap * sizeof(double));
    if (!new_buf) return -1;
    *buf = new_buf;
    *cap = new_cap;
    return 0;
}

int quilt_time_bind_context(quilt_time_cell_t *c,
                              const double *context, size_t context_len,
                              size_t n_variates)
{
    if (!c || !context) return -1;
    /* BIND: save prev_hash, then set the new state. */
    memcpy(c->prev_hash, c->state_hash, 32);
    /* Ensure context buffer is large enough. */
    size_t need = context_len * n_variates;
    if (ensure_capacity(&c->context, &c->context_len, need) != 0) return -1;
    /* Hmm: realloc may invalidate need. Use the new cap. */
    memcpy(c->context, context, need * sizeof(double));
    c->context_len = context_len;
    c->n_variates = n_variates;
    /* New state_hash. */
    hash_tensor(context, need, c->state_hash);
    /* Invalidate the previous forecast (any BIND invalidates it). */
    quilt_forecast_free(&c->forecast);
    quilt_forecast_init(&c->forecast);
    c->n_bind_context++;
    return 0;
}

int quilt_time_bind_past_only_covariate(quilt_time_cell_t *c,
                                          const double *cov, size_t cov_len,
                                          size_t n_cov)
{
    if (!c || !cov) return -1;
    size_t need = cov_len * n_cov;
    if (ensure_capacity(&c->past_only, &c->n_past_only_cov, need) != 0) return -1;
    memcpy(c->past_only, cov, need * sizeof(double));
    c->n_past_only_cov = n_cov;
    c->n_bind_covariate++;
    return 0;
}

int quilt_time_bind_past_future_covariate(quilt_time_cell_t *c,
                                            const double *cov, size_t cov_len,
                                            size_t n_cov)
{
    if (!c || !cov) return -1;
    size_t need = cov_len * n_cov;
    if (ensure_capacity(&c->past_future, &c->n_past_future_cov, need) != 0) return -1;
    memcpy(c->past_future, cov, need * sizeof(double));
    c->n_past_future_cov = n_cov;
    c->n_bind_covariate++;
    return 0;
}

int quilt_time_set_horizon(quilt_time_cell_t *c, size_t horizon)
{
    if (!c) return -1;
    c->horizon = horizon;
    return 0;
}

int quilt_time_forecast(quilt_time_cell_t *c)
{
    if (!c) return -1;
    if (c->context_len == 0 || c->horizon == 0) return -1;
    /* Synthetic forecast: hash of the context, expanded to a horizon
     * vector with monotonically-changing values. The real binding
     * is TimesFM (PyTorch or Flax), called via the substrate. */
    size_t out_len = c->horizon * c->n_variates;
    if (c->forecast.point) { free(c->forecast.point); c->forecast.point = NULL; }
    c->forecast.point = calloc(out_len, sizeof(double));
    if (!c->forecast.point) return -1;
    c->forecast.point_len = out_len;
    /* 9 quantiles: 0.1, 0.2, ..., 0.9 (matches TimesFM default). */
    c->forecast.n_quantiles = 9;
    c->forecast.quantile_len = out_len;
    if (c->forecast.quantiles) { free(c->forecast.quantiles); c->forecast.quantiles = NULL; }
    c->forecast.quantiles = calloc(9 * out_len, sizeof(double));
    if (!c->forecast.quantiles) return -1;
    /* Hash the context to seed a synthetic pattern. */
    uint64_t h = fnv1a64(c->context, c->context_len * c->n_variates * sizeof(double));
    /* Generate a per-variate, per-timestep forecast. */
    for (size_t v = 0; v < c->n_variates; v++) {
        for (size_t t = 0; t < c->horizon; t++) {
            h = (h * 0x100000001b3ULL) ^ ((uint64_t)v * 0x9e3779b97f4a7c15ULL);
            h = (h * 0x100000001b3ULL) ^ ((uint64_t)t);
            /* Use a 32-bit cast to keep values in the int range
             * (so the double cast is exact). */
            double base = (double)((int32_t)(h & 0xFFFFFFFF)) / 100.0;  /* ~-2.1e7..+2.1e7 */
            /* Squash to -50..+50 via tanh-like clipping. */
            if (base > 50.0) base = 50.0;
            if (base < -50.0) base = -50.0;
            c->forecast.point[v * c->horizon + t] = base;
            /* Quantile intervals: spread around the base.
             * q=4 is the median (offset 0), q=0 is q=0.1 (offset -8),
             * q=8 is q=0.9 (offset +8). */
            for (size_t q = 0; q < 9; q++) {
                double offset = ((double)q - 4.0) * 2.0;  /* -8..+8 */
                c->forecast.quantiles[q * out_len + v * c->horizon + t] = base + offset;
            }
        }
    }
    c->forecast.model_version = c->model_version;
    c->n_forecast++;
    return 0;
}

int quilt_time_read_point(quilt_time_cell_t *c, size_t variate,
                            double *out, size_t out_len)
{
    if (!c || !out) return -1;
    if (c->forecast.point_len == 0) return -1;
    if (variate >= c->n_variates) return -1;
    if (out_len < c->horizon) return -1;
    for (size_t t = 0; t < c->horizon; t++) {
        out[t] = c->forecast.point[variate * c->horizon + t];
    }
    c->n_read_point++;
    return 0;
}

int quilt_time_read_quantile(quilt_time_cell_t *c, double q,
                               size_t variate, double *out, size_t out_len)
{
    if (!c || !out) return -1;
    if (c->forecast.quantiles == NULL) return -1;
    if (variate >= c->n_variates) return -1;
    if (out_len < c->horizon) return -1;
    /* Map q in [0.1, 0.9] to a quantile index in [0, 8] (9 quantiles). */
    int qi = (int)(q * 10.0 - 0.5);  /* 0.5 -> 4 (median), 0.1 -> 0, 0.9 -> 8 */
    if (qi < 0) qi = 0;
    if (qi > 8) qi = 8;
    for (size_t t = 0; t < c->horizon; t++) {
        out[t] = c->forecast.quantiles[qi * (c->horizon * c->n_variates)
                                        + variate * c->horizon + t];
    }
    c->n_read_quantile++;
    return 0;
}

const char *quilt_time_kind_name(void) { return "time.cell"; }
int  quilt_time_kind_count(void) { return QUILT_TIME__OPS_COUNT; }
