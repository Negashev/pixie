/*
 * Copyright 2018- The Pixie Authors.
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
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <string>
#include <vector>

#include "src/common/base/base.h"
#include "src/common/system/system.h"
#include "src/shared/types/types.h"
#include "src/stirling/core/connector_context.h"
#include "src/stirling/core/data_table.h"
#include "src/stirling/core/utils.h"

/**
 * These are the steps to follow to add a new data source connector.
 * 1. If required, create a new SourceConnector class.
 * 2. Add a new Create function with the following signature:
 *    static std::unique_ptr<SourceConnector> Create().
 *    In this function create an InfoClassSchema (vector of DataElement)
 * 3. Register the data source in the appropriate registry.
 */

DECLARE_bool(stirling_source_connector_output_multiple_data_tables);

namespace px {
namespace stirling {

class SourceConnector : public NotCopyable {
 public:
  SourceConnector() = delete;
  virtual ~SourceConnector() = default;

  /**
   * Initializes the source connector. Can only be called once.
   * @return Status of whether initialization was successful.
   */
  Status Init();

  /**
   * Sets the initial context for the source connector.
   * Used for context specific init steps (e.g. deploying uprobes on PIDs).
   */
  void InitContext(ConnectorContext* ctx);

  /**
   * Transfers any collected data, for the specified table, into the provided record_batch.
   * @param ctx Shared context, e.g. ASID & tracked PIDs.
   * @param table_num The table number (id) of the data. See DataTableSchemas in individual
   * connectors.
   * @param data_table Destination for the data.
   */
  void TransferData(ConnectorContext* ctx, uint32_t table_num, DataTable* data_table);

  /**
   * Transfers all collected data to data tables.
   * @param ctx Shared context, e.g. ASID & tracked PIDs.
   * @param data_tables Map from the table number to DataTable objects.
   */
  void TransferData(ConnectorContext* ctx, const std::vector<DataTable*>& data_tables);

  /**
   * Stops the source connector and releases any acquired resources.
   * May only be called after a successful Init().
   *
   * @return Status of whether stop was successful.
   */
  Status Stop();

  /**
   * If true, the overlaoded TransferData() is implemented, and the sampled data can be output to
   * multiple data tables.
   *
   * This is temporary, will be removed once all SouceConnector subclasses are changed to output
   * data to multiple data tables.
   */
  virtual bool output_multi_tables() const { return false; }

  const std::string& name() const { return source_name_; }

  uint32_t num_tables() const { return table_schemas_.size(); }

  const DataTableSchema& TableSchema(uint32_t table_num) const {
    DCHECK_LT(table_num, num_tables())
        << absl::Substitute("Access to table out of bounds: table_num=$0", table_num);
    return table_schemas_[table_num];
  }

  static constexpr uint32_t TableNum(ArrayView<DataTableSchema> tables,
                                     const DataTableSchema& key) {
    uint32_t i = 0;
    for (i = 0; i < tables.size(); i++) {
      if (tables[i].name() == key.name()) {
        break;
      }
    }

    // Check that we found the index. This prevents compilation if name is not found,
    // during constexpr evaluation (which is awesome!).
    COMPILE_TIME_ASSERT(i != tables.size(), "Could not find name");

    return i;
  }

  /**
   * Utility function to convert time as recorded by in monotonic clock to real time.
   * This is especially useful for converting times from BPF, which are all in monotonic clock.
   */
  uint64_t ClockRealTimeOffset() const { return sysconfig_.ClockRealTimeOffset(); }

  uint64_t AdjustedSteadyClockNowNS() const {
    auto now = std::chrono::steady_clock::now();
    return std::chrono::duration_cast<std::chrono::nanoseconds>(now.time_since_epoch()).count() +
           ClockRealTimeOffset();
  }

  const SamplePushFrequencyManager& sample_push_mgr() const { return sample_push_freq_mgr_; }
  SamplePushFrequencyManager* mutable_sample_push_mgr() { return &sample_push_freq_mgr_; }

  virtual void SetDebugLevel(int level) { debug_level_ = level; }
  virtual void EnablePIDTrace(int pid) { pids_to_trace_.insert(pid); }
  virtual void DisablePIDTrace(int pid) { pids_to_trace_.erase(pid); }

 protected:
  explicit SourceConnector(std::string_view source_name,
                           const ArrayView<DataTableSchema>& table_schemas)
      : source_name_(source_name), table_schemas_(table_schemas) {}

  virtual Status InitImpl() = 0;

  // Provide a default InitContextImpl which does nothing.
  // SourceConnectors only need override if action is required on the initial context.
  virtual void InitContextImpl(ConnectorContext* /* ctx */) {}

  virtual void TransferDataImpl(ConnectorContext* ctx, uint32_t table_num,
                                DataTable* data_table) = 0;
  virtual void TransferDataImpl(ConnectorContext*, const std::vector<DataTable*>&) {
    // TODO(yzhao): Change to pure virtual function after all subclasses are updated.
    LOG(DFATAL) << "TransferStreams() Unimplemented";
  }

  virtual Status StopImpl() = 0;

 protected:
  /**
   * Track state of connector. A connector's lifetime typically progresses sequentially
   * from kUninitialized -> kActive -> KStopped.
   *
   * kErrors is a special state to track a bad state.
   */
  enum class State { kUninitialized, kActive, kStopped, kErrors };

  // Sub-classes are allowed to inspect state.
  State state() const { return state_; }

  const system::Config& sysconfig_ = system::Config::GetInstance();

  SamplePushFrequencyManager sample_push_freq_mgr_;

  // Debug members.
  int debug_level_ = 0;
  absl::flat_hash_set<int> pids_to_trace_;

 private:
  std::atomic<State> state_ = State::kUninitialized;

  const std::string source_name_;
  const ArrayView<DataTableSchema> table_schemas_;
};

}  // namespace stirling
}  // namespace px
