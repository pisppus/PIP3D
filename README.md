# PIP3D API

## Описание

**PIP3D** — высокооптимизированный 3D-движок для микроконтроллеров ESP32, специально разработанный для работы с дисплеями ST7789. Поддерживает перспективную и ортографическую проекцию, динамическое освещение, планарные тени, инстансинг объектов и продвинутый frustum culling.

### 🎯 Что умеет движок:
- ✅ Рендеринг 3D-объектов с освещением (Blinn-Phong модель)
- ✅ Z-буферизация (правильное перекрытие объектов)
- ✅ Планарные тени на плоскости
- ✅ Frustum culling (автоматическое отсечение невидимых объектов)
- ✅ Инстансинг (эффективная отрисовка множества копий)
- ✅ Скайбокс с градиентным небом
- ✅ Цветовая температура света (2500K-8000K)
- ✅ Перспективная и ортографическая проекция
- ✅ Встроенный растровый шрифт для текста

## Быстрый старт

### Минимальный рабочий пример

```cpp
#include <pip3D.h>
using namespace pip3D;

Renderer renderer;  // Главный объект рендеринга
Cube* cube;         // Указатель на 3D-куб

void setup() {
    Serial.begin(115200);
    
    // ⚠️ ОБЯЗАТЕЛЬНО! Инициализация таблиц sin/cos для быстрых вычислений
    FastMath::init();
    
    // Настройка дисплея ST7789
    DisplayConfig config;
    config.width = 240;      // Ширина экрана в пикселях
    config.height = 320;     // Высота экрана в пикселях
    config.cs_pin = 10;      // Chip Select (SS)
    config.dc_pin = 9;       // Data/Command
    config.rst_pin = 8;      // Reset
    config.bl_pin = -1;      // Backlight (-1 = не используется)
    config.spi_freq = 80000000;  // 80 МГц (максимум для стабильности)
    
    // Инициализация рендерера
    if (!renderer.init(config)) {
        Serial.println("Ошибка инициализации!");
        while(1);  // Останавливаем программу
    }
    
    // Настройка камеры
    Camera& cam = renderer.getCamera();
    cam.position = Vector3(0, 2, -5);  // Камера на высоте 2, отодвинута назад на 5
    cam.target = Vector3(0, 0, 0);     // Смотрит в центр (0,0,0)
    cam.up = Vector3(0, 1, 0);         // Вектор "вверх" (обычно не меняется)
    cam.fov = 60.0f;                   // Угол обзора 60 градусов
    
    // Создание красного куба размером 2x2x2
    cube = new Cube(2.0f, Color::RED);
    cube->setPosition(0, 0, 0);  // Куб в центре координат
    
    // Включение дневного неба
    renderer.setSkyboxType(SKYBOX_DAY);
    
    // Настройка освещения (солнце светит сверху-слева)
    renderer.setMainDirectionalLight(
        Vector3(-0.5f, -1.0f, -0.5f),  // Направление света
        Color::WHITE,                   // Белый свет
        1.0f                            // Полная интенсивность
    );
}

void loop() {
    // Вращение куба
    static float angle = 0;
    cube->setRotation(angle, angle * 0.7f, 0);  // Вращение по X и Y
    angle += 1.0f;  // Скорость вращения (градусы за кадр)
    
    // Рендеринг кадра
    renderer.beginFrame();    // Начало кадра (очистка)
    renderer.drawMesh(cube);  // Отрисовка куба
    renderer.endFrame();      // Конец кадра (отправка на дисплей)
    
    // Вывод FPS в Serial Monitor
    Serial.printf("FPS: %.1f\n", renderer.getFPS());
}
```

### 📝 Пошаговое объяснение:

1. **`FastMath::init()`** — создает таблицы для быстрого вычисления sin/cos. Без этого движок будет работать медленно!
2. **`DisplayConfig`** — структура с настройками дисплея (пины, разрешение, частота SPI)
3. **`renderer.init()`** — инициализирует дисплей, выделяет память для framebuffer и Z-buffer
4. **`Camera`** — определяет, откуда и куда мы смотрим на сцену
5. **`Cube`** — готовый примитив (куб). Можно создавать свои меши через `Mesh`
6. **`beginFrame()/endFrame()`** — обязательная пара для каждого кадра

---

## 🏗️ Основные классы

### 1. DisplayConfig — Конфигурация дисплея

Структура с настройками дисплея ST7789.

#### Поля:

```cpp
struct DisplayConfig {
    uint16_t width;      // Ширина дисплея в пикселях (обычно 240 или 320)
    uint16_t height;     // Высота дисплея в пикселях (обычно 320 или 240)
    int8_t cs_pin;       // Chip Select (SS) пин
    int8_t dc_pin;       // Data/Command пин
    int8_t rst_pin;      // Reset пин
    int8_t bl_pin;       // Backlight пин (-1 если не используется)
    uint32_t spi_freq;   // Частота SPI в Герцах
};
```

#### Пример использования:

```cpp
DisplayConfig config;
config.width = 240;           // Ширина
config.height = 320;          // Высота
config.cs_pin = 10;           // CS подключен к GPIO 10
config.dc_pin = 9;            // DC подключен к GPIO 9
config.rst_pin = 8;           // RST подключен к GPIO 8
config.bl_pin = -1;           // Подсветка управляется отдельно
config.spi_freq = 80000000;   // 80 МГц (рекомендуется для ESP32-S3)
```

#### 💡 Рекомендации по частоте SPI:
- **40 МГц** — безопасно для любых проводов
- **60 МГц** — хорошо для коротких проводов (<10см)
- **80 МГц** — максимум для ESP32-S3, короткие провода обязательны
- **Выше 80 МГц** — нестабильно, не рекомендуется

---

### 2. Renderer — Главный класс рендеринга

Координирует все модули: камеру, освещение, Z-buffer, тени, растеризацию.

#### Инициализация

**`bool init(const DisplayConfig& cfg)`**

**Что делает:**
- Инициализирует дисплей ST7789
- Выделяет память для framebuffer (240x320x2 = 150 КБ)
- Выделяет память для Z-buffer (240x320x2 = 150 КБ)
- Настраивает SPI на максимальную скорость
- Создает камеру по умолчанию

**Параметры:**
- `cfg` — структура `DisplayConfig` с настройками дисплея

**Возвращает:**
- `true` — успешная инициализация
- `false` — ошибка (нехватка памяти или проблема с дисплеем)

**⚠️ Важно:**
- Вызывать **ОДИН РАЗ** в `setup()`
- Перед вызовом обязательно выполнить `FastMath::init()`
- Проверяйте возвращаемое значение!

**Пример:**
```cpp
if (!renderer.init(config)) {
    Serial.println("Ошибка: не удалось инициализировать рендерер!");
    Serial.println("Возможные причины:");
    Serial.println("- Нехватка памяти (нужно ~300 КБ)");
    Serial.println("- Неправильные пины дисплея");
    Serial.println("- Проблемы с SPI");
    while(1);  // Останавливаем программу
}
Serial.println("Рендерер инициализирован успешно!");
```

**`void beginFrame()`**

**Что делает:**
- Очищает Z-buffer (сбрасывает глубину всех пикселей)
- Рисует скайбокс (градиентное небо) ИЛИ заливает фон цветом
- Обновляет матрицы камеры (если камера двигалась)
- Обновляет frustum для culling
- Запускает таймер производительности

**⚠️ Важно:**
- Вызывать **ПЕРЕД** любой отрисовкой в каждом кадре
- Парная функция с `endFrame()`

**Пример:**
```cpp
void loop() {
    renderer.beginFrame();  // ← Всегда первое!
    
    // Здесь вся отрисовка:
    renderer.drawMesh(cube);
    renderer.drawMesh(sphere);
    renderer.drawText(10, 10, "Hello");
    
    renderer.endFrame();    // ← Всегда последнее!
}
```

---

**`void endFrame()`**

**Что делает:**
- Отправляет framebuffer на дисплей через SPI (DMA)
- Останавливает таймер производительности
- Вычисляет FPS

**⚠️ Важно:**
- Вызывать **ПОСЛЕ** всей отрисовки
- Занимает ~5-10 мс (зависит от частоты SPI)

**💡 Оптимизация:**
Если нужен максимальный FPS, используйте частоту SPI 80 МГц и короткие провода (<5 см).

#### Отрисовка объектов

**`void drawMesh(Mesh* mesh)`**

**Что делает:**
- Проверяет видимость объекта (`mesh->isVisible()`)
- Обновляет трансформацию объекта (позиция, вращение, масштаб)
- Выполняет frustum culling (отсекает объекты вне камеры)
- Для каждого треугольника:
  - Вычисляет освещение (Blinn-Phong модель)
  - Проецирует вершины на экран
  - Растеризует с Z-буферизацией
  - Применяет dithering для сглаживания

**Параметры:**
- `mesh` — указатель на объект `Mesh` или его наследников (`Cube`, `Sphere`, и т.д.)

**⚠️ Важно:**
- Меш должен быть валидным (не `nullptr`)
- Вызывать между `beginFrame()` и `endFrame()`
- Невидимые объекты (`setVisible(false)`) пропускаются автоматически

**Пример:**
```cpp
Cube* cube = new Cube(2.0f, Color::BLUE);
cube->setPosition(0, 0, 0);
cube->setRotation(45, 30, 0);

renderer.beginFrame();
renderer.drawMesh(cube);  // Рисуем куб
renderer.endFrame();
```

---

**`void drawMeshInstance(MeshInstance* instance)`**

**Что делает:**
То же самое, что `drawMesh()`, но для инстанса (легковесной копии меша).

**Параметры:**
- `instance` — указатель на `MeshInstance`

**💡 Когда использовать:**
Когда нужно нарисовать много одинаковых объектов (деревья, камни, враги).
Инстансы экономят память — геометрия хранится один раз, а трансформации индивидуальные.

**Пример:**
```cpp
Sphere* sphere = new Sphere(0.5f, 8, 6);  // Один меш

// Создаем 10 копий
MeshInstance* instances[10];
for (int i = 0; i < 10; i++) {
    instances[i] = new MeshInstance(sphere);
    instances[i]->setPosition(i * 2.0f, 0, 0);
    instances[i]->setColor(Color::fromRGB888(255, i * 25, 0));
}

// Рендеринг
renderer.beginFrame();
for (int i = 0; i < 10; i++) {
    renderer.drawMeshInstance(instances[i]);
}
renderer.endFrame();
```

---

**`void drawInstances(InstanceManager& manager)`**

**Что делает:**
- Получает все видимые инстансы (frustum culling)
- Сортирует по расстоянию от камеры (дальние → ближние)
- Рендерит каждый инстанс

**Параметры:**
- `manager` — ссылка на `InstanceManager`

**🚀 Оптимизация:**
Автоматический frustum culling и сортировка. Используйте для сцен с сотнями объектов!

**Пример:**
```cpp
InstanceManager manager;
Cube* cube = new Cube(1.0f);

// Создаем 100 кубиков в сетке
for (int x = 0; x < 10; x++) {
    for (int z = 0; z < 10; z++) {
        auto inst = manager.createInstance(cube);
        inst->setPosition(x * 2.0f, 0, z * 2.0f);
        inst->setColor(Color::fromRGB888(x * 25, z * 25, 128));
    }
}

// Рендеринг (автоматический culling!)
renderer.beginFrame();
renderer.drawInstances(manager);  // Нарисует только видимые
renderer.endFrame();
```

#### Система теней

**`void setShadowsEnabled(bool enabled)`**

**Что делает:**
Включает или выключает глобальную систему теней.

**Параметры:**
- `enabled` — `true` для включения, `false` для выключения

**💡 Производительность:**
Тени добавляют ~20-30% нагрузки. Выключайте, если нужен максимальный FPS.

**Пример:**
```cpp
renderer.setShadowsEnabled(true);   // Включить тени
renderer.setShadowOpacity(0.4f);    // Полупрозрачные
renderer.setShadowColor(Color::BLACK);  // Черные
```

---

**`void drawMeshShadow(Mesh* mesh)`**

**Что делает:**
- Проверяет, должен ли объект отбрасывать тень
- Вычисляет матрицу проекции тени на плоскость (алгоритм OpenGL Advanced 97)
- Проецирует все треугольники объекта на плоскость
- Рендерит тень с альфа-смешиванием

**Параметры:**
- `mesh` — указатель на меш

**⚠️ Важно:**
- Тени должны быть включены (`setShadowsEnabled(true)`)
- Рисовать тень **ПЕРЕД** объектом (чтобы тень была под объектом)
- Тени проецируются на плоскость Y = -1.5 по умолчанию

**Пример:**
```cpp
renderer.setShadowsEnabled(true);
renderer.setShadowOpacity(0.5f);  // 50% прозрачность

renderer.beginFrame();
// Сначала тени, потом объекты!
renderer.drawMeshShadow(cube);
renderer.drawMeshShadow(sphere);
renderer.drawMesh(cube);
renderer.drawMesh(sphere);
renderer.endFrame();
```

---

**`void setShadowOpacity(float opacity)`**

**Что делает:**
Устанавливает прозрачность теней.

**Параметры:**
- `opacity` — значение от 0.0 (полностью прозрачная) до 1.0 (непрозрачная)

**💡 Рекомендации:**
- **0.3-0.4** — мягкие реалистичные тени (дневной свет)
- **0.5-0.6** — более выраженные тени (яркое солнце)
- **0.7-1.0** — очень темные тени (редко используется)

**Пример:**
```cpp
// Мягкие утренние тени
renderer.setShadowOpacity(0.3f);

// Резкие полуденные тени
renderer.setShadowOpacity(0.6f);
```

---

**`void setShadowColor(const Color& color)`**

**Что делает:**
Устанавливает цвет теней.

**Параметры:**
- `color` — цвет тени

**💡 Рекомендации:**
- **`Color::BLACK`** — классические черные тени (по умолчанию)
- **Темно-синий** — реалистичные тени при дневном свете
- **Темно-фиолетовый** — тени при закате

**Пример:**
```cpp
// Черные тени
renderer.setShadowColor(Color::BLACK);

// Реалистичные синеватые тени (дневной свет)
renderer.setShadowColor(Color::fromRGB888(20, 20, 40));

// Теплые тени при закате
renderer.setShadowColor(Color::fromRGB888(40, 20, 30));
```

---

**`void setShadowOffset(float offset)`**

**Что делает:**
Устанавливает смещение тени от плоскости (для избежания z-fighting).

**Параметры:**
- `offset` — смещение в единицах мира

**💡 Рекомендации:**
- **0.01-0.03** — минимальное смещение (обычно достаточно)
- **0.05-0.1** — если видны артефакты (мерцание)

**Пример:**
```cpp
renderer.setShadowOffset(0.02f);  // Небольшое смещение
```

---

**`void setShadowPlane(const PlanarShadow::Plane& plane)`**

**Что делает:**
Устанавливает плоскость, на которую проецируются тени.

**Параметры:**
- `plane` — структура `PlanarShadow::Plane`

**Пример:**
```cpp
// Плоскость пола на высоте Y = 0
auto groundPlane = PlanarShadow::createGroundPlane(0.0f);
renderer.setShadowPlane(groundPlane);

// Наклонная плоскость
PlanarShadow::Plane customPlane(
    Vector3(0, 1, 0),  // Нормаль (вверх)
    -2.0f              // Расстояние от начала координат
);
renderer.setShadowPlane(customPlane);
```

#### Камера

**`Camera& getCamera()`**
- Возвращает активную камеру

**`int createCamera()`**
- Создает новую камеру (макс. 4)

**`void setActiveCamera(int index)`**
- Переключает камеру

#### Освещение

**`void setMainDirectionalLight(const Vector3& direction, const Color& color, float intensity = 1.0f)`**
- Направленный свет (солнце)

**`void setMainPointLight(const Vector3& position, const Color& color, float intensity = 1.0f, float range = 10.0f)`**
- Точечный свет (лампа)

**`void setLightTemperature(float kelvin)`**
- Цвет света по температуре
- 2500K — закат (оранжевый)
- 5500K — день (белый)
- 8000K — луна (голубой)

**`int addLight(const Light& light)`**
- Добавляет источник света (макс. 4)

#### Скайбокс

**`void setSkyboxType(SkyboxType type)`**
- Типы: `SKYBOX_DAY`, `SKYBOX_SUNSET`, `SKYBOX_NIGHT`, `SKYBOX_DAWN`, `SKYBOX_OVERCAST`

**`void setSkyboxWithLighting(SkyboxType type)`**
- Устанавливает скайбокс + автоматическое освещение

**`void setSkyboxEnabled(bool enabled)`**
- Включает/выключает скайбокс

#### Текст

**`void drawText(int16_t x, int16_t y, const char* text, uint16_t color = 0xFFFF)`**
- Рисует текст

**`void drawTextAdaptive(int16_t x, int16_t y, const char* text)`**
- Текст с автовыбором цвета

**`int16_t getTextWidth(const char* text)`**
- Ширина текста в пикселях

#### Производительность

**`float getFPS() const`**
- Текущий FPS

**`float getAverageFPS() const`**
- Средний FPS за 60 кадров

**`uint32_t getFrameTime() const`**
- Время кадра в микросекундах

---

### Camera

Управляет видом сцены.

#### Поля

```cpp
Vector3 position;        // Позиция камеры
Vector3 target;          // Точка взгляда
Vector3 up;              // Вектор "вверх"
float fov;               // Угол обзора (градусы)
float nearPlane;         // Ближняя плоскость
float farPlane;          // Дальняя плоскость
```

#### Методы

**`void setPerspective(float fov = 60.0f, float near = 0.1f, float far = 100.0f)`**
- Перспективная проекция

**`void setOrthographic(float width = 10.0f, float height = 10.0f, float near = 0.1f, float far = 100.0f)`**
- Ортографическая проекция

**`void markDirty()`**
- Пересчитать матрицы

---

### Mesh

Базовый класс 3D-объектов.

#### Трансформация

**`void setPosition(float x, float y, float z)`**
- Устанавливает позицию

**`void setRotation(float x, float y, float z)`**
- Вращение (углы Эйлера в градусах)

**`void setScale(float x, float y, float z)`**
- Масштаб

**`void translate(float x, float y, float z)`**
- Смещение

**`void rotate(float x, float y, float z)`**
- Относительное вращение

#### Управление

**`void setMeshColor(const Color& color)`**
- Цвет меша

**`void setVisible(bool v)`**
- Видимость

**`bool isVisible() const`**
- Проверка видимости

#### Создание меша вручную

**`uint16_t addVertex(const Vector3& pos)`**
- Добавляет вершину

**`bool addFace(uint16_t v0, uint16_t v1, uint16_t v2)`**
- Добавляет треугольник (CCW winding!)

**`void finalizeNormals()`**
- Вычисляет нормали (обязательно!)

**`void calculateBoundingSphere()`**
- Вычисляет bounding sphere

---

### Готовые примитивы

#### Cube

```cpp
Cube(float size = 1.0f, const Color& color = Color::WHITE)
```

#### Sphere

```cpp
Sphere(float radius = 1.0f, uint8_t segments = 8, uint8_t rings = 6, const Color& color = Color::WHITE)
```
- Оптимально: 8-12 сегментов для ESP32

#### Cylinder

```cpp
Cylinder(float radius = 1.0f, float height = 2.0f, uint8_t segments = 16, const Color& color = Color::WHITE)
```

#### Cone

```cpp
Cone(float radius = 1.0f, float height = 2.0f, uint8_t segments = 16, const Color& color = Color::WHITE)
```

#### Pyramid

```cpp
Pyramid(float size = 1.0f, const Color& color = Color::WHITE)
```

#### Plane

```cpp
Plane(float width = 2.0f, float depth = 2.0f, uint8_t subdivisions = 1, const Color& color = Color::WHITE)
```
- Идеально для пола/стен

#### Capsule

```cpp
Capsule(float radius = 1.0f, float height = 2.0f, uint8_t segments = 12, uint8_t rings = 6, const Color& color = Color::WHITE)
```

#### TrefoilKnot

```cpp
TrefoilKnot(float scale = 1.0f, uint8_t segments = 64, uint8_t tubeSegments = 12, const Color& color = Color::WHITE)
```

---

### 3. MeshInstance — Инстанс меша

Легковесная копия меша с индивидуальной трансформацией и цветом.

#### Конструктор

**`MeshInstance(Mesh* mesh = nullptr)`**

**Параметры:**
- `mesh` — указатель на исходный меш

#### Методы трансформации

**`void setPosition(float x, float y, float z)`**

Устанавливает позицию инстанса.

**`void setRotationEuler(float pitch, float yaw, float roll)`**

Устанавливает вращение через углы Эйлера (в градусах).

**Параметры:**
- `pitch` — вращение вокруг X (наклон вперед/назад)
- `yaw` — вращение вокруг Y (поворот влево/вправо)
- `roll` — вращение вокруг Z (крен)

**`void setRotation(const Quaternion& rot)`**

Устанавливает вращение через кватернион (продвинутый способ).

**`void setScale(float uniform)`**

Устанавливает равномерный масштаб.

**`void setScale(float x, float y, float z)`**

Устанавливает масштаб по осям.

**`void setColor(const Color& color)`**

Устанавливает цвет инстанса (перекрывает цвет меша).

**Пример:**
```cpp
Sphere* sphere = new Sphere(1.0f, 8, 6);

MeshInstance* inst1 = new MeshInstance(sphere);
inst1->setPosition(0, 0, 0);
inst1->setRotationEuler(0, 45, 0);  // Поворот на 45° по Y
inst1->setScale(1.5f);  // Увеличить в 1.5 раза
inst1->setColor(Color::RED);

MeshInstance* inst2 = new MeshInstance(sphere);
inst2->setPosition(5, 0, 0);
inst2->setScale(0.5f, 2.0f, 0.5f);  // Вытянутая по Y
inst2->setColor(Color::BLUE);
```

---

### 4. InstanceManager — Менеджер инстансов

Управляет множеством инстансов с автоматическим culling и сортировкой.

#### Методы

**`MeshInstance* createInstance(Mesh* mesh)`**

Создает новый инстанс.

**Параметры:**
- `mesh` — исходный меш

**Возвращает:**
- Указатель на созданный инстанс

**`std::vector<MeshInstance*> createInstances(Mesh* mesh, size_t count)`**

Создает несколько инстансов сразу.

**Параметры:**
- `mesh` — исходный меш
- `count` — количество инстансов

**Возвращает:**
- Вектор указателей на инстансы

**`void removeInstance(MeshInstance* inst)`**

Удаляет инстанс.

**`std::vector<MeshInstance*> getVisibleInstances(const Frustum& frustum) const`**

Возвращает только видимые инстансы (frustum culling).

**`size_t getInstanceCount() const`**

Возвращает общее количество инстансов.

**Пример полного использования:**
```cpp
InstanceManager manager;
Cube* cube = new Cube(1.0f);

// Создаем 100 кубиков в сетке 10x10
auto instances = manager.createInstances(cube, 100);

int idx = 0;
for (int x = 0; x < 10; x++) {
    for (int z = 0; z < 10; z++) {
        instances[idx]->setPosition(x * 2.0f, 0, z * 2.0f);
        
        // Градиентный цвет
        instances[idx]->setColor(
            Color::fromRGB888(x * 25, z * 25, 128)
        );
        
        // Случайный масштаб
        float scale = 0.5f + (rand() % 100) / 100.0f;
        instances[idx]->setScale(scale);
        
        idx++;
    }
}

// Рендеринг (автоматический culling!)
renderer.beginFrame();
renderer.drawInstances(manager);
renderer.endFrame();

Serial.printf("Total: %d, Visible: %d\n", 
    manager.getInstanceCount(),
    manager.getVisibleInstances(renderer.getFrustum()).size()
);
```

---

## 🛠️ Вспомогательные классы

### ObjectHelper — Помощник для рендеринга с тенями

Упрощает отрисовку объектов с тенями.

**`static void renderWithShadow(Renderer* renderer, T* object, bool shadowFirst = true)`**

**Что делает:**
Автоматически рендерит объект с тенью в правильном порядке.

**Параметры:**
- `renderer` — указатель на рендерер
- `object` — указатель на меш или инстанс
- `shadowFirst` — рисовать тень первой (true) или после объекта (false)

**Пример:**
```cpp
Cube* cube = new Cube(2.0f, Color::RED);

renderer.setShadowsEnabled(true);
renderer.beginFrame();

// Вместо:
// renderer.drawMeshShadow(cube);
// renderer.drawMesh(cube);

// Просто:
ObjectHelper::renderWithShadow(&renderer, cube);

renderer.endFrame();
```

---

**`static void renderMultipleWithShadows(Renderer* renderer, T** objects, int count)`**

**Что делает:**
Рендерит несколько объектов с тенями оптимально (сначала все тени, потом все объекты).

**Пример:**
```cpp
Mesh* objects[3];
objects[0] = new Cube(1.0f, Color::RED);
objects[1] = new Sphere(0.5f, 8, 6, Color::BLUE);
objects[2] = new Cylinder(0.3f, 2.0f, 12, Color::GREEN);

objects[0]->setPosition(-2, 0, 0);
objects[1]->setPosition(0, 0, 0);
objects[2]->setPosition(2, 0, 0);

renderer.beginFrame();
ObjectHelper::renderMultipleWithShadows(&renderer, objects, 3);
renderer.endFrame();
```

---

### SceneHelper — Помощник для сцены

Упрощает создание базовых элементов сцены (пол, солнце).

#### Конструктор

**`SceneHelper(Renderer* r)`**

**Параметры:**
- `r` — указатель на рендерер

#### Методы

**`void addGround(float size, float y, Color color)`**

Создает плоскость пола.

**Параметры:**
- `size` — размер пола (квадрат)
- `y` — высота пола
- `color` — цвет пола

**`void renderGround()`**

Рендерит пол.

**`void setSunPosition(float x, float y, float z, float temperature)`**

Устанавливает позицию солнца и цветовую температуру света.

**Параметры:**
- `x, y, z` — позиция солнца
- `temperature` — температура света в Кельвинах

**Полный пример сцены:**
```cpp
Renderer renderer;
SceneHelper scene(&renderer);
Cube* cube;

void setup() {
    FastMath::init();
    
    DisplayConfig config;
    config.width = 240;
    config.height = 320;
    config.cs_pin = 10;
    config.dc_pin = 9;
    config.rst_pin = 8;
    renderer.init(config);
    
    // Настройка камеры
    Camera& cam = renderer.getCamera();
    cam.position = Vector3(5, 5, -8);
    cam.target = Vector3(0, 0, 0);
    
    // Создание сцены
    scene.addGround(20.0f, -1.5f, Color::fromRGB888(80, 80, 80));
    scene.setSunPosition(-10, 10, -5, 5500.0f);  // Дневной свет
    
    // Объект
    cube = new Cube(2.0f, Color::RED);
    cube->setPosition(0, 0, 0);
    
    // Скайбокс и тени
    renderer.setSkyboxType(SKYBOX_DAY);
    renderer.setShadowsEnabled(true);
}

void loop() {
    static float angle = 0;
    cube->setRotation(angle, angle * 0.7f, 0);
    angle += 1.0f;
    
    renderer.beginFrame();
    
    // Рендеринг сцены
    scene.renderGround();
    ObjectHelper::renderWithShadow(&renderer, cube);
    
    // HUD
    char buf[32];
    sprintf(buf, "FPS: %.1f", renderer.getAverageFPS());
    renderer.drawTextAdaptive(10, 10, buf);
    
    renderer.endFrame();
}
```

---

## 🎨 Работа с цветом

### Color — Структура цвета

#### Создание цвета

```cpp
// Из RGB888 (0-255)
Color red = Color::fromRGB888(255, 0, 0);
Color green = Color::fromRGB888(0, 255, 0);
Color custom = Color::fromRGB888(128, 64, 200);

// Из температуры
Color sunset = Color::fromTemperature(2500.0f);   // Оранжевый
Color daylight = Color::fromTemperature(5500.0f); // Белый
Color moonlight = Color::fromTemperature(8000.0f);// Голубой

// Константы
Color black = Color::BLACK;
Color white = Color::WHITE;
```

#### Константы цветов

```cpp
Color::BLACK      // Черный
Color::WHITE      // Белый
Color::RED        // Красный
Color::GREEN      // Зеленый
Color::BLUE       // Синий
Color::CYAN       // Голубой
Color::MAGENTA    // Пурпурный
Color::YELLOW     // Желтый
```

**💡 Совет:** Используйте `fromTemperature()` для реалистичного освещения!

---

## 📐 Математика

### Vector3

```cpp
Vector3(float x, float y, float z)
float length() const
float lengthSquared() const
void normalize()
float dot(const Vector3& v) const
Vector3 cross(const Vector3& v) const
float distanceTo(const Vector3& v) const
```

### FastMath

**`static void init()`**
- Инициализация lookup tables (обязательно в `setup()`!)

**`static float fastSin(float angle)`**
- Быстрый синус

**`static float fastCos(float angle)`**
- Быстрый косинус

---

## 🛠️ Вспомогательные классы

### ObjectHelper

**`static void renderWithShadow(Renderer* renderer, T* object, bool shadowFirst = true)`**
- Рендерит объект с тенью

**`static void renderMultipleWithShadows(Renderer* renderer, T** objects, int count)`**
- Рендерит несколько объектов с тенями

### SceneHelper

**`void addGround(float size, float y, Color color)`**
- Добавляет пол

**`void renderGround()`**
- Рендерит пол

**`void setSunPosition(float x, float y, float z, float temperature)`**
- Устанавливает позицию солнца

---

## ⚙️ Ограничения ESP32

### Память

- **Максимум вершин на меш:** 512
- **Максимум граней на меш:** 1024
- **Максимум инстансов:** ~500 (зависит от PSRAM)
- **Z-buffer:** 320x240 (150KB)
- **Framebuffer:** 320x240x2 (150KB)

### Производительность

- **Целевой FPS:** 25-35
- **Оптимальные сегменты сферы:** 8-12
- **Оптимальные источники света:** 1-2
- **Frustum culling:** Автоматически для всех объектов

### Рекомендации

1. Используйте **инстансинг** для повторяющихся объектов
2. Включайте **frustum culling** (включен по умолчанию)
3. Ограничивайте **количество граней** (low-poly стиль)
4. Используйте **FastMath** вместо `sin()`/`cos()`
5. Вызывайте `FastMath::init()` в `setup()`

---

## 📝 Полезные константы

```cpp
PI          // 3.14159...
TWO_PI      // 6.28318...
DEG2RAD     // 0.01745... (градусы в радианы)
RAD2DEG     // 57.2957... (радианы в градусы)
```

---

## 🚀 Примеры использования

### Вращающийся куб с тенью

```cpp
renderer.setShadowsEnabled(true);
renderer.setShadowOpacity(0.4f);

renderer.beginFrame();
renderer.drawMeshShadow(cube);
renderer.drawMesh(cube);
renderer.endFrame();
```

### Множество инстансов

```cpp
InstanceManager manager;
Sphere* sphere = new Sphere(0.5f, 8, 6);

for (int i = 0; i < 100; i++) {
    auto inst = manager.createInstance(sphere);
    inst->setPosition(random(-10, 10), 0, random(-10, 10));
    inst->setColor(Color::fromRGB888(random(255), random(255), random(255)));
}

renderer.beginFrame();
renderer.drawInstances(manager);
renderer.endFrame();
```

### Изометрическая камера

```cpp
Camera& cam = renderer.getCamera();
cam.setOrthographic(10.0f, 10.0f, 0.1f, 100.0f);
cam.position = Vector3(10, 10, 10);
cam.target = Vector3(0, 0, 0);
cam.markDirty();
```

### Динамическое освещение

```cpp
float time = millis() / 1000.0f;
float x = cos(time) * 5.0f;
float z = sin(time) * 5.0f;
renderer.setMainPointLight(Vector3(x, 5, z), Color::WHITE, 2.0f, 15.0f);
```

### HUD с FPS

```cpp
char buf[32];
sprintf(buf, "FPS: %.1f", renderer.getAverageFPS());
renderer.drawTextAdaptive(10, 10, buf);
```

---

## 🎮 Полные примеры использования

### Пример 1: Вращающийся куб с тенью

```cpp
#include <pip3D.h>
using namespace pip3D;

Renderer renderer;
Cube* cube;

void setup() {
    Serial.begin(115200);
    FastMath::init();
    
    DisplayConfig config;
    config.width = 240;
    config.height = 320;
    config.cs_pin = 10;
    config.dc_pin = 9;
    config.rst_pin = 8;
    renderer.init(config);
    
    Camera& cam = renderer.getCamera();
    cam.position = Vector3(0, 3, -6);
    cam.target = Vector3(0, 0, 0);
    
    cube = new Cube(2.0f, Color::fromRGB888(255, 100, 50));
    
    renderer.setSkyboxType(SKYBOX_DAY);
    renderer.setShadowsEnabled(true);
    renderer.setShadowOpacity(0.4f);
    renderer.setMainDirectionalLight(
        Vector3(-0.5f, -1.0f, -0.5f),
        Color::WHITE,
        1.0f
    );
}

void loop() {
    static float angle = 0;
    cube->setRotation(angle, angle * 0.7f, 0);
    angle += 1.0f;
    
    renderer.beginFrame();
    ObjectHelper::renderWithShadow(&renderer, cube);
    
    char buf[32];
    sprintf(buf, "FPS: %.1f", renderer.getFPS());
    renderer.drawTextAdaptive(10, 10, buf);
    
    renderer.endFrame();
}
```

---

### Пример 2: Множество инстансов (лес кубиков)

```cpp
#include <pip3D.h>
using namespace pip3D;

Renderer renderer;
InstanceManager manager;
Cube* cube;

void setup() {
    Serial.begin(115200);
    FastMath::init();
    
    DisplayConfig config;
    config.width = 240;
    config.height = 320;
    config.cs_pin = 10;
    config.dc_pin = 9;
    config.rst_pin = 8;
    renderer.init(config);
    
    Camera& cam = renderer.getCamera();
    cam.position = Vector3(10, 10, -15);
    cam.target = Vector3(0, 0, 0);
    
    cube = new Cube(1.0f);
    
    // Создаем 100 кубиков
    for (int x = 0; x < 10; x++) {
        for (int z = 0; z < 10; z++) {
            auto inst = manager.createInstance(cube);
            inst->setPosition(x * 2.0f - 10, 0, z * 2.0f - 10);
            inst->setColor(Color::fromRGB888(
                x * 25,
                128,
                z * 25
            ));
            
            // Случайная высота
            float height = 0.5f + (rand() % 100) / 50.0f;
            inst->setScale(1.0f, height, 1.0f);
        }
    }
    
    renderer.setSkyboxType(SKYBOX_DAY);
    renderer.setMainDirectionalLight(
        Vector3(0, -1, -0.5f),
        Color::WHITE,
        1.0f
    );
}

void loop() {
    // Вращение камеры
    static float angle = 0;
    angle += 0.5f;
    float rad = angle * DEG2RAD;
    
    Camera& cam = renderer.getCamera();
    cam.position.x = cos(rad) * 15;
    cam.position.z = sin(rad) * 15;
    cam.markDirty();
    
    renderer.beginFrame();
    renderer.drawInstances(manager);
    
    char buf[64];
    sprintf(buf, "FPS: %.1f | Objects: %d", 
        renderer.getFPS(),
        manager.getInstanceCount()
    );
    renderer.drawTextAdaptive(10, 10, buf);
    
    renderer.endFrame();
}
```

---

### Пример 3: Динамическое освещение

```cpp
#include <pip3D.h>
using namespace pip3D;

Renderer renderer;
Sphere* sphere;

void setup() {
    Serial.begin(115200);
    FastMath::init();
    
    DisplayConfig config;
    config.width = 240;
    config.height = 320;
    config.cs_pin = 10;
    config.dc_pin = 9;
    config.rst_pin = 8;
    renderer.init(config);
    
    Camera& cam = renderer.getCamera();
    cam.position = Vector3(0, 3, -6);
    cam.target = Vector3(0, 0, 0);
    
    sphere = new Sphere(1.5f, 12, 8, Color::WHITE);
    
    renderer.setSkyboxType(SKYBOX_NIGHT);
}

void loop() {
    // Вращающийся точечный свет
    static float angle = 0;
    angle += 2.0f;
    float rad = angle * DEG2RAD;
    
    Vector3 lightPos(
        cos(rad) * 5.0f,
        3.0f,
        sin(rad) * 5.0f
    );
    
    // Меняем цвет света
    float hue = fmod(angle, 360.0f);
    Color lightColor;
    if (hue < 120) {
        lightColor = Color::RED;
    } else if (hue < 240) {
        lightColor = Color::GREEN;
    } else {
        lightColor = Color::BLUE;
    }
    
    renderer.setMainPointLight(
        lightPos,
        lightColor,
        2.0f,
        15.0f
    );
    
    renderer.beginFrame();
    renderer.drawMesh(sphere);
    
    char buf[32];
    sprintf(buf, "FPS: %.1f", renderer.getFPS());
    renderer.drawText(10, 10, buf, Color::WHITE);
    
    renderer.endFrame();
}
```

---

## 📚 Дополнительная документация

Для более подробной информации о структурах данных, математике и продвинутых возможностях смотрите:

**API_EXTENDED.md** — расширенная документация с подробным описанием:
- Color (все методы и примеры)
- Light (все типы и настройки)
- Vector3 (математические операции)
- Quaternion (продвинутые вращения)
- Camera (подробно о проекциях)
- Skybox (кастомизация)
- PerformanceCounter
- Математические константы
- Оптимизация и ограничения

---

## 🎯 Быстрая справка

### Инициализация (обязательно!)
```cpp
FastMath::init();  // Перед renderer.init()!
renderer.init(config);
```

### Базовый цикл рендеринга
```cpp
renderer.beginFrame();
// ... отрисовка ...
renderer.endFrame();
```

### Создание объектов
```cpp
Cube* cube = new Cube(size, color);
Sphere* sphere = new Sphere(radius, segments, rings, color);
Cylinder* cyl = new Cylinder(radius, height, segments, color);
```

### Трансформации
```cpp
object->setPosition(x, y, z);
object->setRotation(x, y, z);  // градусы
object->setScale(x, y, z);
```

### Освещение
```cpp
// Солнце
renderer.setMainDirectionalLight(direction, color, intensity);

// Лампа
renderer.setMainPointLight(position, color, intensity, range);

// Температура
renderer.setLightTemperature(kelvin);
```

### Тени
```cpp
renderer.setShadowsEnabled(true);
renderer.setShadowOpacity(0.4f);
ObjectHelper::renderWithShadow(&renderer, object);
```

### Инстансинг
```cpp
InstanceManager manager;
auto inst = manager.createInstance(mesh);
inst->setPosition(x, y, z);
renderer.drawInstances(manager);
```

---

**Документация создана для PIP3D v1.0.0**  
**© 2024 | Оптимизировано для ESP32-S3**  
**Автор документации: AI Assistant**

📖 **Полная документация:** API.md + API_EXTENDED.md  
🚀 **GitHub:** (добавьте ссылку на ваш репозиторий)  
💬 **Поддержка:** (добавьте контакты)
