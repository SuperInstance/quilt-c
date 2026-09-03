/* quilt/time.h — the `time.cell` cell kind (Phase 228).
 *
 * Wraps Google's TimesFM (Time Series Foundation Model) as a
 * Quilt cell. The cell is a time-series foundation model:
 * state = historical context, value = forecast with quantiles,
 * reads = covariates. The 5th cutting-edge adoption.
 *
 * Why this fits Quilt:
 * - Cell state: the historical time series tensor [T, V]
 * - Cell value: the forecast tensor [H, V] + quantile intervals
 * - Cell reads: covariates (past-only, past-and-future)
 * - The 5+1+1+1+1+1 opcodes apply unchanged.
 *   BIND sets the context.
 *   VIEW reads the forecast.
 *   EFFECT re-forecasts.
 *   PROOF chain-anchors the context hash.
 *   ROUTE picks the model: TimesFM 2.5 vs 3.0, PyTorch vs Flax.
 *   CRDT converges forecasts across replicas.
 * - The 5 abductive-loop operations (from WORLD) compose.
 * - This is the 5th cutting-edge adoption (after PROOF, ROUTE,
 *   CRDT, WORLD). The new opcode: TIME.
 *
 * The polyformalism claim: the shape is the same in C, Python,
 * and (eventually) every other Quilt substrate. The substrate
 * binding (TimesFM 2.5 / 3.0 / PyTorch / Flax / distilled 4B)
 * is the only thing that varies.
 */
#ifndef QUILT_TIME_H
#define QUILT_TIME_H

#include "quilt/cell.h"

#ifdef __cplusplus
extern "C"
#endif

/* ── The 5 time-cell operations ─────────────────────────────────── */
typedef enum {
    /* Bind the historical context (the time series tensor) */
    QUILT_TIME_BIND_CONTEXT = 0,
    /* Bind covariates (past-only or past-and-future) */
    QUILT_TIME_BIND_COVARIATE = 1,
    /* Run the model: produce forecast + quantile intervals */
    QUILT_TIME_FORECAST = 2,
    /* Read the forecast (point estimate, median quantile) */
    QUILT_TIME_READ_POINT = 3,
    /* Read the quantile prediction intervals */
    QUILT_TIME_READ_QUANTILE = 4,
    QUILT_TIME__OPS_COUNT = 5
} quilt_time_op_t;

const char *quilt_time_op_name(quilt_time_op_t op);

/* ── A forecast output ───────────────────────────────────────────── */
typedef struct {
    /* The point forecast (median quantile) for each variate */
    double      *point;       /* owned: array of size horizon * n_variates */
    size_t       point_len;
    /* The quantile prediction intervals */
    double      *quantiles;   /* owned: 2D array [n_quantiles, horizon * n_variates] */
    size_t       n_quantiles;
    size_t       quantile_len;  /* size of each quantile vector */
    /* The model version: 0=2.5, 1=3.0 */
    int          model_version;
    /* The model variant: 0=PyTorch, 1=Flax */
    int          model_variant;
} quilt_forecast_t;

void quilt_forecast_init(quilt_forecast_t *f);
void quilt_forecast_free(quilt_forecast_t *f);

/* ── A time cell ──────────────────────────────────────────────────── */
typedef struct {
    /* The historical context tensor [context_length, n_variates] */
    double      *context;       /* owned: 2D row-major */
    size_t       context_len;   /* number of time steps */
    size_t       n_variates;    /* number of channels */
    /* The covariates (past-only and past-and-future) */
    double      *past_only;     /* owned: [context_len, n_past_only_cov] */
    size_t       n_past_only_cov;
    double      *past_future;   /* owned: [context_len + horizon, n_pf_cov] */
    size_t       n_past_future_cov;
    /* The forecast horizon */
    size_t       horizon;
    /* The FNV-1a 64-bit state hash of the context (4 slices) */
    uint8_t      prev_hash[32];
    uint8_t      state_hash[32];
    /* The last forecast */
    quilt_forecast_t forecast;
    /* The model version: 0=2.5, 1=3.0 */
    int          model_version;
    /* Counters for each of the 5 operations */
    int          n_bind_context;
    int          n_bind_covariate;
    int          n_forecast;
    int          n_read_point;
    int          n_read_quantile;
} quilt_time_cell_t;

void quilt_time_cell_init(quilt_time_cell_t *c);
void quilt_time_cell_free(quilt_time_cell_t *c);

/* Set the historical context. BIND in Quilt terms. */
int  quilt_time_bind_context(quilt_time_cell_t *c,
                               const double *context, size_t context_len,
                               size_t n_variates);
/* Set the past-only covariates. */
int  quilt_time_bind_past_only_covariate(quilt_time_cell_t *c,
                                           const double *cov, size_t cov_len,
                                           size_t n_cov);
/* Set the past-and-future covariates. */
int  quilt_time_bind_past_future_covariate(quilt_time_cell_t *c,
                                             const double *cov, size_t cov_len,
                                             size_t n_cov);
/* Set the forecast horizon. */
int  quilt_time_set_horizon(quilt_time_cell_t *c, size_t horizon);
/* Run the model. Synthetic stub (the real binding is the substrate). */
int  quilt_time_forecast(quilt_time_cell_t *c);
/* Read the point forecast (median quantile) for a given variate. */
int  quilt_time_read_point(quilt_time_cell_t *c, size_t variate,
                             double *out, size_t out_len);
/* Read a quantile forecast (e.g. q=0.1, q=0.5, q=0.9) for a given variate. */
int  quilt_time_read_quantile(quilt_time_cell_t *c, double q,
                                size_t variate, double *out, size_t out_len);

/* The polyformalism: kind name and op count. */
const char *quilt_time_kind_name(void);
int  quilt_time_kind_count(void);

#ifdef __cplusplus
}
#endif

#endif /* QUILT_TIME_H */
