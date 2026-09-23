# cmake -B build -DTE_SANITIZE=address,undefined   (or =thread; ASan and TSan cannot combine)

set(TE_SANITIZE "" CACHE STRING "Comma-separated sanitizers, e.g. address,undefined or thread")

function(te_enable_sanitizers target)
    if(TE_SANITIZE STREQUAL "" OR MSVC)
        return()
    endif()
    if(TE_SANITIZE MATCHES "address" AND TE_SANITIZE MATCHES "thread")
        message(FATAL_ERROR "ASan and TSan are mutually exclusive. Use two build dirs.")
    endif()
    target_compile_options(${target} PRIVATE -fsanitize=${TE_SANITIZE} -fno-omit-frame-pointer -g)
    target_link_options(${target}    PRIVATE -fsanitize=${TE_SANITIZE})

    # UBSan recovers by default and exits 0; make it fatal so CI fails (proved by tests/ub_probe.cpp).
    # Unsigned wraparound is not UB and is not caught -- boundary tests cover those sums.
    if(TE_SANITIZE MATCHES "undefined")
        target_compile_options(${target} PRIVATE -fno-sanitize-recover=undefined)
        target_link_options(${target}    PRIVATE -fno-sanitize-recover=undefined)
    endif()
endfunction()
