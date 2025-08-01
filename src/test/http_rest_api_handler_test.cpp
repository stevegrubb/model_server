//*****************************************************************************
// Copyright 2021 Intel Corporation
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
#include <gtest/gtest.h>

#include "../config.hpp"
#include "../http_rest_api_handler.hpp"
#include "../localfilesystem.hpp"
#include "../logging.hpp"
#include "../modelmanager.hpp"
#include "../servablemanagermodule.hpp"
#include "../server.hpp"
#include "test_utils.hpp"

static const char* configWith1Dummy = R"(
{
    "model_config_list": [
        {
            "config": {
                "name": "dummy",
                "base_path": "/ovms/src/test/dummy"
            }
        }
    ]
})";

static const char* configWith1DummyNew = R"(
{
    "model_config_list": [
        {
            "config": {
                "name": "dummy",
                "base_path": "/ovms/src/test/dummy",
		"batch_size": "16"
            }
        }
    ]
})";

class ModelManagerTest : public ovms::ModelManager {};

class ServerShutdownGuard {
    ovms::Server& ovmsServer;

public:
    ServerShutdownGuard(ovms::Server& ovmsServer) :
        ovmsServer(ovmsServer) {}
    ~ServerShutdownGuard() {
        ovmsServer.shutdownModules();
    }
};

class ConfigApi : public TestWithTempDir {
    std::string configFilePath;
    std::string modelPath;
    std::string modelName;

public:
    void SetUpConfig(const std::string& configContent) {
        std::string adjustedConfigContent = configContent;
        adjustConfigForTargetPlatform(adjustedConfigContent);
        configFilePath = directoryPath + "/ovms_config.json";
        createConfigFileWithContent(adjustedConfigContent, configFilePath);
        std::string port{"9000"};
        randomizeAndEnsureFree(port);
        char* n_argv[] = {(char*)"ovms", (char*)"--config_path", (char*)configFilePath.data(), (char*)"--file_system_poll_wait_seconds", (char*)"0", (char*)"--port", (char*)port.c_str()};
        int arg_count = 7;
        ovms::Config::instance().parse(arg_count, n_argv);
    }

    void LoadConfig(ovms::ModelManager& manager) {
        manager.startFromFile(configFilePath);
    }

    void UnloadConfig(ovms::ModelManager& manager) {
        std::string configContent = R"({
            "model_config_list": []
        })";
        createConfigFileWithContent(configContent, configFilePath);
        manager.startFromFile(configFilePath);
    }

    void RemoveConfig() {
        std::filesystem::remove(configFilePath);
    }

    void SetUpSingleModel(std::string modelPath, std::string modelName) {
        this->modelPath = modelPath;
        this->modelName = modelName;
        std::string port{"9000"};
        randomizeAndEnsureFree(port);
        char* n_argv[] = {(char*)"ovms", (char*)"--model_path", (char*)this->modelPath.data(), (char*)"--model_name", (char*)this->modelName.data(), (char*)"--file_system_poll_wait_seconds", (char*)"0", (char*)"--port", (char*)port.c_str()};
        int arg_count = 9;
        ovms::Config::instance().parse(arg_count, n_argv);
    }
    struct TestHelper1 {
        std::unique_ptr<ServerShutdownGuard> serverGuard;
        TestHelper1(ConfigApi& configApi, const char* configJson = nullptr) {
            ovms::Server& ovmsServer = ovms::Server::instance();
            if (configJson)
                configApi.SetUpConfig(configJson);
            else
                configApi.SetUpSingleModel(getGenericFullPathForSrcTest("/ovms/src/test/dummy"), "dummy");

            auto& config = ovms::Config::instance();
            auto retCode = ovmsServer.startModules(config);
            EXPECT_TRUE(retCode.ok()) << retCode.string();
            if (!retCode.ok())
                throw std::runtime_error("Failed to start modules");
            serverGuard = std::make_unique<ServerShutdownGuard>(ovmsServer);
            auto modulePtr = ovmsServer.getModule(ovms::SERVABLE_MANAGER_MODULE_NAME);
            EXPECT_NE(nullptr, modulePtr);
            if (!modulePtr)
                throw std::runtime_error("Failed to get module");
            auto servableManagerModule = dynamic_cast<const ovms::ServableManagerModule*>(modulePtr);
            EXPECT_NE(nullptr, servableManagerModule);
            if (!servableManagerModule)
                throw std::runtime_error("Failed to get servable module");
            ovms::ModelManager& manager = servableManagerModule->getServableManager();
            configApi.LoadConfig(manager);
        }
        ovms::ModelManager& getManager() {
            ovms::Server& ovmsServer = ovms::Server::instance();
            auto modulePtr = ovmsServer.getModule(ovms::SERVABLE_MANAGER_MODULE_NAME);
            EXPECT_NE(nullptr, modulePtr);
            if (!modulePtr)
                throw std::runtime_error("Failed to get module");
            auto servableManagerModule = dynamic_cast<const ovms::ServableManagerModule*>(modulePtr);
            EXPECT_NE(nullptr, servableManagerModule);
            if (!servableManagerModule)
                throw std::runtime_error("Failed to get servable module");
            return servableManagerModule->getServableManager();
        }
    };
};

class ConfigReload : public ConfigApi {
};

TEST_F(ConfigReload, nonExistingConfigFile) {
    ovms::Server& ovmsServer = ovms::Server::instance();
    TestHelper1 t(*this, configWith1Dummy);
    auto handler = ovms::HttpRestApiHandler(ovmsServer, 10);
    std::string response;
    RemoveConfig();
    auto status = handler.processConfigReloadRequest(response, t.getManager());
    const char* expectedJson = "{\n\t\"error\": \"Config file not found or cannot open.\"\n}";
    EXPECT_EQ(expectedJson, response);
    EXPECT_EQ(status, ovms::StatusCode::CONFIG_FILE_TIMESTAMP_READING_FAILED);
}

static const char* configWithModelNonExistingPath = R"(
{
    "model_config_list": [
        {
            "config": {
                "name": "dummy",
                "base_path": "/non/existing"
            }
        }
    ]
})";

TEST_F(ConfigReload, nonExistingModelPathInConfig) {
    ovms::Server& ovmsServer = ovms::Server::instance();
    TestHelper1 t(*this, configWith1Dummy);
    auto handler = ovms::HttpRestApiHandler(ovmsServer, 10);
    std::string response;
    RemoveConfig();
    SetUpConfig(configWithModelNonExistingPath);

    auto status = handler.processConfigReloadRequest(response, t.getManager());
    const char* expectedJson = "{\n\t\"error\": \"Reloading config file failed. Check server logs for more info.\"\n}";
    EXPECT_EQ(expectedJson, response);
    EXPECT_EQ(status, ovms::StatusCode::PATH_INVALID);
}

static const char* configWithDuplicatedModelName = R"(
{
    "model_config_list": [
        {
            "config": {
                "name": "dummy",
                "base_path": "/ovms/src/test/dummy"
            }
        },
        {
            "config": {
                "name": "dummy",
                "base_path": "/ovms/src/test/add_two_inputs_model"
            }
        }
    ]
})";

TEST_F(ConfigReload, duplicatedModelNameInConfig) {
    ovms::Server& ovmsServer = ovms::Server::instance();
    TestHelper1 t(*this, configWithDuplicatedModelName);
    auto handler = ovms::HttpRestApiHandler(ovmsServer, 10);
    std::string response;

    auto status = handler.processConfigReloadRequest(response, t.getManager());
    const char* expectedJson = "{\n\t\"error\": \"Reloading config file failed. Check server logs for more info.\"\n}";
    EXPECT_EQ(expectedJson, response);
    EXPECT_EQ(status, ovms::StatusCode::MODEL_NAME_OCCUPIED);
}

TEST_F(ConfigReload, startWith1DummyThenReload) {
    ovms::Server& ovmsServer = ovms::Server::instance();
    TestHelper1 t(*this, configWith1Dummy);
    auto handler = ovms::HttpRestApiHandler(ovmsServer, 10);
    std::string response;

    LoadConfig(t.getManager());
    RemoveConfig();
    SetUpConfig(configWith1DummyNew);

    const char* expectedJson = R"({
"dummy" : 
{
 "model_version_status": [
  {
   "version": "1",
   "state": "AVAILABLE",
   "status": {
    "error_code": "OK",
    "error_message": "OK"
   }
  }
 ]
}
})";
    auto status = handler.processConfigReloadRequest(response, t.getManager());

    EXPECT_EQ(expectedJson, response);
    EXPECT_EQ(status, ovms::StatusCode::OK_RELOADED);
}

TEST_F(ConfigReload, singleModel) {
    ovms::Server& ovmsServer = ovms::Server::instance();
    TestHelper1 t(*this);
    auto handler = ovms::HttpRestApiHandler(ovmsServer, 10);
    std::string response;

    auto status = handler.processConfigReloadRequest(response, t.getManager());

    const char* expectedJson = R"({
"dummy" : 
{
 "model_version_status": [
  {
   "version": "1",
   "state": "AVAILABLE",
   "status": {
    "error_code": "OK",
    "error_message": "OK"
   }
  }
 ]
}
})";

    EXPECT_EQ(expectedJson, response);
    EXPECT_EQ(status, ovms::StatusCode::OK_NOT_RELOADED);
}

// Plugin config set to be able to remove the model from disk when model is still loaded
static const char* configWith1DummyInTmp = R"(
{
    "model_config_list": [
        {
            "config": {
                "name": "dummy",
                "base_path": "/tmp/dummy",
                "plugin_config": {"ENABLE_MMAP": "NO"}
            }
        }
    ]
})";

TEST_F(ConfigReload, startWith1DummyThenAddVersion) {
    ovms::Server& ovmsServer = ovms::Server::instance();
    std::filesystem::remove_all(getGenericFullPathForTmp("/tmp/dummy"));
    std::filesystem::copy(getGenericFullPathForSrcTest("/ovms/src/test/dummy"), getGenericFullPathForTmp("/tmp/dummy"), std::filesystem::copy_options::recursive);
    TestHelper1 t(*this, configWith1DummyInTmp);
    auto handler = ovms::HttpRestApiHandler(ovmsServer, 10);

    std::string response;

    LoadConfig(t.getManager());

    const char* expectedJson1 = R"({
"dummy" : 
{
 "model_version_status": [
  {
   "version": "1",
   "state": "AVAILABLE",
   "status": {
    "error_code": "OK",
    "error_message": "OK"
   }
  }
 ]
}
})";
    auto status = handler.processConfigReloadRequest(response, t.getManager());

    EXPECT_EQ(expectedJson1, response);
    EXPECT_EQ(status, ovms::StatusCode::OK_NOT_RELOADED);

    std::filesystem::copy(getGenericFullPathForSrcTest("/ovms/src/test/dummy/1"), getGenericFullPathForTmp("/tmp/dummy/2"), std::filesystem::copy_options::recursive);

    const char* expectedJson2 = R"({
"dummy" : 
{
 "model_version_status": [
  {
   "version": "1",
   "state": "END",
   "status": {
    "error_code": "OK",
    "error_message": "OK"
   }
  },
  {
   "version": "2",
   "state": "AVAILABLE",
   "status": {
    "error_code": "OK",
    "error_message": "OK"
   }
  }
 ]
}
})";
    status = handler.processConfigReloadRequest(response, t.getManager());

    EXPECT_EQ(expectedJson2, response);
    EXPECT_EQ(status, ovms::StatusCode::OK_RELOADED);

    // Remove the model from disk when model is still loaded
    std::filesystem::remove_all(getGenericFullPathForTmp("/tmp/dummy"));
}

TEST_F(ConfigReload, startWithMissingXmlThenAddAndReload) {
    ovms::Server& ovmsServer = ovms::Server::instance();
    std::filesystem::remove_all(getGenericFullPathForTmp("/tmp/dummy"));
    std::filesystem::create_directory(getGenericFullPathForTmp("/tmp/dummy"));
    std::filesystem::create_directory(getGenericFullPathForTmp("/tmp/dummy/1"));
    std::filesystem::copy(getGenericFullPathForSrcTest("/ovms/src/test/dummy/1/dummy.bin"), getGenericFullPathForTmp("/tmp/dummy/1/dummy.bin"), std::filesystem::copy_options::recursive);
    TestHelper1 t(*this, configWith1DummyInTmp);
    auto handler = ovms::HttpRestApiHandler(ovmsServer, 10);

    const char* expectedJson1 = "{\n\t\"error\": \"Reloading config file failed. Check server logs for more info.\"\n}";
    std::string response;
    auto status = handler.processConfigReloadRequest(response, t.getManager());

    EXPECT_EQ(expectedJson1, response);
    EXPECT_EQ(status, ovms::StatusCode::FILE_INVALID);

    std::filesystem::copy(getGenericFullPathForSrcTest("/ovms/src/test/dummy/1/dummy.xml"), getGenericFullPathForTmp("/tmp/dummy/1/dummy.xml"), std::filesystem::copy_options::recursive);

    const char* expectedJson2 = R"({
"dummy" : 
{
 "model_version_status": [
  {
   "version": "1",
   "state": "AVAILABLE",
   "status": {
    "error_code": "OK",
    "error_message": "OK"
   }
  }
 ]
}
})";
    status = handler.processConfigReloadRequest(response, t.getManager());

    EXPECT_EQ(expectedJson2, response);
    EXPECT_EQ(status, ovms::StatusCode::OK_RELOADED);

    // Remove the model from disk when model is still loaded
    std::filesystem::remove_all(getGenericFullPathForTmp("/tmp/dummy"));
}

TEST_F(ConfigReload, startWithEmptyModelDir) {
    ovms::Server& ovmsServer = ovms::Server::instance();
    std::filesystem::remove_all(getGenericFullPathForTmp("/tmp/dummy"));
    std::filesystem::create_directory(getGenericFullPathForTmp("/tmp/dummy"));
    TestHelper1 t(*this, configWith1DummyInTmp);
    auto handler = ovms::HttpRestApiHandler(ovmsServer, 10);

    const char* expectedJson = R"({
"dummy" : 
{
 "model_version_status": []
}
})";
    std::string response;
    auto status = handler.processConfigReloadRequest(response, t.getManager());

    ASSERT_EQ(status, ovms::StatusCode::OK_NOT_RELOADED);
    EXPECT_EQ(expectedJson, response);

    // Remove the model from disk when model is still loaded
    std::filesystem::remove_all(getGenericFullPathForSrcTest("/tmp/dummy"));
}

// Config which restricts deleting model from disk when model is still loaded
static const char* configWith1DummyInTmpEnableMmap = R"(
{
    "model_config_list": [
        {
            "config": {
                "name": "dummy",
                "base_path": "/tmp/dummy",
                "plugin_config": {"ENABLE_MMAP": "YES"}
            }
        }
    ]
})";

TEST_F(ConfigReload, removeModelFromDiskWhenLoaded) {
    ovms::Server& ovmsServer = ovms::Server::instance();
    std::filesystem::remove_all(getGenericFullPathForTmp("/tmp/dummy"));
    std::filesystem::copy(getGenericFullPathForSrcTest("/ovms/src/test/dummy"), getGenericFullPathForTmp("/tmp/dummy"), std::filesystem::copy_options::recursive);
    TestHelper1 t(*this, configWith1DummyInTmpEnableMmap);
    auto handler = ovms::HttpRestApiHandler(ovmsServer, 10);

    std::string response;

    LoadConfig(t.getManager());

    const char* expectedJson1 = R"({
"dummy" : 
{
 "model_version_status": [
  {
   "version": "1",
   "state": "AVAILABLE",
   "status": {
    "error_code": "OK",
    "error_message": "OK"
   }
  }
 ]
}
})";
    auto status = handler.processConfigReloadRequest(response, t.getManager());

    EXPECT_EQ(expectedJson1, response);
    EXPECT_EQ(status, ovms::StatusCode::OK_NOT_RELOADED);

    // On windows, the test expects inability to remove model
#ifdef _WIN32
    EXPECT_THROW({
        std::filesystem::remove_all(getGenericFullPathForSrcTest("/tmp/dummy"));
    },
        std::filesystem::filesystem_error);
#else
    // On linux, removing the model should be possible even if ENABLE_MMAP=YES
    std::filesystem::remove_all(getGenericFullPathForSrcTest("/tmp/dummy"));
#endif
    this->UnloadConfig(t.getManager());
}

static const char* emptyConfig = R"(
{
    "model_config_list": []
})";

TEST_F(ConfigReload, StartWith1DummyThenReloadToRetireDummy) {
    ovms::Server& ovmsServer = ovms::Server::instance();
    TestHelper1 t(*this, configWith1Dummy);
    auto handler = ovms::HttpRestApiHandler(ovmsServer, 10);

    const char* expectedJson_1 = R"({
"dummy" : 
{
 "model_version_status": [
  {
   "version": "1",
   "state": "AVAILABLE",
   "status": {
    "error_code": "OK",
    "error_message": "OK"
   }
  }
 ]
}
})";

    std::string response;
    auto status = handler.processConfigReloadRequest(response, t.getManager());
    EXPECT_EQ(expectedJson_1, response);
    EXPECT_EQ(status, ovms::StatusCode::OK_NOT_RELOADED);

    RemoveConfig();
    SetUpConfig(emptyConfig);

    const char* expectedJson_2 = R"({
"dummy" : 
{
 "model_version_status": [
  {
   "version": "1",
   "state": "END",
   "status": {
    "error_code": "OK",
    "error_message": "OK"
   }
  }
 ]
}
})";

    status = handler.processConfigReloadRequest(response, t.getManager());
    EXPECT_EQ(expectedJson_2, response);
    EXPECT_EQ(status, ovms::StatusCode::OK_RELOADED);
}

TEST_F(ConfigReload, reloadNotNeeded) {
    ovms::Server& ovmsServer = ovms::Server::instance();
    TestHelper1 t(*this, configWith1Dummy);
    auto handler = ovms::HttpRestApiHandler(ovmsServer, 10);

    std::string response;
    auto status = handler.processConfigReloadRequest(response, t.getManager());
    EXPECT_EQ(status, ovms::StatusCode::OK_NOT_RELOADED);
}

TEST_F(ConfigReload, reloadNotNeededManyThreads) {
    ovms::Server& ovmsServer = ovms::Server::instance();
    TestHelper1 t(*this, configWith1Dummy);
    auto handler = ovms::HttpRestApiHandler(ovmsServer, 10);

    int numberOfThreads = 10;
    std::vector<std::thread> threads;
    auto& manager = t.getManager();
    std::function<void()> func = [&handler, &manager]() {
        std::string response;
        EXPECT_EQ(handler.processConfigReloadRequest(response, manager), ovms::StatusCode::OK_NOT_RELOADED);
    };

    for (int i = 0; i < numberOfThreads; i++) {
        threads.push_back(std::thread(func));
    }

    for (auto& thread : threads) {
        thread.join();
    }
}

static const char* configWith1DummyPipeline = R"(
{
    "model_config_list": [
        {
            "config": {
                "name": "dummy",
                "base_path": "/ovms/src/test/dummy"
            }
        }
    ],
    "pipeline_config_list": [
        {
            "name": "pipeline1Dummy",
            "inputs": ["custom_dummy_input"],
            "nodes": [
                {
                    "name": "dummyNode",
                    "model_name": "dummy",
                    "type": "DL model",
                    "inputs": [
                        {"b": {"node_name": "request",
                            "data_item": "custom_dummy_input"}}
                    ], 
                    "outputs": [
                        {"data_item": "a",
                        "alias": "new_dummy_output"}
                    ] 
                }
            ],
            "outputs": [
                {"custom_dummy_output": {"node_name": "dummyNode",
                                        "data_item": "new_dummy_output"}
                }
            ]
        }
    ]
})";

static const char* configWith1DummyPipelineNew = R"(
{
    "model_config_list": [
        {
            "config": {
                "name": "dummy",
                "base_path": "/ovms/src/test/dummy",
                "batch_size": "16"

            }
        }
    ],
    "pipeline_config_list": [
        {
            "name": "pipeline1Dummy",
            "inputs": ["custom_dummy_input"],
            "nodes": [
                {
                    "name": "dummyNode",
                    "model_name": "dummy",
                    "type": "DL model",
                    "inputs": [
                        {"b": {"node_name": "request",
                            "data_item": "custom_dummy_input"}}
                    ], 
                    "outputs": [
                        {"data_item": "a",
                        "alias": "new_dummy_output"}
                    ] 
                }
            ],
            "outputs": [
                {"custom_dummy_output": {"node_name": "dummyNode",
                                        "data_item": "new_dummy_output"}
                }
            ]
        }
    ]
})";

TEST_F(ConfigReload, StartWith1DummyThenReloadToAddPipeline) {
    ovms::Server& ovmsServer = ovms::Server::instance();
    TestHelper1 t(*this, configWith1Dummy);
    auto handler = ovms::HttpRestApiHandler(ovmsServer, 10);

    RemoveConfig();
    SetUpConfig(configWith1DummyPipeline);

    const char* expectedJson = R"({
"dummy" : 
{
 "model_version_status": [
  {
   "version": "1",
   "state": "AVAILABLE",
   "status": {
    "error_code": "OK",
    "error_message": "OK"
   }
  }
 ]
},
"pipeline1Dummy" : 
{
 "model_version_status": [
  {
   "version": "1",
   "state": "AVAILABLE",
   "status": {
    "error_code": "OK",
    "error_message": "OK"
   }
  }
 ]
}
})";
    std::string response;
    auto status = handler.processConfigReloadRequest(response, t.getManager());

    EXPECT_EQ(expectedJson, response);
    EXPECT_EQ(status, ovms::StatusCode::OK_RELOADED);
}
#if (MEDIAPIPE_DISABLE == 0)
TEST_F(ConfigReload, StartWith1DummyThenReloadToMediapipe) {
    ovms::Server& ovmsServer = ovms::Server::instance();
    TestHelper1 t(*this, configWith1Dummy);
    auto handler = ovms::HttpRestApiHandler(ovmsServer, 10);

    RemoveConfig();

    std::string contents;
    auto fs = std::make_shared<ovms::LocalFileSystem>();
    fs->readTextFile(getGenericFullPathForSrcTest("/ovms/src/test/mediapipe/config_mediapipe_add_adapter_full.json"), &contents);

    SetUpConfig(contents);

    const char* expectedJson = R"({
"add" : 
{
 "model_version_status": [
  {
   "version": "1",
   "state": "AVAILABLE",
   "status": {
    "error_code": "OK",
    "error_message": "OK"
   }
  }
 ]
},
"dummy" : 
{
 "model_version_status": [
  {
   "version": "1",
   "state": "END",
   "status": {
    "error_code": "OK",
    "error_message": "OK"
   }
  }
 ]
},
"mediapipeAdd" : 
{
 "model_version_status": [
  {
   "version": "1",
   "state": "AVAILABLE",
   "status": {
    "error_code": "OK",
    "error_message": "OK"
   }
  }
 ]
},
"mediapipeAddADAPTFULL" : 
{
 "model_version_status": [
  {
   "version": "1",
   "state": "AVAILABLE",
   "status": {
    "error_code": "OK",
    "error_message": "OK"
   }
  }
 ]
}
})";
    std::string response;
    auto status = handler.processConfigReloadRequest(response, t.getManager());

    EXPECT_EQ(expectedJson, response);
    EXPECT_EQ(status, ovms::StatusCode::OK_RELOADED);
}
#endif
static const char* configWithPipelineWithInvalidOutputs = R"(
{
    "model_config_list": [
        {
            "config": {
                "name": "dummy",
                "base_path": "/ovms/src/test/dummy"
            }
        }
    ],
    "pipeline_config_list": [
        {
            "name": "pipeline1Dummy",
            "inputs": ["custom_dummy_input"],
            "nodes": [
                {
                    "name": "dummyNode",
                    "model_name": "dummy",
                    "type": "DL model",
                    "inputs": [
                        {"b": {"node_name": "request",
                            "data_item": "custom_dummy_input"}}
                    ], 
                    "outputs": [
                        {"data_item": "a",
                        "alias": "new_dummy_output"}
                    ] 
                }
            ],
            "outputs": [
                {"custom_dummy_output": {"node_name": "dummyNode",
                                        "data_item": "non_existing_output"}
                }
            ]
        }
    ]
})";

TEST_F(ConfigReload, StartWith1DummyThenReloadToAddPipelineWithInvalidOutputs) {
    ovms::Server& ovmsServer = ovms::Server::instance();
    TestHelper1 t(*this, configWith1Dummy);
    auto handler = ovms::HttpRestApiHandler(ovmsServer, 10);

    RemoveConfig();
    SetUpConfig(configWithPipelineWithInvalidOutputs);

    std::string response;
    const char* expectedJson = "{\n\t\"error\": \"Reloading config file failed. Check server logs for more info.\"\n}";
    auto status = handler.processConfigReloadRequest(response, t.getManager());

    EXPECT_EQ(expectedJson, response);
    EXPECT_EQ(status, ovms::StatusCode::PIPELINE_NODE_REFERING_TO_MISSING_DATA_SOURCE);
}

TEST_F(ConfigReload, reloadWithInvalidPipelineConfigManyThreads) {
    ovms::Server& ovmsServer = ovms::Server::instance();
    TestHelper1 t(*this, configWith1Dummy);
    auto handler = ovms::HttpRestApiHandler(ovmsServer, 10);

    RemoveConfig();
    SetUpConfig(configWithPipelineWithInvalidOutputs);
    int numberOfThreads = 2;
    std::vector<std::thread> threads;
    auto& manager = t.getManager();
    std::function<void()> func = [&handler, &manager]() {
        std::string response;
        EXPECT_EQ(handler.processConfigReloadRequest(response, manager), ovms::StatusCode::PIPELINE_NODE_REFERING_TO_MISSING_DATA_SOURCE);
    };

    for (int i = 0; i < numberOfThreads; i++) {
        threads.push_back(std::thread(func));
    }

    for (auto& thread : threads) {
        thread.join();
    }
}

TEST_F(ConfigReload, reloadWithInvalidModelConfigManyThreads) {
    ovms::Server& ovmsServer = ovms::Server::instance();
    TestHelper1 t(*this, configWith1Dummy);
    auto handler = ovms::HttpRestApiHandler(ovmsServer, 10);

    RemoveConfig();
    SetUpConfig(configWithDuplicatedModelName);
    int numberOfThreads = 2;
    std::vector<std::thread> threads;
    auto& manager = t.getManager();
    std::function<void()> func = [&handler, &manager]() {
        std::string response;
        EXPECT_EQ(handler.processConfigReloadRequest(response, manager), ovms::StatusCode::MODEL_NAME_OCCUPIED);
    };

    for (int i = 0; i < numberOfThreads; i++) {
        threads.push_back(std::thread(func));
    }

    for (auto& thread : threads) {
        thread.join();
    }
}
static const char* configWithPipelineContainsNonExistingModel = R"(
{
    "model_config_list": [
        {
            "config": {
                "name": "dummy",
                "base_path": "/ovms/src/test/dummy"
            }
        }
    ],
    "pipeline_config_list": [
        {
            "name": "pipeline1Dummy",
            "inputs": ["custom_dummy_input"],
            "nodes": [
                {
                    "name": "dummyNode",
                    "model_name": "non-existing",
                    "type": "DL model",
                    "inputs": [
                        {"b": {"node_name": "request",
                            "data_item": "custom_dummy_input"}}
                    ], 
                    "outputs": [
                        {"data_item": "a",
                        "alias": "new_dummy_output"}
                    ] 
                }
            ],
            "outputs": [
                {"custom_dummy_output": {"node_name": "dummyNode",
                                        "data_item": "new_dummy_output"}
                }
            ]
        }
    ]
})";

TEST_F(ConfigReload, StartWith1DummyThenReloadToAddPipelineWithNonExistingModel) {
    ovms::Server& ovmsServer = ovms::Server::instance();
    TestHelper1 t(*this, configWith1Dummy);
    auto handler = ovms::HttpRestApiHandler(ovmsServer, 10);

    RemoveConfig();
    SetUpConfig(configWithPipelineContainsNonExistingModel);

    const char* expectedJson = "{\n\t\"error\": \"Reloading config file failed. Check server logs for more info.\"\n}";
    std::string response;
    auto status = handler.processConfigReloadRequest(response, t.getManager());

    EXPECT_EQ(expectedJson, response);
    EXPECT_EQ(status, ovms::StatusCode::PIPELINE_NODE_REFERING_TO_MISSING_MODEL);
}

static const char* configWith2DummyPipelines = R"(
{
    "model_config_list": [
        {
            "config": {
                "name": "dummy",
                "base_path": "/ovms/src/test/dummy"
            }
        }
    ],
    "pipeline_config_list": [
        {
            "name": "pipeline1Dummy",
            "inputs": ["custom_dummy_input"],
            "nodes": [
                {
                    "name": "dummyNode",
                    "model_name": "dummy",
                    "type": "DL model",
                    "inputs": [
                        {"b": {"node_name": "request",
                            "data_item": "custom_dummy_input"}}
                    ], 
                    "outputs": [
                        {"data_item": "a",
                        "alias": "new_dummy_output"}
                    ] 
                }
            ],
            "outputs": [
                {"custom_dummy_output": {"node_name": "dummyNode",
                                        "data_item": "new_dummy_output"}
                }
            ]
        },
        {
            "name": "pipeline2Dummy",
            "inputs": ["custom_dummy_input"],
            "nodes": [
                {
                    "name": "dummyNode",
                    "model_name": "dummy",
                    "type": "DL model",
                    "inputs": [
                        {"b": {"node_name": "request",
                            "data_item": "custom_dummy_input"}}
                    ], 
                    "outputs": [
                        {"data_item": "a",
                        "alias": "new_dummy_output"}
                    ] 
                }
            ],
            "outputs": [
                {"custom_dummy_output": {"node_name": "dummyNode",
                                        "data_item": "new_dummy_output"}
                }
            ]
        }
    ]
})";

TEST_F(ConfigReload, StartWith1DummyPipelineThenReloadToAddPipeline) {
    ovms::Server& ovmsServer = ovms::Server::instance();
    TestHelper1 t(*this, configWith1DummyPipeline);
    auto handler = ovms::HttpRestApiHandler(ovmsServer, 10);

    std::string response;
    RemoveConfig();
    SetUpConfig(configWith1DummyPipelineNew);

    const char* expectedJson_1 = R"({
"dummy" : 
{
 "model_version_status": [
  {
   "version": "1",
   "state": "AVAILABLE",
   "status": {
    "error_code": "OK",
    "error_message": "OK"
   }
  }
 ]
},
"pipeline1Dummy" : 
{
 "model_version_status": [
  {
   "version": "1",
   "state": "AVAILABLE",
   "status": {
    "error_code": "OK",
    "error_message": "OK"
   }
  }
 ]
}
})";

    auto status = handler.processConfigReloadRequest(response, t.getManager());
    EXPECT_EQ(expectedJson_1, response);
    EXPECT_EQ(status, ovms::StatusCode::OK_RELOADED);

    RemoveConfig();
    SetUpConfig(configWith2DummyPipelines);

    const char* expectedJson_2 = R"({
"dummy" : 
{
 "model_version_status": [
  {
   "version": "1",
   "state": "AVAILABLE",
   "status": {
    "error_code": "OK",
    "error_message": "OK"
   }
  }
 ]
},
"pipeline1Dummy" : 
{
 "model_version_status": [
  {
   "version": "1",
   "state": "AVAILABLE",
   "status": {
    "error_code": "OK",
    "error_message": "OK"
   }
  }
 ]
},
"pipeline2Dummy" : 
{
 "model_version_status": [
  {
   "version": "1",
   "state": "AVAILABLE",
   "status": {
    "error_code": "OK",
    "error_message": "OK"
   }
  }
 ]
}
})";

    status = handler.processConfigReloadRequest(response, t.getManager());
    EXPECT_EQ(expectedJson_2, response);
    EXPECT_EQ(status, ovms::StatusCode::OK_RELOADED);
}

class ConfigStatus : public ConfigApi {};

TEST_F(ConfigStatus, configWithPipelines) {
    ovms::Server& ovmsServer = ovms::Server::instance();
    TestHelper1 t(*this, configWith2DummyPipelines);
    auto handler = ovms::HttpRestApiHandler(ovmsServer, 10);
    std::string response;

    const char* expectedJson = R"({
"dummy" : 
{
 "model_version_status": [
  {
   "version": "1",
   "state": "AVAILABLE",
   "status": {
    "error_code": "OK",
    "error_message": "OK"
   }
  }
 ]
},
"pipeline1Dummy" : 
{
 "model_version_status": [
  {
   "version": "1",
   "state": "AVAILABLE",
   "status": {
    "error_code": "OK",
    "error_message": "OK"
   }
  }
 ]
},
"pipeline2Dummy" : 
{
 "model_version_status": [
  {
   "version": "1",
   "state": "AVAILABLE",
   "status": {
    "error_code": "OK",
    "error_message": "OK"
   }
  }
 ]
}
})";
    auto status = handler.processConfigStatusRequest(response, t.getManager());
    EXPECT_EQ(expectedJson, response);
    EXPECT_EQ(status, ovms::StatusCode::OK);
}
#if (MEDIAPIPE_DISABLE == 0)
TEST_F(ConfigStatus, configWithMediapipe) {
    ovms::Server& ovmsServer = ovms::Server::instance();

    std::string contents;
    auto fs = std::make_shared<ovms::LocalFileSystem>();
    fs->readTextFile(getGenericFullPathForSrcTest("/ovms/src/test/mediapipe/config_mediapipe_add_adapter_full.json"), &contents);

    TestHelper1 t(*this, contents.c_str());
    auto handler = ovms::HttpRestApiHandler(ovmsServer, 10);
    std::string response;

    const char* expectedJson = R"({
"add" : 
{
 "model_version_status": [
  {
   "version": "1",
   "state": "AVAILABLE",
   "status": {
    "error_code": "OK",
    "error_message": "OK"
   }
  }
 ]
},
"mediapipeAdd" : 
{
 "model_version_status": [
  {
   "version": "1",
   "state": "AVAILABLE",
   "status": {
    "error_code": "OK",
    "error_message": "OK"
   }
  }
 ]
},
"mediapipeAddADAPTFULL" : 
{
 "model_version_status": [
  {
   "version": "1",
   "state": "AVAILABLE",
   "status": {
    "error_code": "OK",
    "error_message": "OK"
   }
  }
 ]
}
})";
    auto status = handler.processConfigStatusRequest(response, t.getManager());
    EXPECT_EQ(expectedJson, response);
    EXPECT_EQ(status, ovms::StatusCode::OK);
}

TEST_F(ConfigStatus, configWithMediapipeRemoved) {
    ovms::Server& ovmsServer = ovms::Server::instance();

    std::string contents;
    auto fs = std::make_shared<ovms::LocalFileSystem>();
    fs->readTextFile(getGenericFullPathForSrcTest("/ovms/src/test/mediapipe/config_mediapipe_add_adapter_full.json"), &contents);

    TestHelper1 t(*this, contents.c_str());
    auto handler = ovms::HttpRestApiHandler(ovmsServer, 10);
    std::string response;

    const char* expectedJson = R"({
"add" : 
{
 "model_version_status": [
  {
   "version": "1",
   "state": "AVAILABLE",
   "status": {
    "error_code": "OK",
    "error_message": "OK"
   }
  }
 ]
},
"mediapipeAdd" : 
{
 "model_version_status": [
  {
   "version": "1",
   "state": "AVAILABLE",
   "status": {
    "error_code": "OK",
    "error_message": "OK"
   }
  }
 ]
},
"mediapipeAddADAPTFULL" : 
{
 "model_version_status": [
  {
   "version": "1",
   "state": "AVAILABLE",
   "status": {
    "error_code": "OK",
    "error_message": "OK"
   }
  }
 ]
}
})";
    auto status = handler.processConfigStatusRequest(response, t.getManager());
    EXPECT_EQ(expectedJson, response);
    EXPECT_EQ(status, ovms::StatusCode::OK);

    RemoveConfig();

    SetUpConfig(configWith1Dummy);

    const char* expectedJsonRemoved = R"({
"add" : 
{
 "model_version_status": [
  {
   "version": "1",
   "state": "END",
   "status": {
    "error_code": "OK",
    "error_message": "OK"
   }
  }
 ]
},
"dummy" : 
{
 "model_version_status": [
  {
   "version": "1",
   "state": "AVAILABLE",
   "status": {
    "error_code": "OK",
    "error_message": "OK"
   }
  }
 ]
},
"mediapipeAdd" : 
{
 "model_version_status": [
  {
   "version": "1",
   "state": "END",
   "status": {
    "error_code": "OK",
    "error_message": "OK"
   }
  }
 ]
},
"mediapipeAddADAPTFULL" : 
{
 "model_version_status": [
  {
   "version": "1",
   "state": "END",
   "status": {
    "error_code": "OK",
    "error_message": "OK"
   }
  }
 ]
}
})";

    status = handler.processConfigReloadRequest(response, t.getManager());
    EXPECT_EQ(expectedJsonRemoved, response);
    EXPECT_EQ(status, ovms::StatusCode::OK_RELOADED);
}
#endif

TEST_F(ConfigStatus, url_decode) {
    EXPECT_EQ("a b c d", ovms::urlDecode("a%20b%20c%20d"));
    EXPECT_EQ("model/name", ovms::urlDecode("model%2Fname"));
    EXPECT_EQ("model%", ovms::urlDecode("model%"));
    EXPECT_EQ("model%2", ovms::urlDecode("model%2"));
}
