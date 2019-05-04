cc_library(
    name = "seratocrates",
    srcs = [
        "memberpointer.h",
        "read_disk_files.h",
        "seratocrates.cpp",
    ],
    hdrs = [
        "seratocrates.h",
    ],
    includes = ["."],
    # Source files need C++17 to compile but seratocrates.h is C++11 compatible.
    copts = [
        "-std=c++17",
    ],
    linkopts = [
        "-lstdc++fs",
    ],
    visibility = ["//visibility:public"],
)
