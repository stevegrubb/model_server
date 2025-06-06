//*****************************************************************************
// Copyright 2020 Intel Corporation
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
#include <filesystem>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "../dags/entry_node.hpp"
#include "../dags/exit_node.hpp"
#include "../dags/pipeline.hpp"
#include "../dags/pipeline_factory.hpp"
#include "../dags/pipelinedefinition.hpp"
#include "../execution_context.hpp"
#include "../get_model_metadata_impl.hpp"
#include "../kfs_frontend/kfs_grpc_inference_service.hpp"
#include "../model_metric_reporter.hpp"
#include "../modelconfig.hpp"
#include "../modelmanager.hpp"
#include "test_utils.hpp"

using namespace ovms;

using ::testing::ElementsAre;

/*
 * Scenario with pipelines with underlying DL models loaded with mapping_config.json.
 * This set of tests ensures model input/output mapping is respected by pipeline execution.
 */

class PipelineWithInputOutputNameMappedModel : public TestWithTempDir {
protected:
    void SetUp() override {
        TestWithTempDir::SetUp();

        configPath = directoryPath + "/config.json";
        modelPath = directoryPath + "/dummy";
        mappingConfigPath = modelPath + "/1/mapping_config.json";

        std::filesystem::copy(getGenericFullPathForSrcTest("/ovms/src/test/dummy"), modelPath, std::filesystem::copy_options::recursive);
    }

    std::string configPath;
    std::string modelPath;
    std::string mappingConfigPath;

    ConstructorEnabledModelManager managerWithDummyModel;
};

TEST_F(PipelineWithInputOutputNameMappedModel, SuccessfullyReferToMappedNamesAndExecute) {
    // Create mapping config for model
    createConfigFileWithContent(R"({
        "inputs": {"b": "input_tensor"},
        "outputs": {"a": "output_tensor"}
    })",
        mappingConfigPath);

    // Load models
    auto modelConfig = DUMMY_MODEL_CONFIG;
    adjustConfigToAllowModelFileRemovalWhenLoaded(modelConfig);
    modelConfig.setBasePath(getGenericFullPathForSrcTest(modelPath));
    ASSERT_EQ(managerWithDummyModel.reloadModelWithVersions(modelConfig), StatusCode::OK_RELOADED);

    // Create pipeline definition
    PipelineFactory factory;
    std::vector<NodeInfo> info{
        {NodeKind::ENTRY, ENTRY_NODE_NAME, "", std::nullopt, {{"vector", "vector"}}},
        {NodeKind::DL, "dummyA", "dummy", std::nullopt, {{"output_tensor", "output_tensor"}}},
        {NodeKind::DL, "dummyB", "dummy", std::nullopt, {{"output_tensor", "output_tensor"}}},
        {NodeKind::EXIT, EXIT_NODE_NAME},
    };

    pipeline_connections_t connections;

    connections["dummyA"] = {
        {ENTRY_NODE_NAME, {{"vector", "input_tensor"}}}};
    connections["dummyB"] = {
        {"dummyA", {{"output_tensor", "input_tensor"}}}};
    connections[EXIT_NODE_NAME] = {
        {"dummyB", {{"output_tensor", "response_tensor_name"}}}};

    // Ensure definition created without errors
    ASSERT_EQ(factory.createDefinition("pipeline", info, connections, managerWithDummyModel), StatusCode::OK);

    // Prepare request
    std::unique_ptr<Pipeline> pipeline;
    tensorflow::serving::PredictRequest request;
    tensorflow::serving::PredictResponse response;
    auto& input_proto = (*request.mutable_inputs())["vector"];
    input_proto.set_dtype(tensorflow::DataType::DT_FLOAT);
    input_proto.mutable_tensor_shape()->add_dim()->set_size(1);
    input_proto.mutable_tensor_shape()->add_dim()->set_size(DUMMY_MODEL_INPUT_SIZE);

    std::vector<float> input_data(DUMMY_MODEL_INPUT_SIZE);
    std::iota(input_data.begin(), input_data.end(), 1);  // 1, 2, 3, ..., 10
    input_proto.mutable_tensor_content()->assign((char*)input_data.data(), sizeof(float) * DUMMY_MODEL_INPUT_SIZE);

    // Execute pipeline
    factory.create(pipeline, "pipeline", &request, &response, managerWithDummyModel);
    ASSERT_EQ(pipeline->execute(DEFAULT_TEST_CONTEXT), StatusCode::OK);

    // Compare response
    ASSERT_EQ(response.outputs_size(), 1);
    ASSERT_EQ(response.outputs().count("response_tensor_name"), 1);
    const auto& output_proto = response.outputs().at("response_tensor_name");
    ASSERT_EQ(output_proto.dtype(), tensorflow::DataType::DT_FLOAT);
    ASSERT_THAT(asVector(output_proto.tensor_shape()), ElementsAre(1, 10));

    std::vector<float> output_data(DUMMY_MODEL_INPUT_SIZE);
    std::transform(input_data.begin(), input_data.end(), output_data.begin(), [](float& v) { return v + 2.0; });  // 3, 4, ... 12
    ASSERT_EQ(asVector<float>(output_proto.tensor_content()), output_data);
}

TEST_F(PipelineWithInputOutputNameMappedModel, ReferingToOriginalInputNameFailsCreation) {
    // Create mapping config for model
    createConfigFileWithContent(R"({
        "inputs": {"b": "input_tensor"},
        "outputs": {"a": "output_tensor"}
    })",
        mappingConfigPath);

    // Load models
    auto modelConfig = DUMMY_MODEL_CONFIG;
    adjustConfigToAllowModelFileRemovalWhenLoaded(modelConfig);
    modelConfig.setBasePath(getGenericFullPathForSrcTest(modelPath));
    ASSERT_EQ(managerWithDummyModel.reloadModelWithVersions(modelConfig), StatusCode::OK_RELOADED);

    // Create pipeline definition
    PipelineFactory factory;

    std::vector<NodeInfo> info{
        {NodeKind::ENTRY, ENTRY_NODE_NAME, "", std::nullopt, {{"vector", "vector"}}},
        {NodeKind::DL, "dummyA", "dummy", std::nullopt, {{"output_tensor", "output_tensor"}}},
        {NodeKind::DL, "dummyB", "dummy", std::nullopt, {{"output_tensor", "output_tensor"}}},
        {NodeKind::EXIT, EXIT_NODE_NAME},
    };

    pipeline_connections_t connections;

    connections["dummyA"] = {
        {ENTRY_NODE_NAME, {{"vector", "b"}}}};
    connections["dummyB"] = {
        {"dummyA", {{"output_tensor", "b"}}}};
    connections[EXIT_NODE_NAME] = {
        {"dummyB", {{"output_tensor", "response_tensor_name"}}}};

    EXPECT_EQ(factory.createDefinition("pipeline", info, connections, managerWithDummyModel), StatusCode::PIPELINE_CONNECTION_TO_MISSING_MODEL_INPUT);
}

TEST_F(PipelineWithInputOutputNameMappedModel, ReferingToOriginalOutputNameFailsCreation) {
    // Create mapping config for model
    createConfigFileWithContent(R"({
        "inputs": {"b": "input_tensor"},
        "outputs": {"a": "output_tensor"}
    })",
        mappingConfigPath);

    // Load models
    auto modelConfig = DUMMY_MODEL_CONFIG;
    adjustConfigToAllowModelFileRemovalWhenLoaded(modelConfig);
    modelConfig.setBasePath(getGenericFullPathForSrcTest(modelPath));
    ASSERT_EQ(managerWithDummyModel.reloadModelWithVersions(modelConfig), StatusCode::OK_RELOADED);

    // Create pipeline definition
    PipelineFactory factory;

    std::vector<NodeInfo> info{
        {NodeKind::ENTRY, ENTRY_NODE_NAME, "", std::nullopt, {{"vector", "vector"}}},
        {NodeKind::DL, "dummyA", "dummy", std::nullopt, {{"output_tensor", "a"}}},
        {NodeKind::DL, "dummyB", "dummy", std::nullopt, {{"output_tensor", "a"}}},
        {NodeKind::EXIT, EXIT_NODE_NAME},
    };

    pipeline_connections_t connections;

    connections["dummyA"] = {
        {ENTRY_NODE_NAME, {{"vector", "input_tensor"}}}};
    connections["dummyB"] = {
        {"dummyA", {{"output_tensor", "input_tensor"}}}};
    connections[EXIT_NODE_NAME] = {
        {"dummyB", {{"output_tensor", "response_tensor_name"}}}};

    EXPECT_EQ(factory.createDefinition("pipeline", info, connections, managerWithDummyModel), StatusCode::PIPELINE_NODE_REFERING_TO_MISSING_MODEL_OUTPUT);
}

TEST_F(PipelineWithInputOutputNameMappedModel, SuccessfullyReferToMappedNamesAndGetMetadata) {
    // Create mapping config for model
    createConfigFileWithContent(R"({
        "inputs": {"b": "input_tensor"},
        "outputs": {"a": "output_tensor"}
    })",
        mappingConfigPath);

    // Load models
    auto modelConfig = DUMMY_MODEL_CONFIG;
    adjustConfigToAllowModelFileRemovalWhenLoaded(modelConfig);
    modelConfig.setBasePath(getGenericFullPathForSrcTest(modelPath));
    ASSERT_EQ(managerWithDummyModel.reloadModelWithVersions(modelConfig), StatusCode::OK_RELOADED);

    std::vector<NodeInfo> info{
        {NodeKind::ENTRY, ENTRY_NODE_NAME, "", std::nullopt, {{"vector", "vector"}}},
        {NodeKind::DL, "dummyA", "dummy", std::nullopt, {{"output_tensor", "output_tensor"}}},
        {NodeKind::DL, "dummyB", "dummy", std::nullopt, {{"output_tensor", "output_tensor"}}},
        {NodeKind::EXIT, EXIT_NODE_NAME},
    };

    pipeline_connections_t connections;

    connections["dummyA"] = {
        {ENTRY_NODE_NAME, {{"vector", "input_tensor"}}}};
    connections["dummyB"] = {
        {"dummyA", {{"output_tensor", "input_tensor"}}}};
    connections[EXIT_NODE_NAME] = {
        {"dummyB", {{"output_tensor", "response_tensor_name"}}}};

    auto def = std::make_unique<PipelineDefinition>(
        "my_new_pipeline", info, connections);

    ASSERT_EQ(def->validate(managerWithDummyModel), StatusCode::OK);

    auto inputs = def->getInputsInfo();
    auto outputs = def->getOutputsInfo();
    ASSERT_EQ(inputs.size(), 1);
    ASSERT_EQ(outputs.size(), 1);
    ASSERT_NE(inputs.find("vector"), inputs.end());
    ASSERT_NE(outputs.find("response_tensor_name"), outputs.end());

    const auto& vector = inputs.at("vector");
    EXPECT_EQ(vector->getShape(), ovms::Shape({1, DUMMY_MODEL_INPUT_SIZE}));
    EXPECT_EQ(vector->getPrecision(), ovms::Precision::FP32);

    const auto& response_tensor_name = outputs.at("response_tensor_name");
    EXPECT_EQ(response_tensor_name->getShape(), ovms::Shape({1, DUMMY_MODEL_OUTPUT_SIZE}));
    EXPECT_EQ(response_tensor_name->getPrecision(), ovms::Precision::FP32);
}

TEST_F(PipelineWithInputOutputNameMappedModel, SuccessfullyReloadPipelineAfterAddingModelMapping) {
    // Load models
    auto modelConfig = DUMMY_MODEL_CONFIG;
    adjustConfigToAllowModelFileRemovalWhenLoaded(modelConfig);
    modelConfig.setBasePath(getGenericFullPathForSrcTest(modelPath));
    ASSERT_EQ(managerWithDummyModel.reloadModelWithVersions(modelConfig), StatusCode::OK_RELOADED);

    // Create pipeline definition
    std::vector<NodeInfo> info{
        {NodeKind::ENTRY, ENTRY_NODE_NAME, "", std::nullopt, {{"vector", "vector"}}},
        {NodeKind::DL, "dummyA", "dummy", std::nullopt, {{"output_tensor", "output_tensor"}}},
        {NodeKind::DL, "dummyB", "dummy", std::nullopt, {{"output_tensor", "output_tensor"}}},
        {NodeKind::EXIT, EXIT_NODE_NAME},
    };
    pipeline_connections_t connections;

    connections["dummyA"] = {
        {ENTRY_NODE_NAME, {{"vector", "input_tensor"}}}};
    connections["dummyB"] = {
        {"dummyA", {{"output_tensor", "input_tensor"}}}};
    connections[EXIT_NODE_NAME] = {
        {"dummyB", {{"output_tensor", "response_tensor_name"}}}};

    // Validation fails since mapping is expected
    PipelineDefinition pd("UNUSED_NAME", info, connections);
    auto status = pd.validate(managerWithDummyModel);
    EXPECT_TRUE(status.getCode() == ovms::StatusCode::PIPELINE_CONNECTION_TO_MISSING_MODEL_INPUT)
        << status.string();

    // Create mapping config for model
    createConfigFileWithContent(R"({
        "inputs": {"b": "input_tensor"},
        "outputs": {"a": "output_tensor"}
    })",
        mappingConfigPath);

    // reload pipeline definition after adding mapping
    // we need to trigger reload - just adding model mapping won't do the trick
    // hence setting nireq
    modelConfig.setNireq(modelConfig.getNireq() + 1);
    status = managerWithDummyModel.reloadModelWithVersions(modelConfig);
    ASSERT_TRUE(status.ok()) << status.string();
    status = pd.reload(managerWithDummyModel, std::move(info), std::move(connections));
    ASSERT_TRUE(status.ok()) << status.string();

    // Prepare request
    std::unique_ptr<Pipeline> pipeline;
    tensorflow::serving::PredictRequest request;
    tensorflow::serving::PredictResponse response;
    auto& input_proto = (*request.mutable_inputs())["vector"];
    input_proto.set_dtype(tensorflow::DataType::DT_FLOAT);
    input_proto.mutable_tensor_shape()->add_dim()->set_size(1);
    input_proto.mutable_tensor_shape()->add_dim()->set_size(DUMMY_MODEL_INPUT_SIZE);

    std::vector<float> input_data(DUMMY_MODEL_INPUT_SIZE);
    std::iota(input_data.begin(), input_data.end(), 1);  // 1, 2, 3, ..., 10
    input_proto.mutable_tensor_content()->assign((char*)input_data.data(), sizeof(float) * DUMMY_MODEL_INPUT_SIZE);

    // Execute pipeline
    pd.create(pipeline, &request, &response, managerWithDummyModel);
    ASSERT_EQ(pipeline->execute(DEFAULT_TEST_CONTEXT), StatusCode::OK);

    // Compare response
    ASSERT_EQ(response.outputs_size(), 1);
    ASSERT_EQ(response.outputs().count("response_tensor_name"), 1);
    const auto& output_proto = response.outputs().at("response_tensor_name");
    ASSERT_EQ(output_proto.dtype(), tensorflow::DataType::DT_FLOAT);
    ASSERT_THAT(asVector(output_proto.tensor_shape()), ElementsAre(1, 10));

    std::vector<float> output_data(DUMMY_MODEL_INPUT_SIZE);
    std::transform(input_data.begin(), input_data.end(), output_data.begin(), [](float& v) { return v + 2.0; });  // 3, 4, ... 12
    ASSERT_EQ(asVector<float>(output_proto.tensor_content()), output_data);
}

TEST_F(PipelineWithInputOutputNameMappedModel, ReloadPipelineAfterRemovalOfModelMappingWillFail) {
    // Create mapping config for model
    createConfigFileWithContent(R"({
        "inputs": {"b": "input_tensor"},
        "outputs": {"a": "output_tensor"}
    })",
        mappingConfigPath);
    // Load models
    auto modelConfig = DUMMY_MODEL_CONFIG;
    adjustConfigToAllowModelFileRemovalWhenLoaded(modelConfig);
    modelConfig.setBasePath(getGenericFullPathForSrcTest(modelPath));
    ASSERT_EQ(managerWithDummyModel.reloadModelWithVersions(modelConfig), StatusCode::OK_RELOADED);

    // Create pipeline definition
    std::vector<NodeInfo> info{
        {NodeKind::ENTRY, ENTRY_NODE_NAME, "", std::nullopt, {{"vector", "vector"}}},
        {NodeKind::DL, "dummyA", "dummy", std::nullopt, {{"output_tensor", "output_tensor"}}},
        {NodeKind::DL, "dummyB", "dummy", std::nullopt, {{"output_tensor", "output_tensor"}}},
        {NodeKind::EXIT, EXIT_NODE_NAME},
    };
    pipeline_connections_t connections;

    connections["dummyA"] = {
        {ENTRY_NODE_NAME, {{"vector", "input_tensor"}}}};
    connections["dummyB"] = {
        {"dummyA", {{"output_tensor", "input_tensor"}}}};
    connections[EXIT_NODE_NAME] = {
        {"dummyB", {{"output_tensor", "response_tensor_name"}}}};

    // Ensure definition created without errors
    PipelineDefinition pd("UNUSED_NAME", info, connections);
    auto status = pd.validate(managerWithDummyModel);
    ASSERT_TRUE(status.ok()) << status.string();

    // reload pipeline definition after removing mapping
    // we need to trigger reload - just adding model mapping won't do the trick
    // hence setting nireq
    ASSERT_TRUE(std::filesystem::remove(mappingConfigPath));
    modelConfig.setNireq(modelConfig.getNireq() + 1);
    status = managerWithDummyModel.reloadModelWithVersions(modelConfig);
    ASSERT_TRUE(status.ok()) << status.string();
    status = pd.reload(managerWithDummyModel, std::move(info), std::move(connections));
    EXPECT_TRUE(status.getCode() == ovms::StatusCode::PIPELINE_CONNECTION_TO_MISSING_MODEL_INPUT)
        << status.string();
}

class ModelWithInputOutputNameMappedModel : public PipelineWithInputOutputNameMappedModel {
public:
    KFSModelMetadataResponse kfsResponse;
    tensorflow::serving::GetModelMetadataResponse tfsResponse;
    std::shared_ptr<ModelInstance> instance;
};

TEST_F(ModelWithInputOutputNameMappedModel, GetModelMetadataOnKFSEndpoint) {
    // Create mapping config for model
    createConfigFileWithContent(R"({
        "inputs": {"b": "input_tensor"},
        "outputs": {"a": "output_tensor"}
    })",
        mappingConfigPath);

    // Load models
    auto modelConfig = DUMMY_MODEL_CONFIG;
    adjustConfigToAllowModelFileRemovalWhenLoaded(modelConfig);
    modelConfig.setBasePath(getGenericFullPathForSrcTest(modelPath));
    ASSERT_EQ(managerWithDummyModel.reloadModelWithVersions(modelConfig), StatusCode::OK_RELOADED);

    auto model = managerWithDummyModel.findModelByName("dummy");
    ASSERT_NE(model, nullptr);
    instance = model->getDefaultModelInstance();
    ASSERT_NE(instance, nullptr);
    KFSModelExtraMetadata extraMetadata;
    ASSERT_EQ(ovms::KFSInferenceServiceImpl::buildResponse(*model, *instance, &kfsResponse, extraMetadata), ovms::StatusCode::OK);

    EXPECT_EQ(kfsResponse.inputs_size(), 1);
    auto input = kfsResponse.inputs().at(0);
    EXPECT_EQ(input.name(), "input_tensor");

    EXPECT_EQ(kfsResponse.outputs_size(), 1);
    auto output = kfsResponse.outputs().at(0);
    EXPECT_EQ(output.name(), "output_tensor");
}

TEST_F(ModelWithInputOutputNameMappedModel, GetModelMetadataOnTfsEndpoint) {
    // Create mapping config for model
    createConfigFileWithContent(R"({
        "inputs": {"b": "input_tensor"},
        "outputs": {"a": "output_tensor"}
    })",
        mappingConfigPath);

    // Load models
    auto modelConfig = DUMMY_MODEL_CONFIG;
    adjustConfigToAllowModelFileRemovalWhenLoaded(modelConfig);
    modelConfig.setBasePath(getGenericFullPathForSrcTest(modelPath));
    ASSERT_EQ(managerWithDummyModel.reloadModelWithVersions(modelConfig), StatusCode::OK_RELOADED);

    auto model = managerWithDummyModel.findModelByName("dummy");
    ASSERT_NE(model, nullptr);
    instance = model->getDefaultModelInstance();
    ASSERT_NE(instance, nullptr);

    ASSERT_EQ(ovms::GetModelMetadataImpl::buildResponse(instance, &tfsResponse), ovms::StatusCode::OK);

    tensorflow::serving::SignatureDefMap def;
    tfsResponse.metadata().at("signature_def").UnpackTo(&def);

    const auto& inputs = ((*def.mutable_signature_def())["serving_default"]).inputs();
    const auto& outputs = ((*def.mutable_signature_def())["serving_default"]).outputs();

    EXPECT_EQ(inputs.size(), 1);
    EXPECT_EQ(inputs.at("input_tensor").name(), "input_tensor");

    EXPECT_EQ(outputs.size(), 1);
    EXPECT_EQ(outputs.at("output_tensor").name(), "output_tensor");
}
