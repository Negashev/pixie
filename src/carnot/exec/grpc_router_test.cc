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

#include "src/carnot/exec/grpc_router.h"

#include <algorithm>
#include <chrono>
#include <string>
#include <thread>
#include <utility>

#include <absl/strings/substitute.h>
#include <grpcpp/grpcpp.h>
#include <grpcpp/security/credentials.h>
#include <grpcpp/security/server_credentials.h>
#include <grpcpp/server.h>
#include <gtest/gtest.h>

#include "src/api/proto/uuidpb/uuid.pb.h"
#include "src/carnot/exec/exec_node_mock.h"
#include "src/carnot/exec/exec_state.h"
#include "src/carnot/exec/grpc_source_node.h"
#include "src/carnot/exec/test_utils.h"
#include "src/carnot/plan/operators.h"
#include "src/carnot/planpb/test_proto.h"
#include "src/carnot/udf/registry.h"
#include "src/common/base/base.h"
#include "src/common/testing/testing.h"
#include "src/shared/types/arrow_adapter.h"
#include "src/shared/types/types.h"
#include "src/table_store/schemapb/schema.pb.h"
#include "src/table_store/table_store.h"

namespace px {
namespace carnot {
namespace exec {

using grpc::Channel;
using grpc::InsecureChannelCredentials;
using grpc::InsecureServerCredentials;
using grpc::Server;
using grpc::ServerBuilder;
using table_store::schema::RowDescriptor;

class GRPCRouterTest : public ::testing::Test {
 protected:
  GRPCRouterTest() { service_ = std::make_unique<GRPCRouter>(); }

  void SetUp() override {
    // Setup server.
    ServerBuilder builder;
    builder.AddListeningPort("localhost:0", InsecureServerCredentials());
    builder.RegisterService(service_.get());
    server_ = builder.BuildAndStart();

    grpc::ChannelArguments args;
    stub_ = carnotpb::ResultSinkService::NewStub(server_->InProcessChannel(args));
  }

  void TearDown() override { server_->Shutdown(); }

  std::unique_ptr<carnotpb::ResultSinkService::Stub> stub_;
  std::unique_ptr<Server> server_;
  std::unique_ptr<GRPCRouter> service_;
};

class FakeGRPCSourceNode : public px::carnot::exec::GRPCSourceNode {
 public:
  Status EnqueueRowBatch(std::unique_ptr<px::carnotpb::TransferResultChunkRequest> row_batch) {
    row_batches.emplace_back(std::move(row_batch));

    return Status::OK();
  }

  std::vector<std::unique_ptr<px::carnotpb::TransferResultChunkRequest>> row_batches;
};

TEST_F(GRPCRouterTest, no_node_router_test) {
  int64_t grpc_source_node_id = 1;
  uint64_t ab = 0xea8aa095697f49f1, cd = 0xb127d50e5b6e2645;

  carnotpb::TransferResultChunkRequest initiate_stream_req0;
  auto query_id = initiate_stream_req0.mutable_query_id();
  query_id->set_high_bits(ab);
  query_id->set_low_bits(cd);
  initiate_stream_req0.mutable_query_result()->set_grpc_source_id(grpc_source_node_id);
  initiate_stream_req0.mutable_query_result()->set_initiate_result_stream(true);

  // Create row batches.
  RowDescriptor input_rd({types::DataType::INT64});
  auto rb1 = RowBatchBuilder(input_rd, 2, /*eow*/ false, /*eos*/ false)
                 .AddColumn<types::Int64Value>({1, 2})
                 .get();
  carnotpb::TransferResultChunkRequest rb_req1;
  EXPECT_OK(rb1.ToProto(rb_req1.mutable_query_result()->mutable_row_batch()));
  rb_req1.mutable_query_result()->set_grpc_source_id(grpc_source_node_id);
  query_id = rb_req1.mutable_query_id();
  query_id->set_high_bits(ab);
  query_id->set_low_bits(cd);

  auto rb2 = RowBatchBuilder(input_rd, 2, /*eow*/ false, /*eos*/ false)
                 .AddColumn<types::Int64Value>({4, 6})
                 .get();
  carnotpb::TransferResultChunkRequest rb_req2;
  EXPECT_OK(rb2.ToProto(rb_req2.mutable_query_result()->mutable_row_batch()));
  rb_req2.mutable_query_result()->set_grpc_source_id(grpc_source_node_id);
  query_id = rb_req2.mutable_query_id();
  query_id->set_high_bits(ab);
  query_id->set_low_bits(cd);

  // Send row batches to GRPC router.
  carnotpb::TransferResultChunkResponse response;
  grpc::ClientContext context;
  auto writer = stub_->TransferResultChunk(&context, &response);
  writer->Write(initiate_stream_req0);
  writer->Write(rb_req1);
  writer->Write(rb_req2);
  writer->WritesDone();
  writer->Finish();

  // Add source node to GRPC router.
  auto query_uuid = sole::rebuild(ab, cd);
  auto op_proto = planpb::testutils::CreateTestGRPCSource1PB();
  std::unique_ptr<px::carnot::plan::Operator> plan_node =
      plan::GRPCSourceOperator::FromProto(op_proto, grpc_source_node_id);
  auto source_node = FakeGRPCSourceNode();
  ASSERT_OK(source_node.Init(*plan_node, input_rd, {}));

  auto s = service_->AddGRPCSourceNode(query_uuid, grpc_source_node_id, &source_node, [] {});
  ASSERT_OK(s);

  EXPECT_TRUE(source_node.upstream_initiated_connection());
  EXPECT_EQ(2, source_node.row_batches.size());
  EXPECT_EQ(1,
            source_node.row_batches.at(0)->query_result().row_batch().cols(0).int64_data().data(0));
  EXPECT_EQ(4,
            source_node.row_batches.at(1)->query_result().row_batch().cols(0).int64_data().data(0));
  EXPECT_TRUE(source_node.upstream_closed_connection());
}

TEST_F(GRPCRouterTest, basic_router_test) {
  int64_t grpc_source_node_id = 1;
  uint64_t ab = 0xea8aa095697f49f1, cd = 0xb127d50e5b6e2645;

  RowDescriptor input_rd({types::DataType::INT64});
  auto query_uuid = sole::rebuild(ab, cd);

  // Add source node to GRPC router.
  auto op_proto = planpb::testutils::CreateTestGRPCSource1PB();
  std::unique_ptr<px::carnot::plan::Operator> plan_node =
      plan::GRPCSourceOperator::FromProto(op_proto, grpc_source_node_id);
  auto source_node = FakeGRPCSourceNode();
  ASSERT_OK(source_node.Init(*plan_node, input_rd, {}));

  auto num_continues = 0;
  auto s = service_->AddGRPCSourceNode(query_uuid, grpc_source_node_id, &source_node,
                                       [&] { num_continues++; });
  ASSERT_OK(s);
  EXPECT_FALSE(source_node.upstream_initiated_connection());
  EXPECT_EQ(0, source_node.row_batches.size());
  EXPECT_EQ(0, num_continues);
  EXPECT_FALSE(source_node.upstream_closed_connection());

  carnotpb::TransferResultChunkRequest initiate_stream_req0;
  auto query_id = initiate_stream_req0.mutable_query_id();
  query_id->set_high_bits(ab);
  query_id->set_low_bits(cd);
  initiate_stream_req0.mutable_query_result()->set_grpc_source_id(grpc_source_node_id);
  initiate_stream_req0.mutable_query_result()->set_initiate_result_stream(true);

  // Create row batches.
  auto rb1 = RowBatchBuilder(input_rd, 2, /*eow*/ false, /*eos*/ false)
                 .AddColumn<types::Int64Value>({1, 2})
                 .get();
  carnotpb::TransferResultChunkRequest rb_req1;
  EXPECT_OK(rb1.ToProto(rb_req1.mutable_query_result()->mutable_row_batch()));
  rb_req1.mutable_query_result()->set_grpc_source_id(grpc_source_node_id);
  query_id = rb_req1.mutable_query_id();
  query_id->set_high_bits(ab);
  query_id->set_low_bits(cd);

  auto rb2 = RowBatchBuilder(input_rd, 2, /*eow*/ false, /*eos*/ false)
                 .AddColumn<types::Int64Value>({4, 6})
                 .get();
  carnotpb::TransferResultChunkRequest rb_req2;
  EXPECT_OK(rb2.ToProto(rb_req2.mutable_query_result()->mutable_row_batch()));
  rb_req2.mutable_query_result()->set_grpc_source_id(grpc_source_node_id);
  query_id = rb_req2.mutable_query_id();
  query_id->set_high_bits(ab);
  query_id->set_low_bits(cd);

  // Send row batches to GRPC router.
  px::carnotpb::TransferResultChunkResponse response;
  grpc::ClientContext context;
  auto writer = stub_->TransferResultChunk(&context, &response);
  writer->Write(initiate_stream_req0);
  writer->Write(rb_req1);
  writer->Write(rb_req2);
  writer->WritesDone();
  writer->Finish();

  EXPECT_TRUE(source_node.upstream_initiated_connection());
  EXPECT_EQ(2, source_node.row_batches.size());
  EXPECT_EQ(1,
            source_node.row_batches.at(0)->query_result().row_batch().cols(0).int64_data().data(0));
  EXPECT_EQ(4,
            source_node.row_batches.at(1)->query_result().row_batch().cols(0).int64_data().data(0));
  EXPECT_EQ(2, num_continues);
  EXPECT_TRUE(source_node.upstream_closed_connection());
}

TEST_F(GRPCRouterTest, router_and_stats_test) {
  int64_t grpc_source_node_id = 1;
  uint64_t ab = 0xea8aa095697f49f1, cd = 0xb127d50e5b6e2645;
  auto query_uuid = sole::rebuild(ab, cd);

  uint64_t ef = 0xe302d3f9f20a44a3, gh = 0xbdc536fc14ed9089;
  auto agent_uuid = sole::rebuild(ef, gh);

  RowDescriptor input_rd({types::DataType::INT64});

  // Add source node to GRPC router.
  auto op_proto = planpb::testutils::CreateTestGRPCSource1PB();
  std::unique_ptr<px::carnot::plan::Operator> plan_node =
      plan::GRPCSourceOperator::FromProto(op_proto, grpc_source_node_id);
  auto source_node = FakeGRPCSourceNode();
  ASSERT_OK(source_node.Init(*plan_node, input_rd, {}));

  auto num_continues = 0;
  auto s = service_->AddGRPCSourceNode(query_uuid, grpc_source_node_id, &source_node,
                                       [&] { num_continues++; });
  ASSERT_OK(s);
  EXPECT_FALSE(source_node.upstream_initiated_connection());
  EXPECT_EQ(0, source_node.row_batches.size());
  EXPECT_EQ(0, num_continues);
  EXPECT_FALSE(source_node.upstream_closed_connection());

  carnotpb::TransferResultChunkRequest initiate_stream_req0;
  auto query_id = initiate_stream_req0.mutable_query_id();
  query_id->set_high_bits(ab);
  query_id->set_low_bits(cd);
  initiate_stream_req0.mutable_query_result()->set_grpc_source_id(grpc_source_node_id);
  initiate_stream_req0.mutable_query_result()->set_initiate_result_stream(true);

  // Create row batch.
  auto rb1 = RowBatchBuilder(input_rd, 2, /*eow*/ false, /*eos*/ false)
                 .AddColumn<types::Int64Value>({1, 2})
                 .get();
  carnotpb::TransferResultChunkRequest rb_req1;
  EXPECT_OK(rb1.ToProto(rb_req1.mutable_query_result()->mutable_row_batch()));
  rb_req1.mutable_query_result()->set_grpc_source_id(grpc_source_node_id);
  query_id = rb_req1.mutable_query_id();
  query_id->set_high_bits(ab);
  query_id->set_low_bits(cd);

  // Send row batches to GRPC router.
  px::carnotpb::TransferResultChunkResponse response;
  grpc::ClientContext context;
  auto writer = stub_->TransferResultChunk(&context, &response);
  writer->Write(initiate_stream_req0);
  writer->Write(rb_req1);
  writer->WritesDone();
  writer->Finish();

  {
    ::px::carnotpb::TransferResultChunkRequest stats_req;
    ::px::carnotpb::TransferResultChunkResponse stats_resp;

    auto agent_stats = stats_req.mutable_execution_and_timing_info()->add_agent_execution_stats();
    ToProto(agent_uuid, agent_stats->mutable_agent_id());
    ToProto(query_uuid, stats_req.mutable_query_id());
    auto mem_src_stats = agent_stats->add_operator_execution_stats();
    mem_src_stats->set_bytes_output(123);
    mem_src_stats->set_records_output(1);
    mem_src_stats->set_total_execution_time_ns(10000);
    mem_src_stats->set_self_execution_time_ns(5000);

    auto grpc_sink_stats = agent_stats->add_operator_execution_stats();
    grpc_sink_stats->set_bytes_output(0);
    grpc_sink_stats->set_records_output(0);
    grpc_sink_stats->set_total_execution_time_ns(5000);
    grpc_sink_stats->set_self_execution_time_ns(5000);
    grpc::ClientContext stats_context;
    auto writer = stub_->TransferResultChunk(&stats_context, &stats_resp);
    writer->Write(stats_req);
    writer->WritesDone();
    auto s = writer->Finish();
    EXPECT_TRUE(s.ok());
  }

  EXPECT_TRUE(source_node.upstream_initiated_connection());
  EXPECT_EQ(1, source_node.row_batches.size());
  EXPECT_EQ(1,
            source_node.row_batches.at(0)->query_result().row_batch().cols(0).int64_data().data(0));
  EXPECT_EQ(1, num_continues);
  EXPECT_TRUE(source_node.upstream_closed_connection());

  uuidpb::UUID agent_uuid_pb;
  ToProto(agent_uuid, &agent_uuid_pb);
  auto exec_stats_or_s = service_->GetIncomingWorkerExecStats(query_uuid, {agent_uuid_pb});

  auto exec_stats = exec_stats_or_s.ConsumeValueOrDie();
  EXPECT_EQ(exec_stats.size(), 1);
  LOG(INFO) << exec_stats[0].DebugString();
}

TEST_F(GRPCRouterTest, delete_node_router_test) {
  int64_t grpc_source_node_id = 1;
  RowDescriptor input_rd({types::DataType::INT64, types::DataType::BOOLEAN});
  auto query_uuid = sole::rebuild("ea8aa095-697f-49f1-b127-d50e5b6e2645");

  auto op_proto = planpb::testutils::CreateTestGRPCSource1PB();
  std::unique_ptr<px::carnot::plan::Operator> plan_node =
      plan::GRPCSourceOperator::FromProto(op_proto, grpc_source_node_id);
  auto source_node = FakeGRPCSourceNode();
  ASSERT_OK(source_node.Init(*plan_node, input_rd, {}));

  auto s = service_->AddGRPCSourceNode(query_uuid, grpc_source_node_id, &source_node, [] {});
  ASSERT_OK(s);

  service_->DeleteQuery(query_uuid);
}

TEST_F(GRPCRouterTest, done_query_test) {
  int64_t grpc_source_node_id = 1;
  RowDescriptor input_rd({types::DataType::INT64, types::DataType::BOOLEAN});
  auto query_uuid = sole::rebuild("ea8aa095-697f-49f1-b127-d50e5b6e2645");

  auto op_proto = planpb::testutils::CreateTestGRPCSource1PB();
  std::unique_ptr<px::carnot::plan::Operator> plan_node =
      plan::GRPCSourceOperator::FromProto(op_proto, grpc_source_node_id);
  auto source_node = FakeGRPCSourceNode();
  ASSERT_OK(source_node.Init(*plan_node, input_rd, {}));

  auto s = service_->AddGRPCSourceNode(query_uuid, grpc_source_node_id, &source_node, [] {});
  ASSERT_OK(s);

  service_->DeleteQuery(query_uuid);
}

// This test is a TSAN test. IT should be run enough times so that all possible
// race conditions will be met.
TEST_F(GRPCRouterTest, threaded_router_test) {
  uint64_t ab = 0xea8aa095697f49f1, cd = 0xb127d50e5b6e2645;
  auto query_uuid = sole::rebuild(ab, cd);

  auto func_registry_ = std::make_unique<udf::Registry>("test_registry");
  auto table_store = std::make_shared<table_store::TableStore>();
  auto exec_state = std::make_unique<ExecState>(
      func_registry_.get(), table_store, MockResultSinkStubGenerator, sole::uuid4(), nullptr);

  MockExecNode mock_child;

  RowDescriptor input_rd({types::DataType::INT64});
  auto op_proto = planpb::testutils::CreateTestGRPCSource1PB();
  std::unique_ptr<px::carnot::plan::Operator> plan_node =
      plan::GRPCSourceOperator::FromProto(op_proto, 1);
  auto source_node = GRPCSourceNode();
  ASSERT_OK(source_node.Init(*plan_node, input_rd, {}));
  source_node.AddChild(&mock_child, 0);
  ASSERT_OK(source_node.Open(exec_state.get()));
  ASSERT_OK(source_node.Prepare(exec_state.get()));

  FakePlanNode fake_plan_node(111);
  // Silence GMOCK warnings.
  EXPECT_CALL(mock_child, InitImpl(::testing::_));
  EXPECT_CALL(mock_child, PrepareImpl(::testing::_));
  EXPECT_CALL(mock_child, OpenImpl(::testing::_));
  ASSERT_OK(mock_child.Init(fake_plan_node, RowDescriptor({}), {}));
  ASSERT_OK(mock_child.Open(exec_state.get()));
  ASSERT_OK(mock_child.Prepare(exec_state.get()));

  px::carnotpb::TransferResultChunkResponse response;
  grpc::ClientContext context;
  auto writer = stub_->TransferResultChunk(&context, &response);

  carnotpb::TransferResultChunkRequest initiate_stream_req0;
  auto query_id = initiate_stream_req0.mutable_query_id();
  query_id->set_high_bits(ab);
  query_id->set_low_bits(cd);
  initiate_stream_req0.mutable_query_result()->set_grpc_source_id(0);
  initiate_stream_req0.mutable_query_result()->set_initiate_result_stream(true);

  // Start up thread that enqueues row batches.
  std::thread write_thread([&] {
    writer->Write(initiate_stream_req0);
    for (int idx = 0; idx <= 100; ++idx) {
      auto rb = RowBatchBuilder(input_rd, /*size*/ 1, /*eow*/ idx == 100, /*eos*/ idx == 100)
                    .AddColumn<types::Int64Value>({
                        idx,
                    })
                    .get();
      carnotpb::TransferResultChunkRequest rb_req;
      EXPECT_OK(rb.ToProto(rb_req.mutable_query_result()->mutable_row_batch()));
      rb_req.mutable_query_result()->set_grpc_source_id(0);
      auto query_id = rb_req.mutable_query_id();
      query_id->set_high_bits(ab);
      query_id->set_low_bits(cd);
      writer->Write(rb_req);
    }
    writer->WritesDone();
    writer->Finish();
  });
  write_thread.join();

  // Start up thread which adds the source node to the router and verifies rowbatches have been
  // enqueued in correct order.
  std::thread read_thread([&] {
    auto idx = 0;

    auto s = service_->AddGRPCSourceNode(query_uuid, /* source_id */ 0, &source_node, [] {});
    ASSERT_OK(s);
    do {
      auto check_result_batch = [&](ExecState*, const table_store::schema::RowBatch& rb, int64_t) {
        EXPECT_EQ(idx,
                  types::GetValueFromArrowArray<types::DataType::INT64>(rb.ColumnAt(0).get(), 0));
      };

      EXPECT_CALL(mock_child, ConsumeNextImpl(::testing::_, ::testing::_, ::testing::_))
          .Times(1)
          .WillRepeatedly(::testing::DoAll(::testing::Invoke(check_result_batch),
                                           ::testing::Return(Status::OK())))
          .RetiresOnSaturation();

      ASSERT_OK(source_node.GenerateNext(exec_state.get()));
      idx++;
    } while (source_node.HasBatchesRemaining());
  });
  // Sleep for 1ms to allow some row batches to enqueue before the source node is added.
  std::this_thread::sleep_for(std::chrono::milliseconds(1));
  read_thread.join();
}

}  // namespace exec
}  // namespace carnot
}  // namespace px
