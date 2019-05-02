cc_library(
    name = "seratocrates",
    srcs = [
        "memberpointer.h",
        "seratocrates.cpp",
    ],
    hdrs = [
        "seratocrates.h",
    ],
    # Source files need C++17 to compile but seratocrates.h is C++11 compatible.
    copts = [
        "-std=c++17",
    ],
)
