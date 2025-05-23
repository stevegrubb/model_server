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
#include "kfs_utils.hpp"

#include <algorithm>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

#include "../logging.hpp"
#include "../profiler.hpp"
#include "../status.hpp"
#include "../tensorinfo.hpp"
#include "../tensor_conversion_common.hpp"

namespace ovms {
Precision KFSPrecisionToOvmsPrecision(const KFSDataType& datatype) {
    static std::unordered_map<KFSDataType, Precision> precisionMap{
        {"BOOL", Precision::BOOL},
        {"FP64", Precision::FP64},
        {"FP32", Precision::FP32},
        {"FP16", Precision::FP16},
        {"INT64", Precision::I64},
        {"INT32", Precision::I32},
        {"INT16", Precision::I16},
        {"INT8", Precision::I8},
        {"UINT64", Precision::U64},
        {"UINT32", Precision::U32},
        {"UINT16", Precision::U16},
        {"BYTES", Precision::STRING},
        {"UINT8", Precision::U8}};
    auto it = precisionMap.find(datatype);
    if (it == precisionMap.end()) {
        return Precision::UNDEFINED;
    }
    return it->second;
}

size_t KFSDataTypeSize(const KFSDataType& datatype) {
    static std::unordered_map<KFSDataType, size_t> datatypeSizeMap{
        {"BOOL", 1},
        {"UINT8", 1},
        {"UINT16", 2},
        {"UINT32", 4},
        {"UINT64", 8},
        {"INT8", 1},
        {"INT16", 2},
        {"INT32", 4},
        {"INT64", 8},
        {"FP16", 2},
        {"FP32", 4},
        {"FP64", 8},
        {"BYTES", 1}};
    auto it = datatypeSizeMap.find(datatype);
    if (it == datatypeSizeMap.end()) {
        return 0;
    }
    return it->second;
}

const KFSDataType& ovmsPrecisionToKFSPrecision(Precision precision) {
    static std::unordered_map<Precision, KFSDataType> precisionMap{
        {Precision::FP64, "FP64"},
        {Precision::FP32, "FP32"},
        {Precision::FP16, "FP16"},
        {Precision::I64, "INT64"},
        {Precision::I32, "INT32"},
        {Precision::I16, "INT16"},
        {Precision::I8, "INT8"},
        {Precision::U64, "UINT64"},
        {Precision::U32, "UINT32"},
        {Precision::U16, "UINT16"},
        {Precision::U8, "UINT8"},
        {Precision::STRING, "BYTES"},
        {Precision::BOOL, "BOOL"}};
    // {Precision::BF16, ""},
    // {Precision::U4, ""},
    // {Precision::U1, ""},
    // {Precision::CUSTOM, ""},
    // {Precision::DYNAMIC, ""},
    // {Precision::MIXED, ""},
    // {Precision::Q78, ""},
    // {Precision::BIN, ""},
    // {Precision::I4, ""},
    // {Precision::UNDEFINED, "UNDEFINED"}};
    auto it = precisionMap.find(precision);
    if (it == precisionMap.end()) {
        static const std::string invalid{"INVALID"};
        return invalid;
    }
    return it->second;
}

std::string tensorShapeToString(const KFSShapeType& shape) {
    std::ostringstream oss;
    oss << "(";
    int i = 0;
    if (shape.size() > 0) {
        for (; i < shape.size() - 1; i++) {
            oss << shape[i] << ",";
        }
        oss << shape[i];
    }
    oss << ")";

    return oss.str();
}

Status prepareConsolidatedTensorImpl(KFSResponse* response, const std::string& name, ov::element::Type_t precision, const ov::Shape& shape, char*& bufferOut, size_t size) {
    OVMS_PROFILE_FUNCTION();
    for (int i = 0; i < response->outputs_size(); i++) {
        if (response->mutable_outputs(i)->name() == name) {
            SPDLOG_LOGGER_ERROR(dag_executor_logger, "Failed to prepare consolidated tensor, tensor with name {} already prepared", name);
            return StatusCode::INTERNAL_ERROR;
        }
    }
    auto* proto = response->add_outputs();
    proto->set_name(name);
    auto* content = response->add_raw_output_contents();
    content->resize(size);
    bufferOut = content->data();
    return StatusCode::OK;
}
const std::string& getRequestServableName(const KFSRequest& request) {
    return request.model_name();
}
Status isNativeFileFormatUsed(const KFSRequest& request, const std::string& name, bool& nativeFileFormatUsed) {
    auto it = request.inputs().begin();
    while (it != request.inputs().end()) {
        if (it->name() == name) {
            break;
        }
        ++it;
    }
    if (it == request.inputs().end()) {
        SPDLOG_ERROR("Error during checking binary input; input: {} does not exist for request: {}", name, getRequestServableName(request));
        return StatusCode::INTERNAL_ERROR;
    }
    nativeFileFormatUsed = isNativeFileFormatUsed(*it);
    return StatusCode::OK;
}

bool isNativeFileFormatUsed(const KFSTensorInputProto& proto) {
    return proto.datatype() == "BYTES";
}

bool requiresPreProcessing(const KFSTensorInputProto& proto) {
    return proto.datatype() == "BYTES";
}

std::string& createOrGetString(KFSTensorOutputProto& proto, int index) {
    while (proto.contents().bytes_contents_size() <= index) {
        proto.mutable_contents()->add_bytes_contents();
    }
    return *proto.mutable_contents()->mutable_bytes_contents(index);
}
void setBatchSize(KFSTensorOutputProto& proto, int64_t batch) {
    if (proto.shape_size() == 0) {
        proto.add_shape(batch);
    } else {
        proto.set_shape(0, batch);
    }
}
void setStringPrecision(KFSTensorOutputProto& proto) {
    proto.set_datatype("BYTES");
}

Status validateRequestCoherencyKFS(const KFSRequest& request, const std::string servableName, model_version_t servableVersion) {
    if (!request.raw_input_contents().empty()) {
        for (auto& input : request.inputs()) {
            if (input.has_contents()) {
                std::stringstream ss;
                ss << "Passing buffers both in InferInputTensor contents and in raw_input_contents is not allowed. Detected buffer in InferInputTensor contents for input: " << input.name();
                const std::string details = ss.str();
                SPDLOG_DEBUG("[servable name: {} version: {}] Invalid request message - {}", servableName, servableVersion, details);
                return Status(StatusCode::INVALID_MESSAGE_STRUCTURE, details);
            }
        }
    }
    return StatusCode::OK;
}
size_t getElementsCount(const KFSTensorInputProto& proto, ovms::Precision expectedPrecision) {
    switch (expectedPrecision) {
    case ovms::Precision::BOOL: {
        return proto.contents().bool_contents().size();
    }
        /// int_contents
    case ovms::Precision::I8:
    case ovms::Precision::I16:
    case ovms::Precision::I32: {
        return proto.contents().int_contents().size();
    }
        /// int64_contents
    case ovms::Precision::I64: {
        return proto.contents().int64_contents().size();
    }
        // uint_contents
    case ovms::Precision::U8:
    case ovms::Precision::U16:
    case ovms::Precision::U32: {
        return proto.contents().uint_contents().size();
    }
        // uint64_contents
    case ovms::Precision::U64: {
        return proto.contents().uint64_contents().size();
    }
        // fp32_contents
    case ovms::Precision::FP32: {
        return proto.contents().fp32_contents().size();
    }
        // fp64_contentes
    case ovms::Precision::FP64: {
        return proto.contents().fp64_contents().size();
    }
    case ovms::Precision::STRING: {
        return proto.contents().bytes_contents().size();
    }
    case ovms::Precision::FP16:
    case ovms::Precision::U1:
    case ovms::Precision::CUSTOM:
    case ovms::Precision::UNDEFINED:
    case ovms::Precision::DYNAMIC:
    case ovms::Precision::MIXED:
    case ovms::Precision::Q78:
    case ovms::Precision::BIN:
    default:
        return 0;
    }
}
Status validateTensor(const TensorInfo& tensorInfo,
    const ::KFSRequest::InferInputTensor& src,
    const std::string* buffer) {
    OVMS_PROFILE_FUNCTION();
    bool rawInputsContentsUsed = (buffer != nullptr);
    auto status = tensor_conversion::validateLayout(tensorInfo);
    if (!status.ok()) {
        return status;
    }
    // 4 for default pipelines, 5 for pipelines with demultiplication at entry
    bool isShapeLengthValid = tensorInfo.getShape().size() == 4 ||
                              (tensorInfo.isInfluencedByDemultiplexer() && tensorInfo.getShape().size() == 5);
    if (!isShapeLengthValid) {
        return StatusCode::INVALID_SHAPE;
    }

    size_t batchSize = !rawInputsContentsUsed ? src.contents().bytes_contents_size() : tensor_conversion::getNumberOfInputs(buffer);
    if (tensor_conversion::checkBatchSizeMismatch(tensorInfo, batchSize)) {
        SPDLOG_DEBUG("Input: {} request batch size is incorrect. Expected: {} Actual: {}",
            tensorInfo.getMappedName(),
            tensorInfo.getBatchSize().has_value() ? tensorInfo.getBatchSize().value().toString() : std::string{"none"},
            src.contents().bytes_contents_size());
        return StatusCode::INVALID_BATCH_SIZE;
    }

    if (!rawInputsContentsUsed) {
        for (size_t i = 0; i < batchSize; i++) {
            if (src.contents().bytes_contents(i).size() <= 0) {
                SPDLOG_DEBUG("Tensor: {} {}th image of the batch is empty.", src.name(), i);
                return StatusCode::BYTES_CONTENTS_EMPTY;
            }
        }
    } else {
        if (buffer->size() <= 0) {
            SPDLOG_DEBUG("Tensor: {} raw_inputs_contents is empty", src.name());
            return StatusCode::BYTES_CONTENTS_EMPTY;
        }
    }

    return StatusCode::OK;
}

const std::string& getBinaryInput(const ::KFSRequest::InferInputTensor& tensor, size_t i) {
    return tensor.contents().bytes_contents(i);
}
int getBinaryInputsSize(const ::KFSRequest::InferInputTensor& tensor) {
    return tensor.contents().bytes_contents_size();
}

Status convertBinaryExtensionStringFromBufferToNativeOVTensor(const ::KFSRequest::InferInputTensor& src, ov::Tensor& tensor, const std::string* buffer) {
    std::vector<uint32_t> stringSizes;
    uint32_t totalStringsLength = 0;
    while (totalStringsLength + stringSizes.size() * sizeof(uint32_t) + sizeof(uint32_t) <= buffer->size()) {
        uint32_t inputSize = *(reinterpret_cast<const uint32_t*>(buffer->data() + totalStringsLength + stringSizes.size() * sizeof(uint32_t)));
        stringSizes.push_back(inputSize);
        totalStringsLength += inputSize;
    }
    size_t batchSize = stringSizes.size();
    if ((totalStringsLength + batchSize * sizeof(uint32_t)) != buffer->size()) {
        SPDLOG_DEBUG("Input string format conversion failed");
        return StatusCode::INVALID_STRING_INPUT;
    }
    tensor = ov::Tensor(ov::element::Type_t::string, ov::Shape{batchSize});
    std::string* data = tensor.data<std::string>();
    size_t tensorStringsOffset = 0;
    for (size_t i = 0; i < stringSizes.size(); i++) {
        data[i].assign(reinterpret_cast<const char*>(buffer->data() + (i + 1) * sizeof(uint32_t) + tensorStringsOffset), stringSizes[i]);
        tensorStringsOffset += stringSizes[i];
    }
    return StatusCode::OK;
}
}  // namespace ovms
