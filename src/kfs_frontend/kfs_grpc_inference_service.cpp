//*****************************************************************************
// Copyright 2022 Intel Corporation
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
#include "kfs_grpc_inference_service.hpp"

#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

#include "deserialization.hpp"
#include "kfs_utils.hpp"
#include "kfs_request_utils.hpp"
#include "../dags/pipeline.hpp"
#include "../dags/pipelinedefinition.hpp"
#include "../dags/pipelinedefinitionstatus.hpp"
#include "../dags/pipelinedefinitionunloadguard.hpp"
#include "../execution_context.hpp"
#include "../grpc_utils.hpp"
#if (MEDIAPIPE_DISABLE == 0)
// clang-format off
// kfs_graph_executor_impl needs to be included before mediapipegraphexecutor
// because it contains functions required by graph execution template
#include "kfs_graph_executor_impl.hpp"
#include "../mediapipe_internal/mediapipegraphdefinition.hpp"
#include "../mediapipe_internal/mediapipegraphexecutor.hpp"
// clang-format on
#endif
#include "../metric.hpp"
#include "../modelinstance.hpp"
#include "../deserialization_main.hpp"
#include "../inference_executor.hpp"
#include "../modelinstanceunloadguard.hpp"
#include "../modelmanager.hpp"
#include "../ovinferrequestsqueue.hpp"
#include "../servablemanagermodule.hpp"
#include "../server.hpp"
#include "../status.hpp"
#include "../stringutils.hpp"
#include "../tensorinfo.hpp"
#include "../timer.hpp"
#include "../version.hpp"

namespace {
enum : unsigned int {
    TOTAL,
    TIMER_END
};
}

namespace ovms {

Status KFSInferenceServiceImpl::getModelInstance(const KFSRequest* request,
    std::shared_ptr<ovms::ModelInstance>& modelInstance,
    std::unique_ptr<ModelInstanceUnloadGuard>& modelInstanceUnloadGuardPtr) {
    OVMS_PROFILE_FUNCTION();
    model_version_t requestedVersion = 0;
    if (!request->model_version().empty()) {
        auto versionRead = stoi64(request->model_version());
        if (versionRead) {
            requestedVersion = versionRead.value();
        } else {
            SPDLOG_DEBUG("requested model: name {}; with version in invalid format: {}", request->model_name(), request->model_version());
            return StatusCode::MODEL_VERSION_INVALID_FORMAT;
        }
    }
    return this->modelManager.getModelInstance(request->model_name(), requestedVersion, modelInstance, modelInstanceUnloadGuardPtr);
}

Status KFSInferenceServiceImpl::getPipeline(const KFSRequest* request,
    KFSResponse* response,
    std::unique_ptr<ovms::Pipeline>& pipelinePtr) {
    OVMS_PROFILE_FUNCTION();
    return this->modelManager.createPipeline(pipelinePtr, request->model_name(), request, response);
}

const std::string PLATFORM = "OpenVINO";

::grpc::Status KFSInferenceServiceImpl::ServerLive(::grpc::ServerContext* context, const ::inference::ServerLiveRequest* request, ::inference::ServerLiveResponse* response) {
    (void)context;
    (void)request;
    (void)response;
    bool isLive = this->ovmsServer.isLive(GRPC_SERVER_MODULE_NAME);
    SPDLOG_DEBUG("Requested Server liveness state: {}", isLive);
    response->set_live(isLive);
    return grpc::Status::OK;
}

::grpc::Status KFSInferenceServiceImpl::ServerReady(::grpc::ServerContext* context, const ::inference::ServerReadyRequest* request, ::inference::ServerReadyResponse* response) {
    (void)context;
    (void)request;
    (void)response;
    bool isReady = this->ovmsServer.isReady();
    SPDLOG_DEBUG("Requested Server readiness state: {}", isReady);
    response->set_ready(isReady);
    return grpc::Status::OK;
}

Status KFSInferenceServiceImpl::getModelReady(const KFSGetModelStatusRequest* request, KFSGetModelStatusResponse* response, const ModelManager& manager, ExecutionContext executionContext) {
    // Return in response true/false
    // if no version requested give response for default version
    const auto& name = request->name();
    const auto& versionString = request->version();
    auto model = manager.findModelByName(name);
    SPDLOG_DEBUG("ModelReady requested name: {}, version: {}", name, versionString);
    if (model == nullptr) {
        SPDLOG_DEBUG("ModelReady requested model {} is missing, trying to find pipeline with such name", name);
        auto pipelineDefinition = manager.getPipelineFactory().findDefinitionByName(name);
        if (!pipelineDefinition) {
#if (MEDIAPIPE_DISABLE == 0)
            SPDLOG_DEBUG("ModelReady requested pipeline {} is missing, trying to find mediapipe with such name", name);
            auto mediapipeGraphDefinition = manager.getMediapipeFactory().findDefinitionByName(name);
            if (!mediapipeGraphDefinition) {
                return StatusCode::MODEL_NAME_MISSING;
            }
            auto status = buildResponse(*mediapipeGraphDefinition, response);
            INCREMENT_IF_ENABLED(mediapipeGraphDefinition->getMetricReporter().getModelReadyMetric(executionContext, status.ok()));
            return status;
#else
            return StatusCode::MODEL_NAME_MISSING;
#endif
        }
        auto status = buildResponse(*pipelineDefinition, response);
        INCREMENT_IF_ENABLED(pipelineDefinition->getMetricReporter().getModelReadyMetric(executionContext, status.ok()));
        return status;
    }
    std::shared_ptr<ModelInstance> instance = nullptr;
    if (!versionString.empty()) {
        SPDLOG_DEBUG("ModelReady requested model: name {}; version {}", name, versionString);
        model_version_t requestedVersion = 0;
        auto versionRead = stoi64(versionString);
        if (versionRead) {
            requestedVersion = versionRead.value();
        } else {
            SPDLOG_DEBUG("ModelReady requested model: name {}; with version in invalid format: {}", name, versionString);
            return Status(StatusCode::MODEL_VERSION_INVALID_FORMAT);
        }
        instance = model->getModelInstanceByVersion(requestedVersion);
        if (instance == nullptr) {
            SPDLOG_DEBUG("ModelReady requested model {}; version {} is missing", name, versionString);
            return Status(StatusCode::MODEL_VERSION_MISSING);
        }
    } else {
        SPDLOG_DEBUG("ModelReady requested model: name {}; default version", name);
        instance = model->getDefaultModelInstance();
        if (instance == nullptr) {
            SPDLOG_DEBUG("ModelReady requested model {}; version {} is missing", name, versionString);
            return Status(StatusCode::MODEL_VERSION_MISSING);
        }
    }
    auto status = buildResponse(instance, response);
    INCREMENT_IF_ENABLED(instance->getMetricReporter().getModelReadyMetric(executionContext, status.ok()));
    return status;
}

::grpc::Status KFSInferenceServiceImpl::ModelReady(::grpc::ServerContext* context, const KFSGetModelStatusRequest* request, KFSGetModelStatusResponse* response) {
    return grpc(ModelReadyImpl(context, request, response, ExecutionContext{ExecutionContext::Interface::GRPC, ExecutionContext::Method::ModelReady}));
}

Status KFSInferenceServiceImpl::ModelReadyImpl(::grpc::ServerContext* context, const KFSGetModelStatusRequest* request, KFSGetModelStatusResponse* response, ExecutionContext executionContext) {
    (void)context;
    return this->getModelReady(request, response, this->modelManager, executionContext);
}

::grpc::Status KFSInferenceServiceImpl::ServerMetadata(::grpc::ServerContext* context, const KFSServerMetadataRequest* request, KFSServerMetadataResponse* response) {
    return grpc(ServerMetadataImpl(context, request, response));
}

Status KFSInferenceServiceImpl::ServerMetadataImpl(::grpc::ServerContext* context, const KFSServerMetadataRequest* request, KFSServerMetadataResponse* response) {
    (void)context;
    (void)request;
    (void)response;
    response->set_name(PROJECT_NAME);
    response->set_version(PROJECT_VERSION);
    return StatusCode::OK;
}

::grpc::Status KFSInferenceServiceImpl::ModelMetadata(::grpc::ServerContext* context, const KFSModelMetadataRequest* request, KFSModelMetadataResponse* response) {
    KFSModelExtraMetadata extraMetadata;
    return grpc(ModelMetadataImpl(context, request, response, ExecutionContext{ExecutionContext::Interface::GRPC, ExecutionContext::Method::ModelMetadata}, extraMetadata));
}

Status KFSInferenceServiceImpl::ModelMetadataImpl(::grpc::ServerContext* context, const KFSModelMetadataRequest* request, KFSModelMetadataResponse* response, ExecutionContext executionContext, KFSModelExtraMetadata& extraMetadata) {
    const auto& name = request->name();
    const auto& versionString = request->version();

    auto model = this->modelManager.findModelByName(name);
    SPDLOG_DEBUG("ModelMetadata requested name: {}, version: {}", name, versionString);
    if (model == nullptr) {
        SPDLOG_DEBUG("GetModelMetadata: Model {} is missing, trying to find pipeline with such name", name);
        auto pipelineDefinition = this->modelManager.getPipelineFactory().findDefinitionByName(name);
        if (!pipelineDefinition) {
#if (MEDIAPIPE_DISABLE == 0)
            SPDLOG_DEBUG("GetModelMetadata: Pipeline {} is missing, trying to find mediapipe with such name", name);
            auto mediapipeGraphDefinition = this->modelManager.getMediapipeFactory().findDefinitionByName(name);
            if (!mediapipeGraphDefinition) {
                return StatusCode::MODEL_NAME_MISSING;
            }
            auto status = buildResponse(*mediapipeGraphDefinition, response);
            INCREMENT_IF_ENABLED(mediapipeGraphDefinition->getMetricReporter().getModelMetadataMetric(executionContext, status.ok()));
            return status;
#else
            return Status(StatusCode::MODEL_NAME_MISSING);
#endif
        }
        auto status = buildResponse(*pipelineDefinition, response);
        INCREMENT_IF_ENABLED(pipelineDefinition->getMetricReporter().getModelMetadataMetric(executionContext, status.ok()));
        return status;
    }
    std::shared_ptr<ModelInstance> instance = nullptr;
    if (!versionString.empty()) {
        SPDLOG_DEBUG("GetModelMetadata requested model: name {}; version {}", name, versionString);
        model_version_t requestedVersion = 0;
        auto versionRead = stoi64(versionString);
        if (versionRead) {
            requestedVersion = versionRead.value();
        } else {
            SPDLOG_DEBUG("GetModelMetadata requested model: name {}; with version in invalid format: {}", name, versionString);
            return Status(StatusCode::MODEL_VERSION_INVALID_FORMAT);
        }
        instance = model->getModelInstanceByVersion(requestedVersion);
        if (instance == nullptr) {
            SPDLOG_DEBUG("GetModelMetadata requested model {}; version {} is missing", name, versionString);
            return Status(StatusCode::MODEL_VERSION_MISSING);
        }
    } else {
        SPDLOG_DEBUG("GetModelMetadata requested model: name {}; default version", name);
        instance = model->getDefaultModelInstance();
        if (instance == nullptr) {
            SPDLOG_DEBUG("GetModelMetadata requested model {}; version {} is missing", name, versionString);
            return Status(StatusCode::MODEL_VERSION_MISSING);
        }
    }
    auto status = buildResponse(*model, *instance, response, extraMetadata);

    INCREMENT_IF_ENABLED(instance->getMetricReporter().getModelMetadataMetric(executionContext, status.ok()));

    return status;
}

::grpc::Status KFSInferenceServiceImpl::ModelInfer(::grpc::ServerContext* context, const KFSRequest* request, KFSResponse* response) {
    OVMS_PROFILE_FUNCTION();
    Timer<TIMER_END> timer;
    timer.start(TOTAL);
    SPDLOG_DEBUG("Processing gRPC request for model: {}; version: {}",
        request->model_name(),
        request->model_version());
    ServableMetricReporter* reporter = nullptr;
    Status status;
    const std::string servableName = request->model_name();
    try {
        status = this->ModelInferImpl(context, request, response, ExecutionContext{ExecutionContext::Interface::GRPC, ExecutionContext::Method::ModelInfer}, reporter);
        timer.stop(TOTAL);
        if (!status.ok()) {
            return grpc(status);
        }
    } catch (const std::exception& e) {
        SPDLOG_ERROR("Caught exception in InferenceServiceImpl for servable: {} exception: {}", servableName, e.what());
        return grpc(Status(StatusCode::UNKNOWN_ERROR, e.what()));
    } catch (...) {
        SPDLOG_ERROR("Caught unknown exception in InferenceServiceImpl for servable: {}", servableName);
        return grpc(Status(StatusCode::UNKNOWN_ERROR));
    }
    double requestTotal = timer.elapsed<std::chrono::microseconds>(TOTAL);
    SPDLOG_DEBUG("Total gRPC request processing time: {} ms", requestTotal / 1000);
    if (!reporter) {
        return grpc(Status(StatusCode::OK));
        // There is no request time metric for MediaPipe endpoints
    }
    OBSERVE_IF_ENABLED(reporter->requestTimeGrpc, requestTotal);
    return grpc(status);
}

::grpc::Status KFSInferenceServiceImpl::ModelStreamInfer(::grpc::ServerContext* context, ::grpc::ServerReaderWriter<::inference::ModelStreamInferResponse, KFSRequest>* stream) {
    return grpc(ModelStreamInferImpl(context, stream));
}

Status KFSInferenceServiceImpl::ModelInferImpl(::grpc::ServerContext* context, const KFSRequest* request, KFSResponse* response, ExecutionContext executionContext, ServableMetricReporter*& reporterOut) {
    OVMS_PROFILE_FUNCTION();
    std::shared_ptr<ovms::ModelInstance> modelInstance;
    std::unique_ptr<ovms::Pipeline> pipelinePtr;

    std::unique_ptr<ModelInstanceUnloadGuard> modelInstanceUnloadGuard;
    SPDLOG_DEBUG("ModelInfer requested name: {}, version: {}", request->model_name(), request->model_version());
    auto status = getModelInstance(request, modelInstance, modelInstanceUnloadGuard);
    if (status == StatusCode::MODEL_NAME_MISSING) {
        SPDLOG_DEBUG("Requested model: {} does not exist. Searching for pipeline with that name...", request->model_name());
        status = getPipeline(request, response, pipelinePtr);
        if (status == StatusCode::PIPELINE_DEFINITION_NAME_MISSING) {
            SPDLOG_DEBUG("Requested DAG: {} does not exist. Searching for mediapipe graph with that name...", request->model_name());
#if (MEDIAPIPE_DISABLE == 0)
            std::shared_ptr<MediapipeGraphExecutor> executor;
            status = this->modelManager.createPipeline(executor, request->model_name());
            if (!status.ok()) {
                return status;
            }
            status = executor->infer(request, response, executionContext);
            return status;
#else
            SPDLOG_DEBUG("Requested DAG: {} does not exist. Mediapipe support was disabled during build process...", request->model_name());
#endif
        }
    }
    if (!status.ok()) {
        if (modelInstance) {
            INCREMENT_IF_ENABLED(modelInstance->getMetricReporter().getInferRequestMetric(executionContext, status.ok()));
        }
        SPDLOG_DEBUG("Getting modelInstance or pipeline failed. {}", status.string());
        return status;
    }
    if (pipelinePtr) {
        reporterOut = &pipelinePtr->getMetricReporter();
        status = pipelinePtr->execute(executionContext);
    } else if (modelInstance) {
        reporterOut = &modelInstance->getMetricReporter();
        status = infer(*modelInstance, request, response, modelInstanceUnloadGuard);
    }
    INCREMENT_IF_ENABLED(reporterOut->getInferRequestMetric(executionContext, status.ok()));
    if (!status.ok()) {
        return status;
    }
    response->set_id(request->id());
    return StatusCode::OK;
}

Status KFSInferenceServiceImpl::ModelStreamInferImpl(::grpc::ServerContext* context, ::grpc::ServerReaderWriterInterface<::inference::ModelStreamInferResponse, KFSRequest>* serverReaderWriter) {
    OVMS_PROFILE_FUNCTION();
#if (MEDIAPIPE_DISABLE == 0)
    KFSRequest firstRequest;
    if (!serverReaderWriter->Read(&firstRequest)) {
        Status status = StatusCode::MEDIAPIPE_UNINITIALIZED_STREAM_CLOSURE;
        SPDLOG_DEBUG(status.string());
        return status;
    }
    std::shared_ptr<MediapipeGraphExecutor> executor;
    auto status = this->modelManager.createPipeline(executor, firstRequest.model_name());
    if (!status.ok()) {
        return status;
    }
    ExecutionContext executionContext{ExecutionContext::Interface::GRPC, ExecutionContext::Method::ModelInferStream};
    return executor->inferStream(firstRequest, *serverReaderWriter, executionContext);
#else
    SPDLOG_DEBUG("Mediapipe support was disabled during build process...");
    return StatusCode::NOT_IMPLEMENTED;
#endif
}

Status KFSInferenceServiceImpl::buildResponse(
    std::shared_ptr<ModelInstance> instance,
    KFSGetModelStatusResponse* response) {
    bool isReady = instance->getStatus().getState() == ModelVersionState::AVAILABLE;
    SPDLOG_DEBUG("Creating ModelReady response for model: {}; version: {}; ready: {}", instance->getName(), instance->getVersion(), isReady);
    response->set_ready(isReady);
    return StatusCode::OK;
}

Status KFSInferenceServiceImpl::buildResponse(
    PipelineDefinition& pipelineDefinition,
    KFSGetModelStatusResponse* response) {
    bool isReady = pipelineDefinition.getStatus().isAvailable();
    SPDLOG_DEBUG("Creating ModelReady response for pipeline: {}; ready: {}", pipelineDefinition.getName(), isReady);
    response->set_ready(isReady);
    return StatusCode::OK;
}

#if (MEDIAPIPE_DISABLE == 0)
Status KFSInferenceServiceImpl::buildResponse(
    MediapipeGraphDefinition& definition,
    KFSGetModelStatusResponse* response) {
    bool isReady = definition.getStatus().isAvailable();
    SPDLOG_DEBUG("Creating ModelReady response for mediapipe: {}; ready: {}", definition.getName(), isReady);
    response->set_ready(isReady);
    return StatusCode::OK;
}
#endif

static void addReadyVersions(Model& model,
    model_version_t versionAvailableDuringInitialCheck,
    KFSModelMetadataResponse* response) {
    auto modelVersions = model.getModelVersionsMapCopy();
    for (auto& [modelVersion, modelInstance] : modelVersions) {
        // even if we have modelUnloadGuard model can have already state set to LOADING/UNLOADING
        // here we make choice to report it as AVAILABLE even if it is already in different state
        // since we managed to obtain guard. Model could change state after sending response anyway.
        // Otherwise we could respond with metadata of one version but in response send information
        // that different version is ready
        if ((modelVersion == versionAvailableDuringInitialCheck) || (modelInstance.getStatus().getState() == ModelVersionState::AVAILABLE))
            response->add_versions(std::to_string(modelVersion));
    }
}

Status KFSInferenceServiceImpl::buildResponse(
    Model& model,
    ModelInstance& instance,
    KFSModelMetadataResponse* response,
    KFSModelExtraMetadata& extraMetadata) {

    std::unique_ptr<ModelInstanceUnloadGuard> unloadGuard;

    // 0 meaning immediately return unload guard if possible, otherwise do not wait for available state
    auto status = instance.waitForLoaded(0, unloadGuard);
    if (!status.ok()) {
        return status;
    }
    extraMetadata.rt_info = instance.getRTInfo();
    response->Clear();
    response->set_name(instance.getName());
    addReadyVersions(model, instance.getVersion(), response);
    response->set_platform(PLATFORM);

    for (const auto& input : instance.getInputsInfo()) {
        convert(input, response->add_inputs());
    }

    for (const auto& output : instance.getOutputsInfo()) {
        convert(output, response->add_outputs());
    }
    return StatusCode::OK;
}

KFSInferenceServiceImpl::KFSInferenceServiceImpl(const Server& server) :
    ovmsServer(server),
    modelManager(dynamic_cast<const ServableManagerModule*>(this->ovmsServer.getModule(SERVABLE_MANAGER_MODULE_NAME))->getServableManager()) {
    if (nullptr == this->ovmsServer.getModule(SERVABLE_MANAGER_MODULE_NAME)) {
        const char* message = "Tried to create kserve inference service impl without servable manager module";
        SPDLOG_ERROR(message);
        throw std::logic_error(message);
    }
}

Status KFSInferenceServiceImpl::buildResponse(
    PipelineDefinition& pipelineDefinition,
    KFSModelMetadataResponse* response) {

    std::unique_ptr<PipelineDefinitionUnloadGuard> unloadGuard;

    // 0 meaning immediately return unload guard if possible, otherwise do not wait for available state
    auto status = pipelineDefinition.waitForLoaded(unloadGuard, 0);
    if (!status.ok()) {
        return status;
    }

    response->Clear();
    response->set_name(pipelineDefinition.getName());
    response->add_versions("1");
    response->set_platform(PLATFORM);

    for (const auto& input : pipelineDefinition.getInputsInfo()) {
        convert(input, response->add_inputs());
    }

    for (const auto& output : pipelineDefinition.getOutputsInfo()) {
        convert(output, response->add_outputs());
    }

    return StatusCode::OK;
}

#if (MEDIAPIPE_DISABLE == 0)
Status KFSInferenceServiceImpl::buildResponse(
    MediapipeGraphDefinition& mediapipeGraphDefinition,
    KFSModelMetadataResponse* response) {
    std::unique_ptr<MediapipeGraphDefinitionUnloadGuard> unloadGuard;
    // 0 meaning immediately return unload guard if possible, otherwise do not wait for available state
    auto status = mediapipeGraphDefinition.waitForLoaded(unloadGuard, 0);
    if (!status.ok()) {
        return status;
    }

    response->Clear();
    response->set_name(mediapipeGraphDefinition.getName());
    response->add_versions("1");
    response->set_platform(PLATFORM);

    for (const auto& input : mediapipeGraphDefinition.getInputsInfo()) {
        convert(input, response->add_inputs());
    }

    for (const auto& output : mediapipeGraphDefinition.getOutputsInfo()) {
        convert(output, response->add_outputs());
    }

    return StatusCode::OK;
}
#endif

void KFSInferenceServiceImpl::convert(
    const std::pair<std::string, std::shared_ptr<const TensorInfo>>& from,
    KFSModelMetadataResponse::TensorMetadata* to) {
    to->set_name(from.first);
    to->set_datatype(ovmsPrecisionToKFSPrecision(from.second->getPrecision()));
    for (auto& dim : from.second->getShape()) {
        if (dim.isStatic()) {
            to->add_shape(dim.getStaticValue());
        } else {
            to->add_shape(DYNAMIC_DIMENSION);
        }
    }
}

}  // namespace ovms
