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

load("//:common_settings.bzl", "ovms_cc_library")

ovms_cc_library(
    name = "config_export",
    hdrs = ["config_export.hpp",],
    srcs = ["config_export.cpp",],
    deps = [
        "@com_github_tencent_rapidjson//:rapidjson",
        "config_export_types",
        "//src/graph_export:graph_export_types",
        "//src:libovms_server_settings",
        "//src:libovmsfilesystem",
        "//src:libovmslocalfilesystem",
        "//src:libovmslogging",
        "//src:libovmsschema",
    ],
    visibility = ["//visibility:public",],
)

ovms_cc_library(
    name = "config_export_types",
    srcs = ["config_export_types.cpp"],
    hdrs = ["config_export_types.hpp"],
    deps =  [],
    visibility = ["//visibility:public"],
)
