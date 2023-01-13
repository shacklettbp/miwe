#pragma once

#include <stdint.h>
#include <atomic>

// for current setting, it is required to be larger than steps * (#blocks + #nodes)
#define NUM_EVENT_LOG 10000000

namespace madrona
{
    namespace mwGPU
    {

        class DeviceTracingAllocator;

        // todo: expand to log the activity of every block instead of only nodes
        enum class DeviceEvent : uint32_t
        {
            calibration = 0,
            nodeStart = 1,
            nodeFinish = 2,
            blockStart = 3,
            blockWait = 4,
            blockExit = 5,
        };

        class DeviceTracing
        {
        private:
            struct DeviceLog
            {
                DeviceEvent event;
                uint32_t funcID;
                uint32_t numInvocations;
                uint32_t nodeID;
                uint32_t blockID;
                uint32_t smID;
                uint64_t cycleCount;
            };

            std::atomic_uint32_t cur_index_;
            DeviceLog device_logs_[NUM_EVENT_LOG];

        public:
            inline uint32_t getIndex()
            {
                return cur_index_.load(std::memory_order_relaxed);
            }

            inline void resetIndex()
            {
                cur_index_.store(0, std::memory_order_release);
            }

#ifdef MADRONA_GPU_MODE
            __inline__ uint64_t global_timer()
            {
                uint64_t timestamp;
                asm volatile("mov.u64 %0, %%globaltimer;"
                             : "=l"(timestamp));
                return timestamp;
            }
            inline void DeviceEventLogging(DeviceEvent event, uint32_t func_id, uint32_t num_invocations, uint32_t node_id)
            {
#ifdef MADRONA_TRACING
                if (threadIdx.x == 0)
                {
                    uint32_t sm_id;
                    asm("mov.u32 %0, %smid;"
                        : "=r"(sm_id));
                    uint32_t log_index = cur_index_.fetch_add(1, std::memory_order_relaxed);
                    if (log_index >= NUM_EVENT_LOG)
                    {
                        log_index = 0;
                        resetIndex();
                    }
                    device_logs_[log_index] = {event, func_id, num_invocations, node_id, blockIdx.x, sm_id, global_timer()};
                }
#endif
            }
#endif

            friend class DeviceTracingAllocator;
        };

    }

}