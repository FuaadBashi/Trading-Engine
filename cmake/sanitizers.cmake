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

    # Detection is not enforcement. UBSan recovers by default: it prints the diagnostic and
    # keeps running, so the process still exits 0 and a CI run stays green with real undefined
    # behaviour in the log. ASan aborts by default; UBSan does not. Verified by probe --
    # tests/ub_probe.cpp, built only with -DTE_BUILD_UB_PROBE=ON.
    #
    # A compile/link flag rather than UBSAN_OPTIONS=halt_on_error: the env var binds only where
    # it is exported, so a local sanitized build would keep silently recovering. This travels
    # with the binary.
    #
    # Scope note: this covers UBSan's checks, which include SIGNED overflow. Unsigned wraparound
    # is well-defined in C++, is not UB, and is not caught here -- explicit boundary tests remain
    # the only guard for the unsigned sums in trade_reconciler/capture_coordinator/price_level.
    if(TE_SANITIZE MATCHES "undefined")
        target_compile_options(${target} PRIVATE -fno-sanitize-recover=undefined)
        target_link_options(${target}    PRIVATE -fno-sanitize-recover=undefined)
    endif()
endfunction()
