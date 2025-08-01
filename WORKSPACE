#
# Copyright (c) 2020 Intel Corporation
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

workspace(name = "ovms")

load("@bazel_tools//tools/build_defs/repo:git.bzl", "git_repository")
load("@bazel_tools//tools/build_defs/repo:git.bzl", "new_git_repository")
load("@bazel_tools//tools/build_defs/repo:http.bzl", "http_archive")

# 2023-06-05
# This version of Glog is required for Windows support, but currently causes
# crashes on some Android devices.
# OVMS - must be before MP
http_archive(
    name = "com_github_glog_glog_windows",
    strip_prefix = "glog-3a0d4d22c5ae0b9a2216988411cfa6bf860cc372",
    sha256 = "170d08f80210b82d95563f4723a15095eff1aad1863000e8eeb569c96a98fefb",
    urls = [
      "https://github.com/google/glog/archive/3a0d4d22c5ae0b9a2216988411cfa6bf860cc372.zip",
    ],
    patches = [
        "@mediapipe//third_party:com_github_glog_glog.diff",
        "@mediapipe//third_party:com_github_glog_glog_windows_patch.diff",
    ],
    patch_args = [
        "-p1",
    ],
)

http_archive(
    name = "bazel_skylib",
    sha256 = "74d544d96f4a5bb630d465ca8bbcfe231e3594e5aae57e1edbf17a6eb3ca2506",
    urls = [
        "https://storage.googleapis.com/mirror.tensorflow.org/github.com/bazelbuild/bazel-skylib/releases/download/1.3.0/bazel-skylib-1.3.0.tar.gz",
        "https://github.com/bazelbuild/bazel-skylib/releases/download/1.3.0/bazel-skylib-1.3.0.tar.gz",
    ],
)
load("@bazel_skylib//:workspace.bzl", "bazel_skylib_workspace")
bazel_skylib_workspace()
load("@bazel_skylib//lib:versions.bzl", "versions")
versions.check(minimum_bazel_version = "6.0.0")

load("@bazel_tools//tools/build_defs/repo:http.bzl", "http_archive")

http_archive(
    name = "rules_python",
    sha256 = "0cc05ddb27614baecace068986931e2a6e9f69114e6115fc5dc58250faf56e0f",
    strip_prefix = "rules_python-0.37.0",
    url = "https://github.com/bazelbuild/rules_python/releases/download/0.37.0/rules_python-0.37.0.tar.gz",
)

load("@rules_python//python:repositories.bzl", "py_repositories")

py_repositories()

# ABSL on 2023-10-18
# Needed for MP
# https://github.com/google-ai-edge/mediapipe/commit/743cdb747332efdfb43338d92aa6349acc40a06a
# patch for static_assert(ValidateAsciiCasefold() == 0, "error in case conversion");
# needs to be before MP & TF
git_repository(
    name = "com_google_absl",
    remote = "https://github.com/abseil/abseil-cpp",
    commit = "9687a8ea750bfcddf790372093245a1d041b21a3", # MP image buildable original MP
    patches = [
        "@mediapipe//third_party:com_google_absl_windows_patch.diff",
        "abseil_gcc_8.5_constant_expression.patch",
    ],
    patch_args = [
        "-p1",
    ],
)

http_archive(
    name = "zlib",
    build_file = "@mediapipe//third_party:zlib.BUILD",
    sha256 = "9a93b2b7dfdac77ceba5a558a580e74667dd6fede4585b91eefb60f03b72df23",
    strip_prefix = "zlib-1.3.1",
    url = "http://zlib.net/fossils/zlib-1.3.1.tar.gz",
)

# RapidJSON
# Must be defined earlier than tensorflow_serving because TFS is using older rapidjson
# Version must match openvino.genai -> jinja2cpp -> rapidjson
# git/Jinja2Cpp/thirdparty/internal_deps.cmake
# Date:   Tue May 9 21:31:22 2023 +0000 Avoid ptrdiff between pointers to different allocations
http_archive(
    name = "com_github_tencent_rapidjson",
    url = "https://github.com/Tencent/rapidjson/archive/973dc9c06dcd3d035ebd039cfb9ea457721ec213.tar.gz",
    sha256 = "d0c9e52823d493206eb721d38cb3a669ca0212360862bd15a3c2f7d35ea7c6f7",
    strip_prefix = "rapidjson-973dc9c06dcd3d035ebd039cfb9ea457721ec213",
    build_file = "@//third_party/rapidjson:BUILD"
)

http_archive(
    name = "com_github_libevent_libevent",
    url = "https://github.com/libevent/libevent/archive/release-2.1.12-stable.zip",
    sha256 = "8836ad722ab211de41cb82fe098911986604f6286f67d10dfb2b6787bf418f49",
    strip_prefix = "libevent-release-2.1.12-stable",
    build_file = "@//third_party/libevent:BUILD",
)

# overriding tensorflow serving bazel dependency
# alternative would be to use cmake build of grpc and flag
# to use system ssl instead
new_local_repository(
    name = "boringssl",
    path = "/usr/",
    build_file = "@//third_party/boringssl:BUILD",
)

new_local_repository(
    name = "linux_curl",
    path = "/usr/",
    build_file_content = """
cc_library(
    name = "curl",
    hdrs = glob(["include/x86_64/curl/*"]),
    srcs = glob(["lib/x86_64-linux-gnu/libcurl.so"]),
    copts = ["-lcrypto", "-lssl"],
    visibility = ["//visibility:public"],
)
""",
)

new_local_repository(
    name = "windows_curl",
    path = "C:\\opt\\curl-8.14.1_1-win64-mingw",
    build_file_content = """
cc_import(
    name = "curl_lib",
    hdrs = [],
    shared_library = "bin/libcurl-x64.dll",
    visibility = ["//visibility:public"],
)
cc_library(
    name = "curl",
    hdrs = glob(["include/curl/curl.h"]),
    srcs = glob(["lib/libcurl.dll.a"]),
    includes = ["include/"],
    visibility = ["//visibility:public"],
    deps = [":curl_lib"],
)
""",
)


# Used for gRPC API protos only
# Tensorflow serving
git_repository(
    name = "tensorflow_serving",
    remote = "https://github.com/tensorflow/serving.git",
    tag = "2.18.0",
    patch_args = ["-p1"],
    patches = ["net_http.patch", "listen.patch", "partial_2.18.patch"]
    #                             ^^^^^^^^^^^^
    #                       make bind address configurable
    #          ^^^^^^^^^^^^
    #        allow all http methods                ^^^^^^^^^
    #                                        implements partial responses
)

########################################################### Mediapipe
http_archive(
    name = "com_google_protobuf",
    sha256 = "87407cd28e7a9c95d9f61a098a53cf031109d451a7763e7dd1253abf8b4df422",
    strip_prefix = "protobuf-3.19.1",
    urls = ["https://github.com/protocolbuffers/protobuf/archive/v3.19.1.tar.gz"],
    patches = [
        "@mediapipe//third_party:com_google_protobuf_fixes.diff"
    ],
    patch_args = [
        "-p1",
    ],
)

################################### Official/forked mediapipe repository #########
#### Will be used on feature release
git_repository(
    name = "mediapipe",
    remote = "https://github.com/openvinotoolkit/mediapipe",
    commit = "45c2fb897206348f78cd1e75eee4a499b9619d9b", # main as of 26 May 2025
)

# DEV mediapipe 1 source - adjust local repository path for build
#local_repository(
#    name = "mediapipe",
#    path = "C:\\git\\mediapipe",
#)

# Protobuf for Node dependencies
http_archive(
    name = "rules_proto_grpc",
    sha256 = "bbe4db93499f5c9414926e46f9e35016999a4e9f6e3522482d3760dc61011070",
    strip_prefix = "rules_proto_grpc-4.2.0",
    urls = ["https://github.com/rules-proto-grpc/rules_proto_grpc/archive/4.2.0.tar.gz"],
)

# Node dependencies
http_archive(
    name = "build_bazel_rules_nodejs",
    sha256 = "5aae76dced38f784b58d9776e4ab12278bc156a9ed2b1d9fcd3e39921dc88fda",
    urls = ["https://github.com/bazelbuild/rules_nodejs/releases/download/5.7.1/rules_nodejs-5.7.1.tar.gz"],
)

load("@build_bazel_rules_nodejs//:repositories.bzl", "build_bazel_rules_nodejs_dependencies")
build_bazel_rules_nodejs_dependencies()

# fetches nodejs, npm, and yarn
load("@build_bazel_rules_nodejs//:index.bzl", "node_repositories", "yarn_install")
node_repositories()
yarn_install(
    name = "npm",
    package_json = "//:package.json",
    yarn_lock = "//:yarn.lock",
)

http_archive(
    name = "com_google_protobuf_javascript",
    sha256 = "35bca1729532b0a77280bf28ab5937438e3dcccd6b31a282d9ae84c896b6f6e3",
    strip_prefix = "protobuf-javascript-3.21.2",
    urls = ["https://github.com/protocolbuffers/protobuf-javascript/archive/refs/tags/v3.21.2.tar.gz"],
)

git_repository( # Using commit past 0.9.0 that adds cmake 3.26.2 for model api. Be sure to update to 0.10.0 when available.
    name = "rules_foreign_cc",
    remote = "https://github.com/bazelbuild/rules_foreign_cc.git",
    commit = "1fb8a1e",
)

load("@rules_foreign_cc//foreign_cc:repositories.bzl", "rules_foreign_cc_dependencies")

rules_foreign_cc_dependencies(cmake_version="3.26.2")

# gflags needed by glog
http_archive(
    name = "com_github_gflags_gflags",
    strip_prefix = "gflags-2.2.2",
    sha256 = "19713a36c9f32b33df59d1c79b4958434cb005b5b47dc5400a7a4b078111d9b5",
    url = "https://github.com/gflags/gflags/archive/v2.2.2.zip",
)

# 2020-08-21 SHA 3a0d4d22c5ae0b9a2216988411cfa6bf860cc372
http_archive(
    name = "com_github_glog_glog",
    strip_prefix = "glog-3a0d4d22c5ae0b9a2216988411cfa6bf860cc372",
    sha256 = "170d08f80210b82d95563f4723a15095eff1aad1863000e8eeb569c96a98fefb",
    urls = [
        "https://github.com/google/glog/archive/3a0d4d22c5ae0b9a2216988411cfa6bf860cc372.zip",
    ],
)

load("@mediapipe//third_party:external_files.bzl", "external_files")
external_files()

new_local_repository(
    name = "linux_openvino",
    build_file = "@//third_party/openvino:BUILD",
    path = "/opt/intel/openvino/runtime",
)

new_local_repository(
    name = "windows_openvino",
    build_file = "@//third_party/openvino:openvino_windows.BUILD",
    path = "C:\\opt\\openvino\\runtime",
)

new_local_repository(
    name = "windows_genai",
    build_file = "@//third_party/genai:genai_windows.BUILD",
    path = "C:\\opt\\openvino\\runtime",
)

new_local_repository(
    name = "linux_genai",
    build_file = "@//third_party/genai:BUILD",
    path = "/opt/intel/openvino/runtime",
)

new_local_repository(
    name = "linux_opencv",
    build_file = "@//third_party/opencv:BUILD",
    path = "/opt/opencv/",
)

new_local_repository(
    name = "windows_opencv",
    build_file = "@//third_party/opencv:opencv_windows.BUILD",
    path = "C:\\opt\\opencv",
)

new_local_repository(
    name = "windows_opencl",
    build_file = "@//third_party/opencl:opencl_windows.BUILD",
    path = "C:\\opt\\opencl\\external\\OpenCL-CLHPP",
)

new_local_repository(
    name = "windows_opencl2",
    build_file = "@//third_party/opencl:opencl_windows2.BUILD",
    path = "C:\\opt\\opencl\\external\\OpenCL-Headers",
)

########################################################### Mediapipe end

########################################################### Python support start

http_archive(
    name = "aspect_bazel_lib",
    sha256 = "7b39d9f38b82260a8151b18dd4a6219d2d7fc4a0ac313d4f5a630ae6907d205d",
    strip_prefix = "bazel-lib-2.10.0",
    url = "https://github.com/bazel-contrib/bazel-lib/releases/download/v2.10.0/bazel-lib-v2.10.0.tar.gz",
)

load("@aspect_bazel_lib//lib:repositories.bzl", "register_coreutils_toolchains")
register_coreutils_toolchains()

load("@//third_party/python:python_repo.bzl", "python_repository")
python_repository(name = "_python3-linux")

load("@//third_party/python:python_repo_win.bzl", "python_repository")
python_repository(name = "_python3-windows")

new_local_repository(
    name = "python3_linux",
    path = "/usr",
    build_file = "@_python3-linux//:BUILD"
)

new_local_repository(
    name = "python3_windows",
    path = "C:\\opt\\",
    build_file = "@_python3-windows//:BUILD"
)

http_archive(
  name = "pybind11_bazel",
  strip_prefix = "pybind11_bazel-b162c7c88a253e3f6b673df0c621aca27596ce6b",
  urls = ["https://github.com/pybind/pybind11_bazel/archive/b162c7c88a253e3f6b673df0c621aca27596ce6b.zip"],
)
# We still require the pybind library.
http_archive(
  name = "pybind11",
  build_file = "@pybind11_bazel//:pybind11.BUILD",
  strip_prefix = "pybind11-2.11.1",
  urls = ["https://github.com/pybind/pybind11/archive/v2.11.1.tar.gz"],
)
load("@pybind11_bazel//:python_configure.bzl", "python_configure")
python_configure(name = "local_config_python")

load("@rules_python//python:repositories.bzl", "py_repositories")
py_repositories()

load("@rules_python//python:pip.bzl", "pip_parse")

pip_parse(
    name = "pip_deps",
    requirements_lock = "//src/python/binding:tests/requirements.txt",
)

load("@pip_deps//:requirements.bzl", "install_deps")

install_deps()

########################################################### Python support end

# minitrace
new_git_repository(
    name = "minitrace",
    remote = "https://github.com/hrydgard/minitrace.git",
    commit = "020f42b189e8d6ad50e4d8f45d69edee0a6b3f23",
    build_file_content = """
cc_library(
    name = "trace",
    hdrs = ["minitrace.h"],
    srcs = ["minitrace.c"],
    visibility = ["//visibility:public"],
    local_defines = [
    ],
)
""",
)

# We need to override upb due to false positive stringop-truncation warning
# second patch is needed & copied from TF
http_archive(
    name = "upb",
    sha256 = "61d0417abd60e65ed589c9deee7c124fe76a4106831f6ad39464e1525cef1454",
    strip_prefix = "upb-9effcbcb27f0a665f9f345030188c0b291e32482",
    patches = [
            "upb_platform_fix.patch",
            "upb_warning_turn_off.patch"
    ],
    patch_args = [
        "-p1",
    ],
    urls = ["https://github.com/protocolbuffers/upb/archive/9effcbcb27f0a665f9f345030188c0b291e32482.tar.gz"],
)

# TensorFlow repo should always go after the other external dependencies.
# TF on 2024-09-24 same as in Mediapipe
_TENSORFLOW_GIT_COMMIT = "5329ec8dd396487982ef3e743f98c0195af39a6b"
_TENSORFLOW_SHA256 = "eb1f8d740d59ea3dee91108ab1fc19d91c4e9ac2fd17d9ab86d865c3c43d81c9"
http_archive(
    name = "org_tensorflow",
    urls = [
      "https://github.com/tensorflow/tensorflow/archive/%s.tar.gz" % _TENSORFLOW_GIT_COMMIT,
    ],
    patches = [
        "@mediapipe//third_party:org_tensorflow_c_api_experimental.diff",
        "@mediapipe//third_party:org_tensorflow_custom_ops.diff",
        "@mediapipe//third_party:org_tensorflow_objc_build_fixes.diff",
        "tf_2.18_logging.patch",
    ],
    patch_args = [
        "-p1",
    ],
    strip_prefix = "tensorflow-%s" % _TENSORFLOW_GIT_COMMIT,
    sha256 = _TENSORFLOW_SHA256,
    repo_mapping = {"@curl" : "@curl"}
)

load("@tensorflow_serving//tensorflow_serving:workspace.bzl", "tf_serving_workspace")

# Initialize TensorFlow's external dependencies.
load("@org_tensorflow//tensorflow:workspace3.bzl", "workspace")
workspace()
# Initialize hermetic Python
load("@org_tensorflow//third_party/xla/third_party/py:python_init_rules.bzl", "python_init_rules")
python_init_rules()

load("@org_tensorflow//third_party/xla/third_party/py:python_init_repositories.bzl", "python_init_repositories")
python_init_repositories(
    default_python_version = "system",
    local_wheel_dist_folder = "dist",
    requirements = {
        "3.9": "@mediapipe//:requirements_lock.txt",
        "3.10": "@mediapipe//:requirements_lock_3_10.txt",
        "3.11": "@mediapipe//:requirements_lock_3_11.txt",
        "3.12": "@mediapipe//:requirements_lock_3_12.txt",
    },
    #local_wheel_inclusion_list = ["mediapipe*"],
    #local_wheel_workspaces = ["//:WORKSPACE"],
)

load("@org_tensorflow//third_party/xla/third_party/py:python_init_toolchains.bzl", "python_init_toolchains")
python_init_toolchains()

load("@org_tensorflow//third_party/xla/third_party/py:python_init_pip.bzl", "python_init_pip")
python_init_pip()

load("@pypi//:requirements.bzl", "install_deps")
install_deps()
# End hermetic Python initialization
load("@org_tensorflow//tensorflow:workspace2.bzl", "workspace")
workspace()
load("@org_tensorflow//tensorflow:workspace1.bzl", "workspace")
workspace()
load("@org_tensorflow//tensorflow:workspace0.bzl", "workspace")
workspace()

tf_serving_workspace() #moved past TF
# required after update to mp 0.10.18
load(
    "@org_tensorflow//third_party/gpus/cuda/hermetic:cuda_configure.bzl",
    "cuda_configure",
)
cuda_configure(name = "local_config_cuda")



# Initialize bazel package rules' external dependencies.
load("@rules_pkg//:deps.bzl", "rules_pkg_dependencies")
rules_pkg_dependencies()

load("@//third_party/aws-sdk-cpp:aws-sdk-cpp.bzl", "aws_sdk_cpp")
aws_sdk_cpp()

### OpenVINO GenAI
load("@//third_party/llm_engine:llm_engine.bzl", "llm_engine")
llm_engine()

### Libgit2
load("@//third_party/libgit2:libgit2_engine.bzl", "libgit2_engine")
libgit2_engine()

load("@//third_party/drogon:drogon.bzl", "drogon_cpp")
drogon_cpp()

# Azure Storage SDK
new_local_repository(
    name = "azure",
    build_file = "@//third_party/azure:BUILD",
    path = "/azure/azure-storage-cpp",
)

# Azure Storage SDK dependency - cpprest
new_local_repository(
    name = "cpprest",
    build_file = "@//third_party/cpprest:BUILD",
    path = "/azure/cpprestsdk",
)

# Boost (needed for Azure Storage SDK)

new_local_repository(
    name = "boost",
    path = "/usr/local/lib/",
    build_file = "@//third_party/boost:BUILD"
)

# Google Cloud SDK
http_archive(
    name = "com_github_googleapis_google_cloud_cpp",
    sha256 = "a370bcf2913717c674a7250c4a310250448ffeb751b930be559a6f1887155f3b",
    strip_prefix = "google-cloud-cpp-0.21.0",
    url = "https://github.com/googleapis/google-cloud-cpp/archive/v0.21.0.tar.gz",
    repo_mapping = {"@com_github_curl_curl" : "@curl"}
)

load("@com_github_googleapis_google_cloud_cpp//bazel:google_cloud_cpp_deps.bzl", "google_cloud_cpp_deps")
google_cloud_cpp_deps()

load("@com_google_googleapis//:repository_rules.bzl", "switched_rules_by_language")
switched_rules_by_language(
    name = "com_google_googleapis_imports",
    cc = True,  # C++ support is only "Partially implemented", roll our own.
    grpc = True,
)

load("@com_github_googleapis_google_cloud_cpp_common//bazel:google_cloud_cpp_common_deps.bzl", "google_cloud_cpp_common_deps")
google_cloud_cpp_common_deps()

load("@com_github_grpc_grpc//bazel:grpc_deps.bzl", "grpc_deps")
grpc_deps()
http_archive( # 1.60.0
    name = "com_github_grpc_grpc",
    urls = [
        "https://github.com/grpc/grpc/archive/0ef13a7555dbaadd4633399242524129eef5e231.tar.gz",
    ],
    strip_prefix = "grpc-0ef13a7555dbaadd4633399242524129eef5e231",
)

# cxxopts
http_archive(
    name = "com_github_jarro2783_cxxopts",
    url = "https://github.com/jarro2783/cxxopts/archive/v3.1.1.zip",
    sha256 = "25b644a2bfa9c6704d723be51b026bc02420dfdee1277a49bfe5df3f19b0eaa4",
    strip_prefix = "cxxopts-3.1.1",
    build_file = "@//third_party/cxxopts:BUILD",
)

# spdlog
http_archive(
    name = "com_github_gabime_spdlog",
    url = "https://github.com/gabime/spdlog/archive/v1.4.0.tar.gz",
    sha256 = "afd18f62d1bc466c60bef088e6b637b0284be88c515cedc59ad4554150af6043",
    strip_prefix = "spdlog-1.4.0",
    build_file = "@//third_party/spdlog:BUILD"
)

# fmtlib
http_archive(
    name = "fmtlib",
    url = "https://github.com/fmtlib/fmt/archive/6.0.0.tar.gz",
    sha256 = "f1907a58d5e86e6c382e51441d92ad9e23aea63827ba47fd647eacc0d3a16c78",
    strip_prefix = "fmt-6.0.0",
    build_file = "@//third_party/fmtlib:BUILD"
)

# prometheus-cpp
http_archive(
    name = "com_github_jupp0r_prometheus_cpp",
    strip_prefix = "prometheus-cpp-1.0.1",
    urls = ["https://github.com/jupp0r/prometheus-cpp/archive/refs/tags/v1.0.1.zip"],
)
load("@com_github_jupp0r_prometheus_cpp//bazel:repositories.bzl", "prometheus_cpp_repositories")
prometheus_cpp_repositories()

load("@rules_foreign_cc//foreign_cc:cmake.bzl", "cmake")
load("@mediapipe//third_party/model_api:model_api.bzl", "workspace_model_api")
workspace_model_api()

new_local_repository(
    name = "mediapipe_calculators",
    build_file = "@//third_party/mediapipe_calculators:BUILD",
    path = "third_party/mediapipe_calculators",
)

git_repository(
    name = "nlohmann_json",
    remote = "https://github.com/nlohmann/json/",
    tag = "v3.11.3",
)
# for rest client in unit tests (server_test.cpp)
git_repository(
    name = "cpp_httplib",
    remote = "https://github.com/yhirose/cpp-httplib/",
    tag = "v0.18.7",
    build_file = "@//third_party/cpp-httplib:BUILD"
)

new_git_repository(
    name = "stb",
    remote = "https://github.com/nothings/stb",
    commit = "5c205738c191bcb0abc65c4febfa9bd25ff35234",
    build_file_content = """
cc_library(
    name = "image",
    hdrs = ["stb_image.h", "stb_image_write.h"],
    visibility = ["//visibility:public"],
    local_defines = [
    ],
)
""",
)
