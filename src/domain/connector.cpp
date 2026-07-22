// SPDX-License-Identifier: Apache-2.0

#include <graphscore/domain/connector.hpp>

#include <string>
#include <utility>

namespace graphscore {

InputConnector::InputConnector(std::string name)
    : id_(ConnectorId::generate()), name_(std::move(name)) {}

OutputConnector::OutputConnector(std::string name, ConnectorType type)
    : id_(ConnectorId::generate()), name_(std::move(name)), type_(type) {}

}  // namespace graphscore
