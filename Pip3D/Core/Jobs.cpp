#include "Jobs.h"

namespace pip3D
{
#ifdef ARDUINO_ARCH_ESP32

    static constexpr int MAX_JOBS = 32;

    static Job s_jobQueue[MAX_JOBS];
    static volatile int s_head = 0;
    static volatile int s_tail = 0;

    static TaskHandle_t s_workerTask = nullptr;
    static SemaphoreHandle_t s_queueMutex = nullptr;
    static SemaphoreHandle_t s_jobSemaphore = nullptr;

    static bool s_initialized = false;
    static bool s_enabled = false;

    static inline bool queueEmpty()
    {
        return s_head == s_tail;
    }

    static inline bool queueFull()
    {
        int nextHead = s_head + 1;
        if (nextHead >= MAX_JOBS)
            nextHead = 0;
        return nextHead == s_tail;
    }

    bool JobSystem::init()
    {
#if CONFIG_FREERTOS_UNICORE
        s_enabled = false;
        LOGW(::pip3D::Debug::LOG_MODULE_CORE,
             "JobSystem disabled: FreeRTOS unicore configuration");
        return false;
#else
        if (s_initialized)
        {
            s_enabled = true;
            return true;
        }

        s_queueMutex = xSemaphoreCreateMutex();
        if (!s_queueMutex)
        {
            LOGE(::pip3D::Debug::LOG_MODULE_CORE,
                 "JobSystem::init failed: could not create queue mutex");
            return false;
        }

        s_jobSemaphore = xSemaphoreCreateCounting(MAX_JOBS, 0);
        if (!s_jobSemaphore)
        {
            vSemaphoreDelete(s_queueMutex);
            s_queueMutex = nullptr;
            LOGE(::pip3D::Debug::LOG_MODULE_CORE,
                 "JobSystem::init failed: could not create job semaphore");
            return false;
        }

        const uint32_t STACK_SIZE = 4096;
        BaseType_t res = xTaskCreatePinnedToCore(
            JobSystem::workerLoop,
            "Pip3DJobWorker",
            STACK_SIZE,
            nullptr,
            1,
            &s_workerTask,
            0);

        if (res != pdPASS)
        {
            vSemaphoreDelete(s_queueMutex);
            vSemaphoreDelete(s_jobSemaphore);
            s_queueMutex = nullptr;
            s_jobSemaphore = nullptr;
            s_workerTask = nullptr;
            LOGE(::pip3D::Debug::LOG_MODULE_CORE,
                 "JobSystem::init failed: xTaskCreatePinnedToCore returned %d",
                 (int)res);
            return false;
        }

        s_head = 0;
        s_tail = 0;
        s_initialized = true;
        s_enabled = true;
        return true;
    }
#endif

        void JobSystem::shutdown()
        {
            s_enabled = false;

            if (s_workerTask)
            {
                vTaskDelete(s_workerTask);
                s_workerTask = nullptr;
            }

            if (s_queueMutex)
            {
                vSemaphoreDelete(s_queueMutex);
                s_queueMutex = nullptr;
            }

            if (s_jobSemaphore)
            {
                vSemaphoreDelete(s_jobSemaphore);
                s_jobSemaphore = nullptr;
            }

            s_head = 0;
            s_tail = 0;
            s_initialized = false;
        }

        bool JobSystem::submit(JobFunc func, void *userData)
        {
            if (!s_initialized || !s_enabled || !func)
            {
                LOGW(::pip3D::Debug::LOG_MODULE_CORE,
                     "JobSystem::submit rejected: initialized=%d enabled=%d func=%p",
                     (int)s_initialized,
                     (int)s_enabled,
                     (void *)func);
                return false;
            }

            if (xSemaphoreTake(s_queueMutex, 0) != pdTRUE)
            {
                LOGW(::pip3D::Debug::LOG_MODULE_CORE,
                     "JobSystem::submit could not acquire queue mutex");
                return false;
            }

            if (queueFull())
            {
                xSemaphoreGive(s_queueMutex);
                LOGW(::pip3D::Debug::LOG_MODULE_CORE,
                     "JobSystem queue full, job rejected");
                return false;
            }

            int index = s_head;
            s_jobQueue[index].func = func;
            s_jobQueue[index].userData = userData;

            int nextHead = index + 1;
            if (nextHead >= MAX_JOBS)
                nextHead = 0;
            s_head = nextHead;

            xSemaphoreGive(s_queueMutex);
            xSemaphoreGive(s_jobSemaphore);

            return true;
        }

        bool JobSystem::isEnabled()
        {
            return s_enabled;
        }

        void JobSystem::workerLoop(void *param)
        {
            (void)param;

            for (;;)
            {
                if (!s_enabled)
                {
                    vTaskDelay(1);
                    continue;
                }

                if (xSemaphoreTake(s_jobSemaphore, portMAX_DELAY) != pdTRUE)
                {
                    continue;
                }

                Job job = {nullptr, nullptr};

                if (xSemaphoreTake(s_queueMutex, portMAX_DELAY) == pdTRUE)
                {
                    if (!queueEmpty())
                    {
                        int index = s_tail;
                        job = s_jobQueue[index];

                        int nextTail = index + 1;
                        if (nextTail >= MAX_JOBS)
                            nextTail = 0;
                        s_tail = nextTail;
                    }
                    xSemaphoreGive(s_queueMutex);
                }

                if (job.func)
                {
                    job.func(job.userData);
                }
            }
        }

#else

    static bool s_enabled = false;

    bool JobSystem::init()
    {
        s_enabled = false;
        return false;
    }

    void JobSystem::shutdown()
    {
        s_enabled = false;
    }

    bool JobSystem::submit(JobFunc func, void *userData)
    {
        if (!func)
            return false;
        func(userData);
        return true;
    }

    bool JobSystem::isEnabled()
    {
        return s_enabled;
    }

    void JobSystem::workerLoop(void *param)
    {
        (void)param;
    }

#endif

        void useDualCore(bool enabled)
        {
            if (enabled)
            {
                JobSystem::init();
            }
            else
            {
                JobSystem::shutdown();
            }
        }

        bool isDualCoreEnabled()
        {
            return JobSystem::isEnabled();
        }
    }
