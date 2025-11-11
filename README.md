# 🚀 PIP3D API Documentation

**Версия:** 1.0.0  
**Платформа:** ESP32-S3  
**Дисплей:** ST7789 (240x320)

## 📖 Описание

**PIP3D** — высокооптимизированный 3D-движок для микроконтроллеров ESP32, специально разработанный для работы с дисплеями ST7789. Поддерживает перспективную и ортографическую проекцию, динамическое освещение, планарные тени, инстансинг объектов и продвинутый frustum culling.

---

[[_TOC_]]

---

## ⚡ Быстрый старт

### Минимальный рабочий пример

```cpp
#include <pip3D.h>
using namespace pip3D;

Renderer renderer;
Cube* cube;

void setup() {
    Serial.begin(115200);
    FastMath::init();  // Обязательно!
    
    DisplayConfig config;
    config.width = 240;
    config.height = 320;
    config.cs_pin = 10;
    config.dc_pin = 9;
    config.rst_pin = 8;
    
    renderer.init(config);
    
    Camera& cam = renderer.getCamera();
    cam.position = Vector3(0, 2, -5);
    cam.target = Vector3(0, 0, 0);
    
    cube = new Cube(2.0f, Color::RED);
    renderer.setSkyboxType(SKYBOX_DAY);
}

void loop() {
    static float angle = 0;
    cube->setRotation(angle, angle * 0.7f, 0);
    angle += 1.0f;
    
    renderer.beginFrame();
    renderer.drawMesh(cube);
    renderer.endFrame();
}
```

---

## 🏗️ Основные классы

### Renderer

Главный класс рендеринга.

#### Инициализация

**`bool init(const DisplayConfig& cfg)`**
- Инициализирует рендерер
- Вызывать один раз в `setup()`

**`void beginFrame()`**
- Начинает новый кадр
- Очищает Z-buffer

**`void endFrame()`**
- Завершает кадр
- Отправляет данные на дисплей

#### Отрисовка

**`void drawMesh(Mesh* mesh)`**
- Рендерит 3D-меш с освещением

**`void drawMeshInstance(MeshInstance* instance)`**
- Рендерит инстанс меша

**`void drawInstances(InstanceManager& manager)`**
- Рендерит все видимые инстансы

#### Тени

**`void setShadowsEnabled(bool enabled)`**
- Включает/выключает тени

**`void drawMeshShadow(Mesh* mesh)`**
- Рендерит планарную тень

**`void setShadowOpacity(float opacity)`**
- Прозрачность теней (0.0-1.0)
- Рекомендуется: 0.3-0.6

**`void setShadowColor(const Color& color)`**
- Цвет теней

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

### MeshInstance

Легковесная копия меша.

**`MeshInstance(Mesh* mesh = nullptr)`**

**`void setPosition(float x, float y, float z)`**

**`void setRotationEuler(float pitch, float yaw, float roll)`**

**`void setScale(float uniform)`**

**`void setColor(const Color& color)`**

---

### InstanceManager

Управляет множеством инстансов.

**`MeshInstance* createInstance(Mesh* mesh)`**

**`std::vector<MeshInstance*> createInstances(Mesh* mesh, size_t count)`**

**`void removeInstance(MeshInstance* inst)`**

**`std::vector<MeshInstance*> getVisibleInstances(const Frustum& frustum) const`**

---

## 🎨 Работа с цветом

### Color

```cpp
Color(uint8_t r, uint8_t g, uint8_t b)  // RGB888
Color::fromRGB888(uint8_t r, uint8_t g, uint8_t b)
Color::fromTemperature(float kelvin)
```

#### Константы

```cpp
Color::BLACK
Color::WHITE
Color::RED
Color::GREEN
Color::BLUE
Color::CYAN
Color::MAGENTA
Color::YELLOW
```

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

**Документация создана для PIP3D v1.0.0**  
**© 2024 | Оптимизировано для ESP32-S3**
