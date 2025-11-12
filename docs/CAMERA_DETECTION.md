# Camera Detection and Priority Management System

## Огляд

Ця система забезпечує надійне та детальне визначення камер у дронах з автоматичним управлінням пріоритетами. Система підтримує як CSI (Camera Serial Interface), так і USB камери з можливістю автоматичного вибору найкращої камери за заданими критеріями.

## Функціональні можливості

### 1. Типи камер
- **CSI камери**: Камери з інтерфейсом CSI, підключені через GPIO
- **USB камери**: UVC-сумісні камери, підключені через USB
- **Thermal камери**: Теплові камери (поки не реалізовано)
- **Fake камери**: Віртуальні камери для тестування

### 2. Підтримувані сенсори з пріоритетами
- **IMX415**: Sony IMX415 CMOS сенсор (Високий пріоритет, якість 95/100)
- **OV13850**: OmniVision OV13850 CMOS сенсор (Високий пріоритет, якість 90/100)
- **IMX307**: Sony IMX307 CMOS сенсор (Середній пріоритет, якість 80/100)
- **OV4689**: OmniVision OV4689 CMOS сенсор (Середній пріоритет, якість 75/100)
- **OV5647**: OmniVision OV5647 CMOS сенсор (Середній пріоритет, якість 70/100)
- **UVC Generic**: Загальні UVC USB камери (Низький пріоритет, якість 60/100)

### 3. Система пріоритетів
- **CAMERA_PRIORITY_HIGH (1)**: Високоякісні CSI сенсори (IMX415, OV13850)
- **CAMERA_PRIORITY_MEDIUM (2)**: Стандартні CSI сенсори (IMX307, OV4689, OV5647)
- **CAMERA_PRIORITY_LOW (3)**: USB камери
- **CAMERA_PRIORITY_FALLBACK (4)**: Резервні/невідомі камери

### 3. Методи детекції

#### CSI камери:
1. **Device Tree перевірка**: Сканування `/sys/firmware/devicetree/base/` для визначення наявних сенсорів
2. **V4L2 детекція**: Перевірка відеопристроїв через `/dev/video*`
3. **Driver matching**: Розпізнавання типу сенсора за назвою драйвера та картки

#### USB камери:
1. **V4L2 UVC детекція**: Перевірка bus_info на наявність "usb"
2. **Capability перевірка**: Перевірка підтримки стримінгу
3. **Resolution enumeration**: Отримання підтримуваних роздільних здатностей

#### Системна діагностика:
1. **sysfs перевірка**: Перевірка існування пристроїв через файлову систему
2. **Device capability**: Детальна інформація про можливості пристрою
3. **Resolution detection**: Автоматичне визначення підтримуваних роздільних здатностей

## API Функції

### Camera Manager (Рекомендований підхід)

```c
// Ініціалізація менеджера камер
int camera_manager_init(camera_manager_t *manager);

// Отримання камер за пріоритетом
camera_info_t* camera_manager_get_primary(camera_manager_t *manager);
camera_info_t* camera_manager_get_secondary(camera_manager_t *manager);

// Пошук за типом або сенсором
camera_info_t* camera_manager_get_by_type(camera_manager_t *manager, camera_type_t type);
camera_info_t* camera_manager_get_by_sensor(camera_manager_t *manager, camera_sensor_t sensor);

// Управління пріоритетами
int camera_manager_select_best(camera_manager_t *manager, camera_type_t preferred_type);

// Вивід інформації
void camera_manager_print_all(camera_manager_t *manager);
```

### Основні функції детекції (Legacy)

```c
// Визначити тип основної камери (legacy function)
camera_type_t camera_detect_type(void);

// Знайти всі доступні камери
int camera_detect_all(camera_info_t *cameras, int max_cameras);

// Отримати детальну інформацію про конкретну камеру
int camera_get_info(int device_index, camera_info_t *info);
```

### Специфічні функції

```c
// CSI камери
int camera_detect_csi(camera_info_t *cameras, int max_cameras);
bool camera_is_csi_available(void);

// USB камери
int camera_detect_usb(camera_info_t *cameras, int max_cameras);
bool camera_is_usb_available(void);
```

### Низькорівневі функції

```c
// Системні перевірки
bool camera_check_device_tree(const char *sensor_name);
bool camera_check_sysfs_device(const char *device_path);
bool camera_test_v4l2_device(const char *device_path, camera_info_t *info);
```

## Структури даних

### Розширена інформація про камеру
```c
typedef struct {
    camera_type_t type;                    // Тип камери
    camera_sensor_t sensor;                // Тип сенсора
    char name[MAX_CAMERA_NAME_LEN];        // Назва камери
    char device_path[MAX_DEVICE_PATH_LEN]; // Шлях до пристрою
    char driver_name[MAX_CAMERA_NAME_LEN]; // Назва драйвера
    char bus_info[MAX_CAMERA_NAME_LEN];    // Інформація про шину
    uint32_t device_id;                    // ID пристрою
    uint32_t vendor_id;                    // ID виробника (USB)
    uint32_t product_id;                   // ID продукту (USB)
    bool is_available;                     // Доступність камери
    bool supports_streaming;               // Підтримка стримінгу
    camera_priority_t priority;            // Пріоритет камери
    uint8_t quality_score;                 // Оцінка якості від 0 до 100
    camera_resolution_t supported_resolutions[MAX_SUPPORTED_RESOLUTIONS];
    int num_resolutions;                   // Кількість підтримуваних роздільних здатностей
} camera_info_t;
```

### Менеджер камер
```c
typedef struct {
    camera_info_t cameras[MAX_CAMERAS];    // Масив всіх знайдених камер
    int count;                             // Кількість знайдених камер
    int primary_camera_index;              // Індекс обраної основної камери
    int secondary_camera_index;            // Індекс резервної камери
} camera_manager_t;
```

## Використання

### Рекомендований підхід з Camera Manager

```c
#include "camera/camera.h"

int main() {
    // Ініціалізація менеджера камер
    camera_manager_t camera_manager;
    int cameras_found = camera_manager_init(&camera_manager);
    
    if (cameras_found <= 0) {
        printf("No cameras detected!\n");
        return -1;
    }
    
    // Показати всі камери з пріоритетами
    camera_manager_print_all(&camera_manager);
    
    // Отримати основну камеру (автоматично обрана за пріоритетом)
    camera_info_t *primary = camera_manager_get_primary(&camera_manager);
    if (primary) {
        printf("Using primary camera: %s\n", primary->name);
        printf("Priority: %s, Quality: %d/100\n", 
               priority_to_string(primary->priority), 
               primary->quality_score);
        
        // Ініціалізувати камеру для стрімінгу
        if (primary->type == CAMERA_CSI) {
            // camera_csi_init(primary);
        } else if (primary->type == CAMERA_USB) {
            // camera_usb_init(primary);
        }
    }
    
    // Отримати резервну камеру
    camera_info_t *secondary = camera_manager_get_secondary(&camera_manager);
    if (secondary) {
        printf("Backup camera available: %s\n", secondary->name);
    }
    
    return 0;
}
```

### Пошук конкретного типу камери

```c
// Знайти найкращу CSI камеру
camera_info_t *csi_camera = camera_manager_get_by_type(&manager, CAMERA_CSI);
if (csi_camera) {
    printf("Found CSI camera: %s (priority=%s)\n", 
           csi_camera->name, priority_to_string(csi_camera->priority));
}

// Знайти конкретний сенсор
camera_info_t *imx415 = camera_manager_get_by_sensor(&manager, SENSOR_IMX415);
if (imx415) {
    printf("Found high-quality IMX415 sensor\n");
}
```

### Зміна пріоритетів під час роботи

```c
// Переключитися на USB камери (наприклад, при несправності CSI)
camera_manager_select_best(&manager, CAMERA_USB);

camera_info_t *new_primary = camera_manager_get_primary(&manager);
if (new_primary) {
    printf("Switched to: %s\n", new_primary->name);
}
```

### Базове використання (Legacy)

```c
#include "camera/camera.h"

int main() {
    // Просте визначення типу камери
    camera_type_t type = camera_detect_type();
    printf("Camera type: %d\n", type);
    
    // Детальне сканування всіх камер
    camera_info_t cameras[10];
    int found = camera_detect_all(cameras, 10);
    
    for (int i = 0; i < found; i++) {
        printf("Camera %d: %s (%s)\n", 
               i, cameras[i].name, cameras[i].device_path);
    }
    
    return 0;
}
```

### Перевірка конкретного типу

```c
// Перевірити наявність CSI камер
if (camera_is_csi_available()) {
    printf("CSI camera is available\n");
    
    camera_info_t csi_cameras[5];
    int csi_count = camera_detect_csi(csi_cameras, 5);
    // Обробити CSI камери...
}

// Перевірити наявність USB камер
if (camera_is_usb_available()) {
    printf("USB camera is available\n");
    
    camera_info_t usb_cameras[5];  
    int usb_count = camera_detect_usb(usb_cameras, 5);
    // Обробити USB камери...
}
```

## Тестування

Для тестування системи детекції використовуйте програми:

### 1. Базовий тест камер
```bash
# Збірка
cmake --build build-drone --parallel

# Запуск базового тесту
./build-drone/drone/camera-test
```

### 2. Тест менеджера камер з пріоритетами
```bash
# Запуск розширеного тесту
./build-drone/drone/camera-manager-example
```

### 3. Ручний тест на пристрої
```bash
# Копіювання на дрон та запуск
scp build-drone/drone/camera-manager-example root@<drone-ip>:
ssh root@<drone-ip> './camera-manager-example'
```

## Поліпшення у порівнянні з попередньою версією

1. **Система пріоритетів**: Автоматичний вибір найкращої камери за пріоритетом та якістю
2. **Camera Manager**: Централізоване управління всіма камерами з можливістю переключення
3. **Quality scoring**: Оцінка якості камер від 0 до 100 балів
4. **Primary/Secondary**: Автоматичний вибір основної та резервної камери
5. **Багатосенсорна підтримка**: Підтримка різних типів сенсорів з індивідуальними пріоритетами
6. **USB камери**: Повна підтримка USB UVC камер з правильним пріоритетом
7. **Детальна інформація**: Розширена структура з детальною інформацією про камери
8. **Системна діагностика**: Перевірка через device tree і sysfs
9. **Resolution detection**: Автоматичне визначення підтримуваних роздільних здатностей
10. **Backward compatibility**: Збереження сумісності з існуючим кодом
11. **Надійність**: Поліпшені алгоритми детекції з кращою обробкою помилок
12. **Flexible selection**: Можливість вибору камери за типом, сенсором або пріоритетом

## Алгоритм роботи Camera Manager

### Ініціалізація:
1. Сканування всіх доступних камер (CSI + USB)
2. Визначення типу, сенсора та призначення пріоритету кожній камері
3. Сортування камер за пріоритетом та якістю
4. Автоматичний вибір primary та secondary камери

### Вибір найкращої камери:
1. **Перший прохід**: Пошук камер бажаного типу з найвищим пріоритетом
2. **Другий прохід**: Якщо не знайдено, вибір будь-якої доступної камери
3. **Третій прохід**: Вибір резервної камери з решти доступних
4. **Сортування**: За пріоритетом (1=найвищий) та якістю (100=найкраща)

### Критерії вибору:
- **Доступність**: Камера повинна бути доступна (`is_available = true`)
- **Streaming**: Камера повинна підтримувати стрімінг (`supports_streaming = true`)
- **Пріоритет**: Нижче число = вищий пріоритет
- **Якість**: Вище число = краща якість
- **Тип**: Відповідність бажаному типу (CSI/USB)

Ця система забезпечує максимально коректне та надійне визначення камер з автоматичним управлінням пріоритетами для оптимального вибору найкращої доступної камери.