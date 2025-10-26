/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef LTE_MODEM_H
#define LTE_MODEM_H

#include <stddef.h>
#include <stdbool.h>
#include <limits.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MAX_NEIGH_CELLS 16

/* UMTS */
struct lte_umts_cell {
    int channel;
    int psc;
    int rscp;
    int ecio;
};

struct lte_umts_info {
    int location_area_code;
    int cell_id;
    int channel;
    int primary_scrambling_code;
    int rscp;
    int ecio;
    struct lte_umts_cell neigh[MAX_NEIGH_CELLS];
    int neigh_count;
};

/* LTE */
struct lte_cell {
    int    physical_cell_id;
    double rsrp;
    double rsrq;
    double rssi;
};

struct lte_lte_info {
    int  tracking_area_code;
    int  enodeb_id;
    int  cell_id;
    int  channel;
    int  band;
    int  frequency;
    char duplex[8];
    int  serving_cell_id;
    struct lte_cell neigh[MAX_NEIGH_CELLS];
    int  neigh_count;
};

struct lte_cellinfo {
    bool has_umts;
    bool has_lte;
    struct lte_umts_info umts;
    struct lte_lte_info  lte_intra;  /* intrafrequency_lte_info */
    struct lte_lte_info  lte_inter;  /* interfrequency_lte_info (only neighbours typically) */
};

/* ---------------- Signal info -------------------------------------------- */

struct lte_signal_info {
    char  type[16];    /* "lte", "wcdma", "gsm", ... or "" if unknown */

    long  rssi;        /* dBm; for GSM may map from "signal" */
    long  ecio;        /* WCDMA only, LONG_MIN if absent */

    long  rsrq;        /* LTE: dB (int), LONG_MIN if absent */
    long  rsrp;        /* LTE: dBm (int), LONG_MIN if absent */

    double snr;        /* LTE: dB (double) */
    bool   snr_valid;  /* true if snr is present */

    long  signal;      /* GSM: dBm from "signal"; LONG_MIN if absent */
};

/* Device control */
void        lte_modem_set_device(const char *path);
const char* lte_modem_get_device(void);

/* Signal-info API */
int lte_modem_get_signal(long *out_rssi, long *out_ecio);
int lte_modem_get_signal_ex(char *out_type, size_t type_cap,
                            long *out_rssi, long *out_ecio);
int lte_modem_get_signal_info(struct lte_signal_info *info);
int lte_modem_get_signal_str(char *buf, size_t buf_sz);

/* ---------------- Cell location info ------------------------------------- */

int  lte_modem_get_cell_location(struct lte_cellinfo *out);
void lte_modem_print_cell_location(const struct lte_cellinfo *ci);

#ifdef __cplusplus
}
#endif
#endif /* LTE_MODEM_H */
