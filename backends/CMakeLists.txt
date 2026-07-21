# backends/ — compute backends behind the stable extern-C ABI (ADR DD-007).
#
# The CPU backend is the reference and always builds. GPU backends are opt-in
# via HB_BACKEND_* options. A failed backend call must always fall back to the
# CPU per-tensor path (a runtime contract, enforced in the executor later).

if(HB_BACKEND_CPU)
    add_subdirectory(cpu)
endif()

if(HB_BACKEND_CUDA)
    add_subdirectory(cuda)
endif()

if(HB_BACKEND_METAL)
    add_subdirectory(metal)
endif()

if(HB_BACKEND_ROCM)
    message(FATAL_ERROR "HB_BACKEND_ROCM is declared for the future but not implemented yet.")
endif()

if(HB_BACKEND_VULKAN)
    message(FATAL_ERROR "HB_BACKEND_VULKAN is declared for the future but not implemented yet.")
endif()
