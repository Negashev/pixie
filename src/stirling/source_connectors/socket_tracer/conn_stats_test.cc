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

#include "src/stirling/source_connectors/socket_tracer/conn_stats.h"

#include <memory>

#include <absl/container/flat_hash_map.h>

#include "src/common/testing/testing.h"
#include "src/stirling/source_connectors/socket_tracer/testing/event_generator.h"

namespace px {
namespace stirling {

using ::testing::AllOf;
using ::testing::Field;
using ::testing::IsEmpty;
using ::testing::Pair;
using ::testing::SizeIs;

TEST(HashTest, CanBeUsedInFlatHashMap) {
  absl::flat_hash_map<ConnStats::AggKey, int> map;
  EXPECT_THAT(map, IsEmpty());

  ConnStats::AggKey key = {
      .upid = {.tgid = 1, .start_time_ticks = 2},
      .remote_addr = "test",
      .remote_port = 12345,
  };

  map[key] = 1;
  EXPECT_THAT(map, SizeIs(1));
  map[key] = 2;
  EXPECT_THAT(map, SizeIs(1));

  ConnStats::AggKey key_diff_upid = key;
  key_diff_upid.upid = {.tgid = 1, .start_time_ticks = 3},

  map[key_diff_upid] = 1;
  EXPECT_THAT(map, SizeIs(2));
  map[key] = 2;
  EXPECT_THAT(map, SizeIs(2));
}

class ConnStatsTest : public ::testing::Test {
 protected:
  ConnStatsTest() : event_gen_(&mock_clock_) { tracker_.set_conn_stats(&conn_stats_); }

  ConnStats conn_stats_;
  ConnTracker tracker_;

  testing::MockClock mock_clock_;
  testing::EventGenerator event_gen_;
};

auto AggKeyIs(int tgid, std::string_view remote_addr) {
  return AllOf(Field(&ConnStats::AggKey::upid, Field(&upid_t::tgid, tgid)),
               Field(&ConnStats::AggKey::remote_addr, remote_addr));
}

auto StatsIs(int open, int close, int sent, int recv) {
  return AllOf(
      Field(&ConnStats::Stats::conn_open, open), Field(&ConnStats::Stats::conn_close, close),
      Field(&ConnStats::Stats::bytes_sent, sent), Field(&ConnStats::Stats::bytes_recv, recv));
}

// Tests that aggregated records for client side events are correctly put into ConnStats.
TEST_F(ConnStatsTest, ClientSizeAggregationRecord) {
  struct socket_control_event_t conn = event_gen_.InitConn(kRoleClient);
  auto* sockaddr = reinterpret_cast<struct sockaddr_in*>(&conn.open.addr);
  sockaddr->sin_family = AF_INET;
  sockaddr->sin_port = 54321;
  sockaddr->sin_addr.s_addr = 0x01010101;  // 1.1.1.1

  auto frame1 = event_gen_.InitSendEvent(kProtocolHTTP, kRoleClient, "abc");
  auto frame2 = event_gen_.InitSendEvent(kProtocolHTTP, kRoleClient, "def");
  auto frame3 = event_gen_.InitRecvEvent(kProtocolHTTP, kRoleClient, "1234");
  auto frame4 = event_gen_.InitRecvEvent(kProtocolHTTP, kRoleClient, "5");
  auto frame5 = event_gen_.InitRecvEvent(kProtocolHTTP, kRoleClient, "6789");

  struct socket_control_event_t close_event = event_gen_.InitClose();

  // This sets up the remote address and port.
  tracker_.AddControlEvent(conn);

  EXPECT_THAT(conn_stats_.mutable_agg_stats(),
              ElementsAre(Pair(AggKeyIs(12345, "1.1.1.1"), StatsIs(1, 0, 0, 0))));

  tracker_.AddDataEvent(std::move(frame1));

  EXPECT_THAT(conn_stats_.mutable_agg_stats(),
              ElementsAre(Pair(AggKeyIs(12345, "1.1.1.1"), StatsIs(1, 0, 3, 0))));

  tracker_.AddDataEvent(std::move(frame2));
  tracker_.AddDataEvent(std::move(frame3));
  tracker_.AddDataEvent(std::move(frame4));

  EXPECT_THAT(conn_stats_.mutable_agg_stats(),
              ElementsAre(Pair(AggKeyIs(12345, "1.1.1.1"), StatsIs(1, 0, 6, 5))));

  tracker_.AddDataEvent(std::move(frame5));

  EXPECT_THAT(conn_stats_.mutable_agg_stats(),
              ElementsAre(Pair(AggKeyIs(12345, "1.1.1.1"), StatsIs(1, 0, 6, 9))));

  tracker_.AddControlEvent(close_event);

  EXPECT_THAT(conn_stats_.mutable_agg_stats(),
              ElementsAre(Pair(AggKeyIs(12345, "1.1.1.1"), StatsIs(1, 1, 6, 9))));

  // Tests that after receiving conn close event for a connection, another same close event won't
  // increment the connection.
  tracker_.AddControlEvent(close_event);
  EXPECT_THAT(conn_stats_.mutable_agg_stats(),
              ElementsAre(Pair(AggKeyIs(12345, "1.1.1.1"), StatsIs(1, 1, 6, 9))));
}

// Tests that aggregated records for server side events are correctly put into ConnStats.
TEST_F(ConnStatsTest, ServerSizeAggregationRecord) {
  struct socket_control_event_t conn = event_gen_.InitConn(kRoleServer);
  auto* sockaddr = reinterpret_cast<struct sockaddr_in*>(&conn.open.addr);
  sockaddr->sin_family = AF_INET;
  sockaddr->sin_port = 54321;
  sockaddr->sin_addr.s_addr = 0x01010101;  // 1.1.1.1

  auto frame1 = event_gen_.InitSendEvent(kProtocolHTTP, kRoleServer, "abc");
  auto frame2 = event_gen_.InitSendEvent(kProtocolHTTP, kRoleServer, "def");
  auto frame3 = event_gen_.InitRecvEvent(kProtocolHTTP, kRoleServer, "1234");
  auto frame4 = event_gen_.InitRecvEvent(kProtocolHTTP, kRoleServer, "5");
  auto frame5 = event_gen_.InitRecvEvent(kProtocolHTTP, kRoleServer, "6789");

  struct socket_control_event_t close_event = event_gen_.InitClose();

  // This sets up the remote address and port.
  tracker_.AddControlEvent(conn);

  EXPECT_THAT(conn_stats_.mutable_agg_stats(),
              ElementsAre(Pair(AggKeyIs(12345, "1.1.1.1"), StatsIs(1, 0, 0, 0))));

  tracker_.AddDataEvent(std::move(frame1));

  EXPECT_THAT(conn_stats_.mutable_agg_stats(),
              ElementsAre(Pair(AggKeyIs(12345, "1.1.1.1"), StatsIs(1, 0, 3, 0))));

  tracker_.AddDataEvent(std::move(frame2));
  tracker_.AddDataEvent(std::move(frame3));
  tracker_.AddDataEvent(std::move(frame4));

  EXPECT_THAT(conn_stats_.mutable_agg_stats(),
              ElementsAre(Pair(AggKeyIs(12345, "1.1.1.1"), StatsIs(1, 0, 6, 5))));

  tracker_.AddDataEvent(std::move(frame5));

  EXPECT_THAT(conn_stats_.mutable_agg_stats(),
              ElementsAre(Pair(AggKeyIs(12345, "1.1.1.1"), StatsIs(1, 0, 6, 9))));

  tracker_.AddControlEvent(close_event);

  EXPECT_THAT(conn_stats_.mutable_agg_stats(),
              ElementsAre(Pair(AggKeyIs(12345, "1.1.1.1"), StatsIs(1, 1, 6, 9))));

  // Tests that after receiving conn close event for a connection, another same close event won't
  // increment the connection.
  tracker_.AddControlEvent(close_event);
  EXPECT_THAT(conn_stats_.mutable_agg_stats(),
              ElementsAre(Pair(AggKeyIs(12345, "1.1.1.1"), StatsIs(1, 1, 6, 9))));
}

// Tests that any connection trackers with no remote endpoint do not report conn stats events.
TEST_F(ConnStatsTest, NoEventsIfNoRemoteAddr) {
  auto frame1 = event_gen_.InitSendEvent(kProtocolHTTP, kRoleClient, "foo");

  tracker_.AddDataEvent(std::move(frame1));

  EXPECT_THAT(conn_stats_.mutable_agg_stats(), IsEmpty());
}

// Tests that disabled ConnTracker still reports data.
TEST_F(ConnStatsTest, DisabledConnTracker) {
  struct socket_control_event_t conn = event_gen_.InitConn();
  auto* sockaddr = reinterpret_cast<struct sockaddr_in*>(&conn.open.addr);
  sockaddr->sin_family = AF_INET;
  sockaddr->sin_port = 54321;
  sockaddr->sin_addr.s_addr = 0x01010101;  // 1.1.1.1

  auto frame1 = event_gen_.InitSendEvent(kProtocolHTTP, kRoleClient, "abc");

  // Main test sequence.
  tracker_.AddControlEvent(conn);
  tracker_.Disable("test");
  tracker_.AddDataEvent(std::move(frame1));

  EXPECT_THAT(conn_stats_.mutable_agg_stats(),
              ElementsAre(Pair(AggKeyIs(12345, "1.1.1.1"), StatsIs(1, 0, 3, 0))));
}

}  // namespace stirling
}  // namespace px
