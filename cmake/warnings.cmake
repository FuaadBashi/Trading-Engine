# -Werror from day one. -Wconversion catches prices and sizes silently changing width or sign;
# demote it with TE_STRICT_CONVERSIONS=OFF rather than dropping -Werror.

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

        # GCC -O2 false-positives on the Result<T,E> idiom (hasValue() then errorIf()); clang doesn't.
        if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
            target_compile_options(${target} PRIVATE -Wno-null-dereference)
        else()
            target_compile_options(${target} PRIVATE -Wnull-dereference)
        endif()
    endif()
endfunction()
