# VD-Link Telemetry System

## Overview

Ця система дозволяє відправляти телеметричні дані (CRSF LinkStatistics) з дрона на ground station через UDP сокет на порту 5613.

## Структура даних

```c
typedef struct {
    uint8_t uplink_rssi_1;      // RSSI антени 1 (dBm + 130)
    uint8_t uplink_rssi_2;      // RSSI антени 2 (dBm + 130) 
    uint8_t uplink_link_quality; // Якість uplink (0-100%)
    int8_t  uplink_snr;         // SNR uplink (dB)
    uint8_t active_antenna;     // Активна антена (0 або 1)
    uint8_t rf_mode;           // RF режим (0-7)
    uint8_t uplink_tx_power;   // TX потужність (0-8, фактична = 2^value mW)
    uint8_t downlink_rssi;     // RSSI downlink (dBm + 130)
    uint8_t downlink_link_quality; // Якість downlink (0-100%)
    int8_t  downlink_snr;      // SNR downlink (dB)
} __attribute__((packed)) crsf_link_statistics_t;
```

## Використання в коді

### 1. Ініціалізація

```c
#include "fc_conn/fc_conn.h"

// Ініціалізація з localhost
telemetry_init(NULL);

// Або з конкретною IP адресою
telemetry_init("192.168.1.100");
```

### 2. Оновлення даних

```c
// Оновлення статистики посилання
telemetry_update_link_stats(
    130 - 50,   // uplink_rssi_1 (-50 dBm)
    130 - 52,   // uplink_rssi_2 (-52 dBm)
    95,         // uplink_quality (95%)
    12,         // uplink_snr (12 dB)
    130 - 48,   // downlink_rssi (-48 dBm)
    98,         // downlink_quality (98%)
    15          // downlink_snr (15 dB)
);

// Оновлення RF параметрів
telemetry_update_rf_params(
    0,          // active_antenna (антена 0)
    4,          // rf_mode (150Hz)
    6           // tx_power (64 mW)
);
```

### 3. Відправка даних

```c
// Відправка поточних даних
if (telemetry_send_link_stats() == 0) {
    printf("Telemetry sent successfully\\n");
}
```

### 4. Зміна цілі

```c
// Зміна IP адреси отримувача
telemetry_set_target_ip("192.168.1.200");
```

### 5. Очистка

```c
telemetry_cleanup();
```

## Компіляція та тестування

### 1. Компіляція

```bash
make -f Makefile.telemetry all
```

### 2. Запуск receiver (в одному терміналі)

```bash
./telemetry_receiver
```

### 3. Запуск тесту (в іншому терміналі)

```bash
# Відправка на localhost
./test_telemetry

# Відправка на конкретну IP
./test_telemetry 192.168.1.100
```

## RF Режими

| Значення | Частота | Опис |
|----------|---------|------|
| 0 | 4Hz | Найнижча частота |
| 1 | 25Hz | Низька частота |
| 2 | 50Hz | Середня-низька |
| 3 | 100Hz | Середня |
| 4 | 150Hz | Середня-висока |
| 5 | 200Hz | Висока |
| 6 | 250Hz | Дуже висока |
| 7 | 500Hz | Максимальна |

## Конвертація значень

### RSSI
- Зберігається як: `dBm + 130`
- Конвертація назад: `rssi_value - 130`
- Приклад: -50 dBm → 80 (130-50)

### TX Power
- Зберігається як: індекс степені 2
- Фактична потужність: `2^tx_power` mW
- Приклад: 6 → 64 mW (2^6)

## Інтеграція

Система автоматично ініціалізується при виклику `connect_to_fc()` та очищається при `disconnect_from_fc()`.

Для періодичної відправки телеметрії додайте виклики в ваш основний цикл:

```c
// В основному циклі (наприклад, кожні 100ms)
telemetry_update_link_stats(current_rssi1, current_rssi2, ...);
telemetry_send_link_stats();
```