# Run every test under ASan+UBSan from day one, not from week 9.
#
#   cmake -B build       -DTE_SANITIZE=address,undefined
#   cmake -B build-tsan  -DTE_SANITIZE=thread
#
# ASan and TSan cannot be combined, hence two build directories. You will need the
# TSan one from slice 3 onward, when the SPSC queue exists — it is the only tool that
# will catch a wrong memory order under contention.

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
endfunction()
