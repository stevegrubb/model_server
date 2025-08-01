//*****************************************************************************
// Copyright 2023 Intel Corporation
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//*****************************************************************************
#include "mediapipegraphexecutor.hpp"

#include <string>
#include <utility>
#include <vector>

#pragma warning(push)
#pragma warning(disable : 4324 6001 6385 6386 6326 6011 4309 4005 4456 6246)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#include "mediapipe/framework/calculator_graph.h"
#pragma GCC diagnostic pop
#pragma warning(pop)

#if (PYTHON_DISABLE == 0)
#include "../python/python_backend.hpp"
#endif

#include "../image_gen/pipelines.hpp"

namespace ovms {

MediapipeGraphExecutor::MediapipeGraphExecutor(
    const std::string& name,
    const std::string& version,
    const ::mediapipe::CalculatorGraphConfig& config,
    stream_types_mapping_t inputTypes,
    stream_types_mapping_t outputTypes,
    std::vector<std::string> inputNames,
    std::vector<std::string> outputNames,
    const PythonNodeResourcesMap& pythonNodeResourcesMap,
    const GenAiServableMap& llmNodeResourcesMap,
    const EmbeddingsServableMap& embeddingsServableMap,
    const RerankServableMap& rerankServableMap,
    PythonBackend* pythonBackend,
    MediapipeServableMetricReporter* mediapipeServableMetricReporter) :
    name(name),
    version(version),
    config(config),
    inputTypes(std::move(inputTypes)),
    outputTypes(std::move(outputTypes)),
    inputNames(std::move(inputNames)),
    outputNames(std::move(outputNames)),
    sidePacketMaps({pythonNodeResourcesMap, llmNodeResourcesMap, {}, embeddingsServableMap, rerankServableMap}),
    pythonBackend(pythonBackend),
    currentStreamTimestamp(STARTING_TIMESTAMP),
    mediapipeServableMetricReporter(mediapipeServableMetricReporter) {}
MediapipeGraphExecutor::MediapipeGraphExecutor(
    const std::string& name,
    const std::string& version,
    const ::mediapipe::CalculatorGraphConfig& config,
    stream_types_mapping_t inputTypes,
    stream_types_mapping_t outputTypes,
    std::vector<std::string> inputNames,
    std::vector<std::string> outputNames,
    const GraphSidePackets& sidePacketMaps,
    PythonBackend* pythonBackend,
    MediapipeServableMetricReporter* mediapipeServableMetricReporter) :
    name(name),
    version(version),
    config(config),
    inputTypes(std::move(inputTypes)),
    outputTypes(std::move(outputTypes)),
    inputNames(std::move(inputNames)),
    outputNames(std::move(outputNames)),
    sidePacketMaps(sidePacketMaps),
    pythonBackend(pythonBackend),
    currentStreamTimestamp(STARTING_TIMESTAMP),
    mediapipeServableMetricReporter(mediapipeServableMetricReporter) {}

const std::string MediapipeGraphExecutor::PYTHON_SESSION_SIDE_PACKET_TAG = "py";
const std::string MediapipeGraphExecutor::LLM_SESSION_SIDE_PACKET_TAG = "llm";
const std::string MediapipeGraphExecutor::IMAGE_GEN_SESSION_SIDE_PACKET_TAG = "pipes";
const std::string MediapipeGraphExecutor::EMBEDDINGS_SESSION_SIDE_PACKET_TAG = "embeddings_servable";
const std::string MediapipeGraphExecutor::RERANK_SESSION_SIDE_PACKET_TAG = "rerank_servable";
const ::mediapipe::Timestamp MediapipeGraphExecutor::STARTING_TIMESTAMP = ::mediapipe::Timestamp(0);

}  // namespace ovms
