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
#pragma once
#include <exception>
#include <sstream>
#include <string>
#include <unordered_map>

#include "notifyreceiver.hpp"

namespace ovms {
struct NotifyReceiver;

class ModelChangeSubscription {
    const std::string ownerName;
    std::unordered_map<std::string, NotifyReceiver&> subscriptions;

public:
    ModelChangeSubscription(const std::string& ownerName) :
        ownerName(ownerName) {}

    void subscribe(NotifyReceiver& pd);

    void unsubscribe(NotifyReceiver& pd);

    void notifySubscribers();

    bool isSubscribed() const { return subscriptions.size() > 0; }
};
}  // namespace ovms
