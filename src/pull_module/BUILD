#
# Copyright (c) 2025 Intel Corporation
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#

load("//:common_settings.bzl",
     "ovms_cc_library")

ovms_cc_library(
    name = "cmd_exec",
    srcs = ["cmd_exec.cpp"],
    hdrs = ["cmd_exec.hpp"],
    deps =  [],
    visibility = ["//visibility:public"],
)

ovms_cc_library(
    name = "libgit2",
    srcs = ["libgit2.cpp"],
    hdrs = ["libgit2.hpp"],
    deps =  [
        "@libgit2_engine//:libgit2_engine",
        "@ovms//src:libovmslogging",
        "@ovms//src:libovmsstatus",
        "@ovms//src:libovmsstring_utils",
        "@ovms//src:libovmsfilesystem",
        "@ovms//src:libovmslocalfilesystem",
        ":cmd_exec",
    ],
    visibility = ["//visibility:public"],
)

ovms_cc_library(
    name = "hf_pull_model_module",
    srcs = ["hf_pull_model_module.cpp"],
    hdrs = ["hf_pull_model_module.hpp"],
    deps =  [
        ":libgit2",
        "@ovms//src/graph_export:graph_export",
        "@ovms//src:cpp_headers",
        "@ovms//src:libovmsstring_utils",
        "@ovms//src:libovmslogging",
        "@ovms//src:libovms_server_settings",
        "@ovms//src:libovms_module",
    ],
    visibility = ["//visibility:public"],
)
