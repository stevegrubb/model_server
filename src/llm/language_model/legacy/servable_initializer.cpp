//*****************************************************************************
// Copyright 2025 Intel Corporation
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
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "openvino/genai/llm_pipeline.hpp"
#include <openvino/openvino.hpp>
#include <spdlog/spdlog.h>

#pragma warning(push)
#pragma warning(disable : 4005 4309 6001 6385 6386 6326 6011 4005 4456 6246)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#include "mediapipe/framework/calculator_graph.h"
#pragma GCC diagnostic pop
#pragma warning(pop)

#include "../../../json_parser.hpp"
#include "../../../logging.hpp"
#include "../../../mediapipe_internal/mediapipe_utils.hpp"
#include "../../../status.hpp"
#include "servable.hpp"
#include "servable_initializer.hpp"

namespace ovms {
Status LegacyServableInitializer::initialize(std::shared_ptr<GenAiServable>& servable, const mediapipe::LLMCalculatorOptions& nodeOptions, std::string graphPath) {
    std::string parsedModelsPath;
    auto status = parseModelsPath(parsedModelsPath, nodeOptions.models_path(), graphPath);
    if (!status.ok()) {
        return status;
    }

    servable = std::make_shared<LegacyServable>();
    auto properties = std::static_pointer_cast<LegacyServableProperties>(servable->getProperties());

    properties->modelsPath = parsedModelsPath;
    std::filesystem::path modelGenerationConfigPath = std::filesystem::path(parsedModelsPath) / "generation_config.json";
    if (std::filesystem::exists(modelGenerationConfigPath)) {
        properties->baseGenerationConfig = ov::genai::GenerationConfig(modelGenerationConfigPath.string());
    }

    if (nodeOptions.has_response_parser()) {
        properties->responseParserName = nodeOptions.response_parser();
    }

    properties->schedulerConfig.max_num_batched_tokens = nodeOptions.max_num_batched_tokens();
    properties->schedulerConfig.cache_size = nodeOptions.cache_size();
    properties->schedulerConfig.dynamic_split_fuse = nodeOptions.dynamic_split_fuse();
    properties->schedulerConfig.max_num_seqs = nodeOptions.max_num_seqs();
    properties->schedulerConfig.enable_prefix_caching = nodeOptions.enable_prefix_caching();

    properties->device = nodeOptions.device();

    if (nodeOptions.has_draft_max_num_batched_tokens() || nodeOptions.has_draft_cache_size() || nodeOptions.has_draft_dynamic_split_fuse() || nodeOptions.has_draft_max_num_seqs() || nodeOptions.has_draft_block_size() || nodeOptions.has_draft_device()) {
        // Consider moving draft parameters to separate structure in node options, so it's validated on the proto level
        SPDLOG_ERROR("Draft model path is not provided, but draft scheduler options are set.");
        return StatusCode::LLM_NODE_RESOURCE_STATE_INITIALIZATION_FAILED;
    }

    status = JsonParser::parsePluginConfig(nodeOptions.plugin_config(), properties->pluginConfig);
    if (!status.ok()) {
        SPDLOG_ERROR("Error during llm node plugin_config option parsing to JSON: {}", nodeOptions.plugin_config());
        return status;
    }

    // Max prompt len is NPU specific property
    if (properties->device == "NPU") {
        auto it = properties->pluginConfig.find("MAX_PROMPT_LEN");
        if (it != properties->pluginConfig.end()) {
            try {
                properties->maxPromptLength = it->second.as<int64_t>();
            } catch (const std::exception& e) {
                SPDLOG_ERROR("Error during MAX_PROMPT_LEN property read: {}", e.what());
                return StatusCode::LLM_NODE_RESOURCE_STATE_INITIALIZATION_FAILED;
            }
        }
    }

    // Enforce construction of stateful pipeline on any device selected (CPU and GPU by default construct CB pipeline through CB adapter)
    properties->pluginConfig["ATTENTION_BACKEND"] = "SDPA";
    try {
        properties->pipeline = std::make_shared<ov::genai::LLMPipeline>(parsedModelsPath, properties->device, properties->pluginConfig);
        properties->tokenizer = properties->pipeline->get_tokenizer();
    } catch (const std::exception& e) {
        SPDLOG_ERROR("Error during llm node initialization for models_path: {} exception: {}", parsedModelsPath, e.what());
        return StatusCode::LLM_NODE_RESOURCE_STATE_INITIALIZATION_FAILED;
    } catch (...) {
        SPDLOG_ERROR("Error during llm node initialization for models_path: {}", parsedModelsPath);
        return StatusCode::LLM_NODE_RESOURCE_STATE_INITIALIZATION_FAILED;
    }
#if (PYTHON_DISABLE == 0)
    loadPyTemplateProcessor(properties, parsedModelsPath);
#else
    loadDefaultTemplateProcessorIfNeeded(properties);
#endif
    properties->legacyExecutor = std::make_shared<LegacyExecutorWrapper>(properties->pipeline);
    if (nodeOptions.has_max_tokens_limit()) {
        properties->maxTokensLimit = nodeOptions.max_tokens_limit();
    }
    properties->bestOfLimit = nodeOptions.best_of_limit();
    properties->maxModelLength = parseMaxModelLength(parsedModelsPath);
    return StatusCode::OK;
}

}  // namespace ovms
