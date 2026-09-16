# Warnings are the cheapest bug detector you own.
#
# -Werror is on from day one, same as AudioVisualiser. The extra flags beyond
# -Wall -Wextra -Wpedantic are here because this project is full of int64 arithmetic
# and raw pointers, which is exactly what -Wconversion and -Wcast-align catch.
#
# -Wconversion plus -Werror WILL be painful in slice 1. That pain is the feature:
# every hit is a place where a price or a size is silently changing width or sign.
# If it becomes unworkable, demote it via TE_STRICT_CONVERSIONS=OFF rather than
# dropping -Werror entirely.

option(TE_STRICT_CONVERSIONS "Treat implicit numeric conversions as errors" ON)

function(te_set_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /WX /permissive-)
    else()
        target_compile_options(${target} PRIVATE
            -Wall -Wextra -Wpedantic -Werror
            -Wshadow                # a shadowed `qty` in the book builder is a real bug
            -Wnon-virtual-dtor      # Feed, Clock, Strategy, ExecutionVenue all need one
            -Wold-style-cast        # forces static_cast, which is greppable
            -Wcast-align            # matters when you reinterpret the binary record buffer
            -Wdouble-promotion      # catches a float sneaking toward price maths
            -Wformat=2
            -Wimplicit-fallthrough
        )
        if(TE_STRICT_CONVERSIONS)
            target_compile_options(${target} PRIVATE -Wconversion -Wsign-conversion)
        endif()
        # Keep frame pointers so Instruments and perf can walk the stack.
        target_compile_options(${target} PRIVATE -fno-omit-frame-pointer)

        # -Wnull-dereference: clang never raises it on this codebase (Debug or Release). GCC at
        # -O2 (Release only -- Debug uses -Og) raises it on the core Result<T,E> idiom itself --
        # `if (!x.hasValue()) return failure(*x.errorIf());`, checked and used on the very next
        # line, same object, no intervening call. errorIf() is non-null exactly when hasValue() is
        # false by construction (std::variant holds exactly one of T or E), so this is a real
        # invariant GCC's flow analysis doesn't connect across the two accessor calls, not a bug --
        # confirmed by testing every flagged site by hand. Enabled for clang, where it never fires
        # a false positive; disabled for GCC, where fixing it would mean rewriting the error-
        # propagation idiom used at every Result<T,E> call site in the codebase for no safety gain.
        if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
            target_compile_options(${target} PRIVATE -Wno-null-dereference)
        else()
            target_compile_options(${target} PRIVATE -Wnull-dereference)
        endif()
    endif()
endfunction()
