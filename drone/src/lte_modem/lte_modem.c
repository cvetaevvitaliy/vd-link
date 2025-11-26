/* SPDX-License-Identifier: GPL-2.0-only */
/**
 * Minimal uqmi wrapper for signal & cell location info (LTE/WCDMA/GSM).
 * Requires: json-c, uqmi in PATH.
 *
 * Build example:
 *   gcc -O2 lte_modem.c -ljson-c -o lte_modem_demo -DLTE_MODEM_DEBUG
 */
#include "lte_modem.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <limits.h>
#include <stdbool.h>

#include <json-c/json.h>

/* Default QMI device (can be overridden by lte_modem_set_device()) */
#ifndef LTE_MODEM_DEVICE
#define LTE_MODEM_DEVICE "/dev/cdc-wdm0"
#endif

/* uqmi command parts for signal-info */
#define UQMI_CMD_PREFIX "uqmi -d "
#define UQMI_CMD_SUFFIX " --get-signal-info --timeout 1000"

/* IO sizes */
#define READ_CHUNK (1024*8)
#define MAX_CMD    512

/* ---------------- Device management -------------------------------------- */

static char g_device_path[128] = LTE_MODEM_DEVICE;
static bool g_env_checked = false;

/* Return current device path (internal helper). */
static const char* _lte_modem_current_device(void)
{
    /* One-time env override, if present */
    if (!g_env_checked) {
        const char* e = getenv("LTE_MODEM_DEVICE");
        if (e && *e) {
            strncpy(g_device_path, e, sizeof(g_device_path) - 1);
            g_device_path[sizeof(g_device_path) - 1] = '\0';
        }
        g_env_checked = true;
    }
    return g_device_path;
}

void lte_modem_set_device(const char* path)
{
    if (!path || !*path)
        return;
    strncpy(g_device_path, path, sizeof(g_device_path) - 1);
    g_device_path[sizeof(g_device_path) - 1] = '\0';
    /* After explicit set, don't let env override */
    g_env_checked = true;
}

const char* lte_modem_get_device(void)
{
    return _lte_modem_current_device();
}

/* ---------------- Helpers ------------------------------------------------- */

/* Execute command and capture stdout into a dynamically-allocated buffer. */
static char* read_cmd_output(const char* cmd, size_t* out_len)
{
#ifdef LTE_MODEM_DEBUG
    fprintf(stderr, "[lte_modem] run: %s\n", cmd);
#endif
    FILE* fp = popen(cmd, "r");
    if (!fp) {
        fprintf(stderr, "[lte_modem] popen() failed: %s\n", strerror(errno));
        return NULL;
    }

    size_t cap = READ_CHUNK;
    size_t len = 0;
    char* buf = (char*)malloc(cap);
    if (!buf) {
        fprintf(stderr, "[lte_modem] malloc() failed\n");
        pclose(fp);
        return NULL;
    }

    while (!feof(fp)) {
        if (cap - len < READ_CHUNK) {
            size_t new_cap = cap * 2;
            char* tmp = (char*)realloc(buf, new_cap);
            if (!tmp) {
                fprintf(stderr, "[lte_modem] realloc() failed\n");
                free(buf);
                pclose(fp);
                return NULL;
            }
            buf = tmp;
            cap = new_cap;
        }
        size_t n = fread(buf + len, 1, READ_CHUNK, fp);
        len += n;
    }

    int rc = pclose(fp);
    if (rc != 0) {
        /* uqmi may print valid JSON while returning non-zero (transient). */
#ifdef LTE_MODEM_DEBUG
        fprintf(stderr, "[lte_modem] warning: exit status: %d\n", rc);
#endif
    }

    if (cap == len) {
        char* tmp = (char*)realloc(buf, cap + 1);
        if (!tmp) {
            fprintf(stderr, "[lte_modem] realloc() failed (NUL)\n");
            free(buf);
            return NULL;
        }
        buf = tmp;
    }
    buf[len] = '\0';
    if (out_len)
        *out_len = len;

#ifdef LTE_MODEM_DEBUG
    // fprintf(stderr, "[lte_modem] raw: %.*s\n", (int)len, buf);
#endif
    return buf;
}

/* Build uqmi command for the current device (signal-info). */
static int build_uqmi_cmd(char* buf, size_t cap)
{
    const char* dev = _lte_modem_current_device();
    int n = snprintf(buf, cap, "%s%s%s", UQMI_CMD_PREFIX, dev, UQMI_CMD_SUFFIX);
    if (n < 0 || (size_t)n >= cap) {
        fprintf(stderr, "[lte_modem] command buffer too small\n");
        return -1;
    }
    return 0;
}

/* Obtain integer as long (accept int or double JSON by rounding). */
static bool json_get_number_as_long(struct json_object* obj, long* out_val)
{
    if (!obj || !out_val)
        return false;

    if (json_object_is_type(obj, json_type_int)) {
        *out_val = json_object_get_int64(obj);
        return true;
    }
    if (json_object_is_type(obj, json_type_double)) {
        double d = json_object_get_double(obj);
        *out_val = (long)(d >= 0 ? d + 0.5 : d - 0.5); /* round to nearest */
        return true;
    }
    return false;
}

/* ---------------- Signal-info parsing ------------------------------------- */

static int parse_signal_info_json_full(const char* json_text, struct lte_signal_info* info)
{
    if (!info)
        return -1;

    /* Initialize defaults */
    memset(info->type, 0, sizeof(info->type));
    info->rssi = LONG_MIN;
    info->ecio = LONG_MIN;
    info->rsrq = LONG_MIN;
    info->rsrp = LONG_MIN;
    info->snr = 0.0;
    info->snr_valid = false;
    info->signal = LONG_MIN; /* GSM */

    struct json_tokener* tok = json_tokener_new();
    if (!tok) {
        fprintf(stderr, "[lte_modem] json_tokener_new() failed\n");
        return -1;
    }

    struct json_object* root = json_tokener_parse_ex(tok, json_text, (int)strlen(json_text));
    enum json_tokener_error jerr = json_tokener_get_error(tok);
    if (jerr != json_tokener_success || !root) {
        fprintf(stderr, "[lte_modem] JSON parse error: %s\n", json_tokener_error_desc(jerr));
        json_tokener_free(tok);
        return -1;
    }

    struct json_object* v = NULL;

    /* type */
    if (json_object_object_get_ex(root, "type", &v) && json_object_is_type(v, json_type_string)) {
        const char* s = json_object_get_string(v);
        if (s) {
            strncpy(info->type, s, sizeof(info->type) - 1);
            info->type[sizeof(info->type) - 1] = '\0';
        }
    }

    /* Common / RAT-specific fields */
    if (json_object_object_get_ex(root, "rssi", &v))
        (void)json_get_number_as_long(v, &info->rssi);
    if (json_object_object_get_ex(root, "ecio", &v))
        (void)json_get_number_as_long(v, &info->ecio);
    if (json_object_object_get_ex(root, "rsrq", &v))
        (void)json_get_number_as_long(v, &info->rsrq);
    if (json_object_object_get_ex(root, "rsrp", &v))
        (void)json_get_number_as_long(v, &info->rsrp);

    if (json_object_object_get_ex(root, "snr", &v)) {
        if (json_object_is_type(v, json_type_double) || json_object_is_type(v, json_type_int)) {
            info->snr = json_object_get_double(v);
            info->snr_valid = true;
        }
    }

    /* GSM: "signal" (map to rssi if rssi missing) */
    if (json_object_object_get_ex(root, "signal", &v)) {
        (void)json_get_number_as_long(v, &info->signal);
        if (info->rssi == LONG_MIN)
            info->rssi = info->signal;
    }

    json_object_put(root);
    json_tokener_free(tok);
    return 0;
}

/* ---------------- Public API (legacy) ------------------------------------- */

int lte_modem_get_signal(long* out_rssi, long* out_ecio)
{
    struct lte_signal_info info;
    if (lte_modem_get_signal_info(&info) != 0)
        return -1;

    if (out_rssi)
        *out_rssi = info.rssi;
    if (out_ecio)
        *out_ecio = info.ecio;
    return 0;
}

int lte_modem_get_signal_ex(char* out_type, size_t type_cap, long* out_rssi, long* out_ecio)
{
    struct lte_signal_info info;
    if (lte_modem_get_signal_info(&info) != 0)
        return -1;

    if (out_type && type_cap) {
        out_type[0] = '\0';
        if (info.type[0]) {
            strncpy(out_type, info.type, type_cap - 1);
            out_type[type_cap - 1] = '\0';
        }
    }
    if (out_rssi)
        *out_rssi = info.rssi;
    if (out_ecio)
        *out_ecio = info.ecio;
    return 0;
}

/* ---------------- Public API (modern, signal) ----------------------------- */

int lte_modem_get_signal_info(struct lte_signal_info* info)
{
    if (!info)
        return -1;

    char cmd[MAX_CMD];
    if (build_uqmi_cmd(cmd, sizeof(cmd)) != 0)
        return -1;

    size_t len = 0;
    char* json_text = read_cmd_output(cmd, &len);
    if (!json_text)
        return -1;

    int rc = parse_signal_info_json_full(json_text, info);
    free(json_text);
    return rc;
}

int lte_modem_get_signal_str(char* buf, size_t buf_sz)
{
    if (!buf || buf_sz == 0)
        return -1;

    struct lte_signal_info info;
    if (lte_modem_get_signal_info(&info) != 0) {
        snprintf(buf, buf_sz, "unknown");
        return -1;
    }

    /* LTE formatting */
    if (info.type[0] && strcmp(info.type, "lte") == 0) {
        int n = snprintf(buf, buf_sz, "4G ");
        if (info.rssi != LONG_MIN)
            n += snprintf(buf + n, buf_sz > (size_t)n ? buf_sz - (size_t)n : 0, "rssi %lddBm\n", info.rssi);
        if (info.rsrp != LONG_MIN)
            n += snprintf(buf + n, buf_sz > (size_t)n ? buf_sz - (size_t)n : 0, "rsrp %lddBm\n", info.rsrp);
        if (info.rsrq != LONG_MIN)
            n += snprintf(buf + n, buf_sz > (size_t)n ? buf_sz - (size_t)n : 0, "rsrq %lddB\n", info.rsrq);
        if (info.snr_valid)
            snprintf(buf + n, buf_sz > (size_t)n ? buf_sz - (size_t)n : 0, "snr %.1fdB\n", info.snr);
        return 0;
    }

    /* GSM formatting */
    if (info.type[0] && strcmp(info.type, "gsm") == 0) {
        if (info.signal != LONG_MIN)
            snprintf(buf, buf_sz, "2G signal %lddBm", info.signal);
        else if (info.rssi != LONG_MIN)
            snprintf(buf, buf_sz, "2G rssi %lddBm", info.rssi);
        else
            snprintf(buf, buf_sz, "2G");
        return 0;
    }

    /* WCDMA formatting */
    if (info.type[0] && strcmp(info.type, "wcdma") == 0) {
        if (info.rssi != LONG_MIN && info.ecio != LONG_MIN)
            snprintf(buf, buf_sz, "3G rssi %lddBm ecio %ld", info.rssi, info.ecio);
        else if (info.rssi != LONG_MIN)
            snprintf(buf, buf_sz, "3G rssi %lddBm", info.rssi);
        else
            snprintf(buf, buf_sz, "3G");
        return 0;
    }

    /* Generic / unknown RAT */
    if (info.type[0]) {
        int n = snprintf(buf, buf_sz, "type=%s", info.type);
        if (info.rssi != LONG_MIN)
            snprintf(buf + n, buf_sz > (size_t)n ? buf_sz - (size_t)n : 0, " rssi %lddBm", info.rssi);
    } else if (info.rssi != LONG_MIN) {
        snprintf(buf, buf_sz, "rssi %lddBm", info.rssi);
    } else {
        snprintf(buf, buf_sz, "unknown");
    }
    return 0;
}

/* ---------------- Cell location parsing ----------------------------------- */
/* Helpers for strict JSON path */
static int get_int(struct json_object *o, const char *key)
{
    struct json_object *v = NULL;
    if (json_object_object_get_ex(o, key, &v) && json_object_is_type(v, json_type_int))
        return json_object_get_int(v);
    return 0;
}

static double get_double(struct json_object *o, const char *key)
{
    struct json_object *v = NULL;
    if (json_object_object_get_ex(o, key, &v) &&
        (json_object_is_type(v, json_type_double) || json_object_is_type(v, json_type_int)))
        return json_object_get_double(v);
    return 0.0;
}

static void get_str(struct json_object *o, const char *key, char *out, size_t n)
{
    struct json_object *v = NULL;
    if (json_object_object_get_ex(o, key, &v) && json_object_is_type(v, json_type_string)) {
        snprintf(out, n, "%s", json_object_get_string(v));
    } else if (n)
        out[0] = 0;
}

/* detect neighbour objects (uqmi dumps them as anonymous entries inside object) */
static void parse_umts_neighbors(struct json_object *root, struct lte_umts_info *u)
{
    u->neigh_count = 0;
    struct lh_table *t = json_object_get_object(root)->table;
    struct lh_entry *e;
    for (e = t->head; e && u->neigh_count < MAX_NEIGH_CELLS; e = e->next) {
        if (!e->v)
            return;
        struct json_object *val = (struct json_object *)e->v;
        if (!val)
            return;
        if (!json_object_is_type(val, json_type_object))
            continue;
        if (json_object_object_get_ex(val, "primary_scrambling_code", NULL)) {
            u->neigh[u->neigh_count].channel = get_int(val, "channel");
            u->neigh[u->neigh_count].psc = get_int(val, "primary_scrambling_code");
            u->neigh[u->neigh_count].rscp = get_int(val, "rscp");
            u->neigh[u->neigh_count].ecio = get_int(val, "ecio");
            u->neigh_count++;
        }
    }
}

static void parse_lte_cells(struct json_object *root, struct lte_lte_info *l)
{
    l->neigh_count = 0;
    struct lh_table *t = json_object_get_object(root)->table;
    struct lh_entry *e;
    for (e = t->head; e && l->neigh_count < MAX_NEIGH_CELLS; e = e->next) {
        struct json_object *val = (struct json_object *)e->v;
        if (!json_object_is_type(val, json_type_object))
            continue;
        if (json_object_object_get_ex(val, "physical_cell_id", NULL)) {
            l->neigh[l->neigh_count].physical_cell_id = get_int(val, "physical_cell_id");
            l->neigh[l->neigh_count].rsrq = get_double(val, "rsrq");
            l->neigh[l->neigh_count].rsrp = get_double(val, "rsrp");
            l->neigh[l->neigh_count].rssi = get_double(val, "rssi");
            l->neigh_count++;
        }
    }
}

/* --- Sanitize: cut the first {...} block out of noisy output -------------- */
static char* sanitize_payload(char *text, size_t *len_inout)
{
    if (!text) return NULL;
    const char *start = strchr(text, '{');
    if (!start) return text; // nothing to sanitize
    const char *end = strrchr(text, '}');
    if (!end || end < start) return text;

    size_t n = (size_t)(end - start + 1);
    char *clean = (char*)malloc(n + 1);
    if (!clean) return text; // fallback to original
    memcpy(clean, start, n);
    clean[n] = '\0';
    if (len_inout) *len_inout = n;
    free(text);
    return clean;
}

/* --- Tolerant text parsing fallback (for non-strict JSON) ----------------- */

static const char* find_key(const char* s, const char* key) {
    char needle[128];
    snprintf(needle, sizeof(needle), "\"%s\"", key);
    const char* p = strstr(s, needle);
    if (!p) return NULL;
    p = strchr(p + strlen(needle), ':');
    if (!p) return NULL;
    return p + 1;
}

static bool parse_long_after(const char* p, long* out) {
    if (!p || !out) return false;
    while (*p == ' ' || *p == '\t') p++;
    char* end = NULL;
    long v = strtol(p, &end, 10);
    if (end == p) return false;
    *out = v;
    return true;
}

static bool parse_double_after(const char* p, double* out) {
    if (!p || !out) return false;
    while (*p == ' ' || *p == '\t') p++;
    char* end = NULL;
    double v = strtod(p, &end);
    if (end == p) return false;
    *out = v;
    return true;
}

static const char* skip_to_next_object_end(const char* p) {
    while (*p && *p != '}') p++;
    return p;
}

static bool fallback_parse_umts(const char* text, struct lte_umts_info* u) {
    const char* root = strstr(text, "\"umts_info\"");
    if (!root) return false;

    memset(u, 0, sizeof(*u));
    long tmp;
    const char* p;

    if ((p = find_key(root, "location_area_code")) && parse_long_after(p, &tmp)) u->location_area_code = (int)tmp;
    if ((p = find_key(root, "cell_id"))            && parse_long_after(p, &tmp)) u->cell_id = (int)tmp;
    if ((p = find_key(root, "channel"))            && parse_long_after(p, &tmp)) u->channel = (int)tmp;
    if ((p = find_key(root, "primary_scrambling_code")) && parse_long_after(p, &tmp)) u->primary_scrambling_code = (int)tmp;
    if ((p = find_key(root, "rscp"))               && parse_long_after(p, &tmp)) u->rscp = (int)tmp;
    if ((p = find_key(root, "ecio"))               && parse_long_after(p, &tmp)) u->ecio = (int)tmp;

    /* Neighbours */
    u->neigh_count = 0;
    const char* q = root;
    const char* first_psc = find_key(root, "primary_scrambling_code");
    if (first_psc) q = first_psc + 1;

    while (u->neigh_count < MAX_NEIGH_CELLS) {
        const char* psc_k = find_key(q, "primary_scrambling_code");
        if (!psc_k) break;
        const char* obj_end = skip_to_next_object_end(psc_k);

        long psc = 0, ch = 0, rscp = 0, ecio = 0, v;
        const char* t;
        if ((t = find_key(psc_k, "channel")) && t < obj_end && parse_long_after(t, &v)) ch = (int)v;
        if ((t = find_key(psc_k, "rscp"))    && t < obj_end && parse_long_after(t, &v)) rscp = (int)v;
        if ((t = find_key(psc_k, "ecio"))    && t < obj_end && parse_long_after(t, &v)) ecio = (int)v;
        if (parse_long_after(psc_k, &v)) psc = (int)v;

        if (psc || rscp || ecio || ch) {
            u->neigh[u->neigh_count].psc = (int)psc;
            u->neigh[u->neigh_count].channel = (int)ch;
            u->neigh[u->neigh_count].rscp = (int)rscp;
            u->neigh[u->neigh_count].ecio = (int)ecio;
            u->neigh_count++;
        }

        if (!*obj_end) break;
        q = obj_end + 1;
    }

    return (u->location_area_code || u->cell_id || u->primary_scrambling_code || u->neigh_count > 0);
}

static bool fallback_parse_lte(const char* text, struct lte_lte_info* intra, struct lte_lte_info* inter) {
    bool any = false;

    /* Intra */
    const char* ri = strstr(text, "\"intrafrequency_lte_info\"");
    if (ri) {
        memset(intra, 0, sizeof(*intra));
        long v; double dv; const char* p;

        if ((p = find_key(ri, "tracking_area_code")) && parse_long_after(p, &v)) intra->tracking_area_code = (int)v;
        if ((p = find_key(ri, "enodeb_id"))          && parse_long_after(p, &v)) intra->enodeb_id = (int)v;
        if ((p = find_key(ri, "cell_id"))            && parse_long_after(p, &v)) intra->cell_id = (int)v;
        if ((p = find_key(ri, "channel"))            && parse_long_after(p, &v)) intra->channel = (int)v;
        if ((p = find_key(ri, "band"))               && parse_long_after(p, &v)) intra->band = (int)v;
        if ((p = find_key(ri, "frequency"))          && parse_long_after(p, &v)) intra->frequency = (int)v;

        /* duplex */
        {
            const char* dkey = strstr(ri, "\"duplex\"");
            if (dkey) {
                const char* dp = strchr(dkey, ':');
                if (dp) {
                    const char* q1 = strchr(dp, '"');
                    const char* q2 = q1 ? strchr(q1 + 1, '"') : NULL;
                    if (q1 && q2 && q2 > q1 + 1) {
                        size_t n = (size_t)(q2 - (q1 + 1));
                        if (n >= sizeof(intra->duplex)) n = sizeof(intra->duplex) - 1;
                        memcpy(intra->duplex, q1 + 1, n);
                        intra->duplex[n] = '\0';
                    }
                }
            }
        }
        if ((p = find_key(ri, "serving_cell_id"))    && parse_long_after(p, &v)) intra->serving_cell_id = (int)v;

        /* neighbours with physical_cell_id */
        intra->neigh_count = 0;
        const char* q = ri;
        while (intra->neigh_count < MAX_NEIGH_CELLS) {
            const char* pci_k = find_key(q, "physical_cell_id");
            if (!pci_k) break;
            const char* obj_end = skip_to_next_object_end(pci_k);

            long pci = 0;
            double rsrp = 0, rsrq = 0, rssi = 0;
            parse_long_after(pci_k, &pci);

            const char* t;
            if ((t = find_key(pci_k, "rsrp")) && t < obj_end && parse_double_after(t, &dv)) rsrp = dv;
            if ((t = find_key(pci_k, "rsrq")) && t < obj_end && parse_double_after(t, &dv)) rsrq = dv;
            if ((t = find_key(pci_k, "rssi")) && t < obj_end && parse_double_after(t, &dv)) rssi = dv;

            if (pci || rsrp || rsrq || rssi) {
                intra->neigh[intra->neigh_count].physical_cell_id = (int)pci;
                intra->neigh[intra->neigh_count].rsrp = rsrp;
                intra->neigh[intra->neigh_count].rsrq = rsrq;
                intra->neigh[intra->neigh_count].rssi = rssi;
                intra->neigh_count++;
            }
            if (!*obj_end) break;
            q = obj_end + 1;
        }
        any = any || (intra->tracking_area_code || intra->neigh_count > 0);
    }

    /* Inter: collect neighbours (optionally multiple blocks; we just scan pci entries) */
    const char* re = strstr(text, "\"interfrequency_lte_info\"");
    if (re) {
        memset(inter, 0, sizeof(*inter));
        inter->neigh_count = 0;
        const char* q = re;
        while (inter->neigh_count < MAX_NEIGH_CELLS) {
            const char* pci_k = find_key(q, "physical_cell_id");
            if (!pci_k) break;
            const char* obj_end = skip_to_next_object_end(pci_k);

            long pci = 0;
            double rsrp = 0, rsrq = 0, rssi = 0, dv;
            parse_long_after(pci_k, &pci);
            const char* t;
            if ((t = find_key(pci_k, "rsrp")) && t < obj_end && parse_double_after(t, &dv)) rsrp = dv;
            if ((t = find_key(pci_k, "rsrq")) && t < obj_end && parse_double_after(t, &dv)) rsrq = dv;
            if ((t = find_key(pci_k, "rssi")) && t < obj_end && parse_double_after(t, &dv)) rssi = dv;

            if (pci || rsrp || rsrq || rssi) {
                inter->neigh[inter->neigh_count].physical_cell_id = (int)pci;
                inter->neigh[inter->neigh_count].rsrp = rsrp;
                inter->neigh[inter->neigh_count].rsrq = rsrq;
                inter->neigh[inter->neigh_count].rssi = rssi;
                inter->neigh_count++;
            }
            if (!*obj_end) break;
            q = obj_end + 1;
        }
        any = any || (inter->neigh_count > 0);
    }

    return any;
}

/* Public API: cell location */
int lte_modem_get_cell_location(struct lte_cellinfo *out)
{
    if (!out)
        return -1;
    memset(out, 0, sizeof(*out));

    char cmd[MAX_CMD];
    const char *dev = _lte_modem_current_device();
    snprintf(cmd, sizeof(cmd), "uqmi -d %s --timeout 2000 --get-cell-location-info", dev);

    size_t len = 0;
    char *text = read_cmd_output(cmd, &len);
    if (!text)
        return -1;

#ifdef LTE_MODEM_DEBUG
    fprintf(stderr, "[lte_modem] raw len=%zu\n", len);
#endif

    /* Sanitize noisy output to the first {...} block */
    text = sanitize_payload(text, &len);

#ifdef LTE_MODEM_DEBUG
    fprintf(stderr, "[lte_modem] sanitized len=%zu\n", len);
    // fprintf(stderr, "[lte_modem] sanitized: %.*s\n", (int)len, text);
#endif

    /* First try strict JSON with json-c */
    bool parsed_any = false;
    struct json_tokener *tok = json_tokener_new();
    struct json_object *root = json_tokener_parse_ex(tok, text, len);
    if (root) {
        struct json_object *umts = NULL;
        if (json_object_object_get_ex(root, "umts_info", &umts) &&
            json_object_is_type(umts, json_type_object)) {
            out->has_umts = true;
            struct lte_umts_info *u = &out->umts;
            u->location_area_code = get_int(umts, "location_area_code");
            u->cell_id = get_int(umts, "cell_id");
            u->channel = get_int(umts, "channel");
            u->primary_scrambling_code = get_int(umts, "primary_scrambling_code");
            u->rscp = get_int(umts, "rscp");
            u->ecio = get_int(umts, "ecio");
            parse_umts_neighbors(umts, u);
            parsed_any = true;
        }

        struct json_object *lte_intra = NULL;
        if (json_object_object_get_ex(root, "intrafrequency_lte_info", &lte_intra) &&
            json_object_is_type(lte_intra, json_type_object)) {
            out->has_lte = true;
            struct lte_lte_info *l = &out->lte_intra;
            l->tracking_area_code = get_int(lte_intra, "tracking_area_code");
            l->enodeb_id = get_int(lte_intra, "enodeb_id");
            l->cell_id = get_int(lte_intra, "cell_id");
            l->channel = get_int(lte_intra, "channel");
            l->band = get_int(lte_intra, "band");
            l->frequency = get_int(lte_intra, "frequency");
            get_str(lte_intra, "duplex", l->duplex, sizeof(l->duplex));
            l->serving_cell_id = get_int(lte_intra, "serving_cell_id");
            parse_lte_cells(lte_intra, l);
            parsed_any = true;
        }

        struct json_object *lte_inter = NULL;
        if (json_object_object_get_ex(root, "interfrequency_lte_info", &lte_inter) &&
            json_object_is_type(lte_inter, json_type_object)) {
            out->has_lte = true;
            parse_lte_cells(lte_inter, &out->lte_inter);
            parsed_any = true;
        }

        json_object_put(root);
        json_tokener_free(tok);
#ifdef LTE_MODEM_DEBUG
        fprintf(stderr, "[lte_modem] strict JSON parsed_any=%d\n", parsed_any ? 1 : 0);
#endif
    } else {
#ifdef LTE_MODEM_DEBUG
        enum json_tokener_error jerr = json_tokener_get_error(tok);
        fprintf(stderr, "[lte_modem] strict JSON error: %s\n", json_tokener_error_desc(jerr));
#endif
        json_tokener_free(tok);
    }

    /* If strict JSON failed or gave nothing useful — fallback tolerant parser */
    if (!parsed_any) {
        bool any = false;
        memset(&out->umts, 0, sizeof(out->umts));
        memset(&out->lte_intra, 0, sizeof(out->lte_intra));
        memset(&out->lte_inter, 0, sizeof(out->lte_inter));

        if (fallback_parse_umts(text, &out->umts)) {
            out->has_umts = true;
            any = true;
        }
        if (fallback_parse_lte(text, &out->lte_intra, &out->lte_inter)) {
            out->has_lte = (out->lte_intra.neigh_count > 0 || out->lte_inter.neigh_count > 0
                            || out->lte_intra.tracking_area_code != 0);
            any = true;
        }

#ifdef LTE_MODEM_DEBUG
        fprintf(stderr, "[lte_modem] fallback any=%d umts_neigh=%d lte_intra_neigh=%d lte_inter_neigh=%d\n",
                any ? 1 : 0, out->umts.neigh_count, out->lte_intra.neigh_count, out->lte_inter.neigh_count);
#endif

        if (!any) {
            free(text);
            return -1;
        }
    }

    free(text);
    return 0;
}

void lte_modem_print_cell_location(const struct lte_cellinfo *ci)
{
    if (ci->has_umts) {
        const struct lte_umts_info *u = &ci->umts;
        printf("UMTS LAC=%d CID=%d CH=%d PSC=%d RSCP=%d ECIO=%d\n",
               u->location_area_code, u->cell_id, u->channel,
               u->primary_scrambling_code, u->rscp, u->ecio);
        for (int i = 0; i < u->neigh_count; i++) {
            printf("  N%d: CH=%d PSC=%d RSCP=%d ECIO=%d\n",
                   i, u->neigh[i].channel, u->neigh[i].psc,
                   u->neigh[i].rscp, u->neigh[i].ecio);
        }
    }

    if (ci->has_lte) {
        const struct lte_lte_info *l = &ci->lte_intra;
        printf("LTE TAC=%d eNB=%d CID=%d CH=%d BAND=%d Freq=%d %s (Serving PCI=%d)\n",
               l->tracking_area_code, l->enodeb_id, l->cell_id,
               l->channel, l->band, l->frequency, l->duplex, l->serving_cell_id);
        for (int i = 0; i < l->neigh_count; i++) {
            printf("  Intra N%d: PCI=%d RSRP=%.1f RSRQ=%.1f RSSI=%.1f\n",
                   i, l->neigh[i].physical_cell_id, l->neigh[i].rsrp,
                   l->neigh[i].rsrq, l->neigh[i].rssi);
        }

        /* Inter neighbours (if present) */
        const struct lte_lte_info *li = &ci->lte_inter;
        for (int i = 0; i < li->neigh_count; i++) {
            printf("  Inter N%d: PCI=%d RSRP=%.1f RSRQ=%.1f RSSI=%.1f\n",
                   i, li->neigh[i].physical_cell_id, li->neigh[i].rsrp,
                   li->neigh[i].rsrq, li->neigh[i].rssi);
        }
    }
}
