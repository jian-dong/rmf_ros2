/*
 * Copyright (C) 2020 Open Source Robotics Foundation
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
*/

#include <rmf_fleet_adapter/agv/Adapter.hpp>

#include "Node.hpp"
#include "internal_FleetUpdateHandle.hpp"

#include <rmf_traffic_ros2/schedule/MirrorManager.hpp>
#include <rmf_traffic_ros2/schedule/Negotiation.hpp>
#include <rmf_traffic_ros2/schedule/Writer.hpp>

namespace rmf_fleet_adapter {
namespace agv {

//==============================================================================
class Adapter::Implementation
{
public:

  rxcpp::schedulers::worker worker;
  std::shared_ptr<Node> node;
  std::shared_ptr<rmf_traffic_ros2::schedule::Negotiation> negotiation;
  std::shared_ptr<rmf_traffic_ros2::schedule::Writer> writer;
  rmf_utils::optional<rmf_traffic_ros2::schedule::MirrorManager> mirror_manager;

  std::vector<std::shared_ptr<FleetUpdateHandle>> fleets;

  Implementation(
      const std::string& node_name,
      const rclcpp::NodeOptions& node_options,
      const rmf_traffic::Duration wait_time)
    : worker(rxcpp::schedulers::make_event_loop().create_worker())
  {
    node = std::make_shared<Node>(node_name, node_options);

    auto mirror_future = rmf_traffic_ros2::schedule::make_mirror(
          *node, rmf_traffic::schedule::query_all());

    writer = rmf_traffic_ros2::schedule::Writer::make(*node);

    using namespace std::chrono_literals;

    const auto stop_time = std::chrono::steady_clock::now() + wait_time;
    while (rclcpp::ok() && std::chrono::steady_clock::now() < stop_time)
    {
      rclcpp::spin_some(node);

      bool ready = true;
      ready &= writer->ready();
      ready &= (mirror_future.wait_for(0s) == std::future_status::ready);

      if (ready)
        break;
    }

    mirror_manager = mirror_future.get();

    negotiation = std::make_shared<rmf_traffic_ros2::schedule::Negotiation>(
          *node, mirror_manager->snapshot_handle());
  }
};

//==============================================================================
std::shared_ptr<Adapter> Adapter::make(
    const std::string& node_name,
    const rclcpp::NodeOptions& node_options,
    const rmf_traffic::Duration wait_time)
{
  Adapter adapter;
  adapter._pimpl = rmf_utils::make_unique_impl<Implementation>(
        node_name, node_options, wait_time);
  return std::make_shared<Adapter>(std::move(adapter));
}

//==============================================================================
std::shared_ptr<FleetUpdateHandle> Adapter::add_fleet(
    const std::string& fleet_name,
    rmf_traffic::agv::VehicleTraits traits,
    rmf_traffic::agv::Graph navigation_graph)
{
  auto planner = std::make_shared<rmf_traffic::agv::Planner>(
        rmf_traffic::agv::Planner::Configuration(
          std::move(navigation_graph),
          std::move(traits)),
        rmf_traffic::agv::Planner::Options(nullptr));

  auto fleet = FleetUpdateHandle::Implementation::make(
        fleet_name, std::move(planner), _pimpl->node, _pimpl->worker,
        _pimpl->writer, _pimpl->mirror_manager->snapshot_handle(),
        _pimpl->negotiation);

  _pimpl->fleets.push_back(fleet);
  return fleet;
}

//==============================================================================
Adapter::Adapter()
{
  // Do nothing
}

} // namespace agv
} // namespace rmf_fleet_adapter
