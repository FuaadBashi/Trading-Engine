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
            -Wnull-dereference
            -Wformat=2
            -Wimplicit-fallthrough
        )
        if(TE_STRICT_CONVERSIONS)
            target_compile_options(${target} PRIVATE -Wconversion -Wsign-conversion)
        endif()
        # Keep frame pointers so Instruments and perf can walk the stack.
        target_compile_options(${target} PRIVATE -fno-omit-frame-pointer)
    endif()
endfunction()
