/*
 ***********************************************************************************************************************
 *
 *  Copyright (c) 2014-2023 Advanced Micro Devices, Inc. All Rights Reserved.
 *
 *  Permission is hereby granted, free of charge, to any person obtaining a copy
 *  of this software and associated documentation files (the "Software"), to deal
 *  in the Software without restriction, including without limitation the rights
 *  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 *  copies of the Software, and to permit persons to whom the Software is
 *  furnished to do so, subject to the following conditions:
 *
 *  The above copyright notice and this permission notice shall be included in all
 *  copies or substantial portions of the Software.
 *
 *  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 *  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 *  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 *  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 *  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 *  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 *  SOFTWARE.
 *
 **********************************************************************************************************************/

#ifndef __VK_DEFERRED_OPERATION_H__
#define __VK_DEFERRED_OPERATION_H__
#pragma once

#include "include/khronos/vulkan.h"
#include "include/vk_defines.h"
#include "include/vk_dispatch.h"
#include "include/vk_utils.h"
#include "include/vk_alloccb.h"

#include "palEvent.h"

namespace vk
{

class DeferredHostOperation;
#if VKI_RAY_TRACING
class PipelineCache;
class RayTracingPipeline;
#endif

enum class DeferredCallbackType : uint32_t
{
    Join = 0,
    GetMaxConcurrency,
    GetResult
};

// Callback for "simple" operations: should fully execute the deferred operation and return its result
typedef VkResult (*DeferredHostSimpleFunc)(Device* pDevice,
                                           const void* pArgs);

typedef int32_t (*DeferredHostCallback)(Device*                pDevice,
                                        DeferredHostOperation* pOperation,
                                        DeferredCallbackType   type);

struct DeferredWorkload
{
    uint32_t    nextInstance;          // Next workload instance to execute
    uint32_t    completedInstances;    // # of workload instances fully executed
    uint32_t    totalInstances;        // Actual # of workload instances (UINT_MAX if not yet known, 0 if no-op)
    uint32_t    maxInstances;          // Upper limit estimate of the # of instances (for when actual # is unavailable)
    void*       pPayloads;             // Array of payloads (per workload instance)
    void        (*Execute)(void*);     // Function pointer to the call used to execute the workload
    Util::Event event;                 // Event to notify main thread when the workers have completed
};

// =====================================================================================================================
// Vulkan deferred host operation object
class DeferredHostOperation : public NonDispatchable<VkDeferredOperationKHR, DeferredHostOperation>
{
public:
#if VKI_RAY_TRACING
    // State for deferred vkBuildAccelerationStructuresKHR
    struct AccelBuildState
    {
        uint32_t                                                nextPending;
        uint32_t                                                completed;
        uint32_t                                                failedMaps;

        uint32_t                                                infoCount;
        const VkAccelerationStructureBuildGeometryInfoKHR*      pInfos;
        const VkAccelerationStructureBuildRangeInfoKHR* const*  ppBuildRangeInfos;
    };

    // State for deferred VkRayTracingPipelineCreateInfoKHR
    struct RayTracingPipelineCreateState
    {
        uint32_t                                 nextPending;
        uint32_t                                 completed;
        uint32_t                                 finalResult;
        uint32_t                                 skipRemaining;

        PipelineCache*                           pPipelineCache;
        uint32_t                                 infoCount;
        const VkRayTracingPipelineCreateInfoKHR* pInfos;
        const VkAllocationCallbacks*             pAllocator;
        VkPipeline*                              pPipelines;
    };
#endif

    // State for "simple" operations set by SetSimpleOperation()
    struct SimpleState
    {
        uint32_t               joined;
        DeferredHostSimpleFunc pfnOperation;
        const void*            pArg;
        VkResult               result;
    };

    static VkResult Create(
        Device*                      pDevice,
        const VkAllocationCallbacks* pAllocator,
        VkDeferredOperationKHR*      pDeferredOperation);

    VkResult Destroy(
        Device*                      pDevice,
        const VkAllocationCallbacks* pAllocator);

    VkResult Join(Device* pDevice);

    uint32_t GetMaxConcurrency(Device* pDevice);

    VkResult GetOperationResult(Device* pDevice);

    void SetOperation(DeferredHostCallback pfnCallback);

    // Simple operation that executes fully within the first join call
    void SetSimpleOperation(DeferredHostSimpleFunc pfnSimple,
                            const void* pArg);

    template<typename ArgType> void SetSimpleOperation(VkResult (*pfnSimple)(Device* pDevice, const ArgType*),
                                                       const ArgType* pArgs)
    {
        SetSimpleOperation(reinterpret_cast<DeferredHostSimpleFunc>(pfnSimple), pArgs);
    }

    SimpleState* Simple() { return &m_state.simple; }

#if VKI_RAY_TRACING
    AccelBuildState* AccelBuild() { return &m_state.accelBuild; }
    RayTracingPipelineCreateState* RayTracingPipelineCreate() { return &m_state.rtPipelineCreate; }
#endif

    static void ExecuteWorkload(DeferredWorkload* pWorkload);
    VkResult GenerateWorkloads(uint32_t count);

    uint32_t WorkloadCount() { return m_workloadCount; }
    DeferredWorkload* Workload(uint32_t idx) { return &m_pWorkloads[idx]; }

private:
    PAL_DISALLOW_COPY_AND_ASSIGN(DeferredHostOperation);

    DeferredHostOperation(Instance* pInstance);

    // Used for deferred host objects that haven't yet been assigned to a command
    static int32_t UnusedCallback(Device* pDevice, DeferredHostOperation* pHost, DeferredCallbackType type);

    // Implementation for simple operations
    static int32_t SimpleCallback(Device* pDevice, DeferredHostOperation* pHost, DeferredCallbackType type);

    // Destroys any initialized workloads
    void DestroyWorkloads();

    DeferredHostCallback m_pfnCallback; ///< Callback for executing deferred Join/GetMaxConcurrency/GetResult.

    union
    {
        SimpleState                   simple;           ///< Command state for simple operations
#if VKI_RAY_TRACING
        AccelBuildState               accelBuild;       ///< Command state for deferred vkBuildAccelerationStructuresKHR
        RayTracingPipelineCreateState rtPipelineCreate; ///< Command state for deferred VkRayTracingPipelineCreateInfoKHR
#endif
    } m_state;

    Instance*         m_pInstance;
    uint32_t          m_workloadCount;
    DeferredWorkload* m_pWorkloads;
};

} // namespace vk

#endif /* __VK_DEFERRED_OPERATION_H__ */
