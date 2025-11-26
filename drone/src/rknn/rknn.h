/* SPDX-License-Identifier: GPL-2.0-only */
/**
 * Copyright (C) 2025 Vitaliy N <vitaliy.nimych@gmail.com>
 */
#ifndef RKNN_H
#define RKNN_H

typedef struct {
    int width;
    int height;
    int channel;
} rknn_model_info_t;

void rknn_thread_start(void);
void rknn_thread_stop(void);
rknn_model_info_t* rknn_get_model_info(void);

#endif //RKNN_H
