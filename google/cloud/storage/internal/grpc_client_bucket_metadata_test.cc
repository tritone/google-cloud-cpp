// Copyright 2020 Google LLC
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

#include "google/cloud/storage/internal/grpc_client.h"
#include "google/cloud/testing_util/assert_ok.h"
#include "google/cloud/testing_util/is_proto_equal.h"
#include <google/protobuf/text_format.h>
#include <gmock/gmock.h>

namespace google {
namespace cloud {
namespace storage {
inline namespace STORAGE_CLIENT_NS {
namespace internal {
namespace {

namespace storage_proto = ::google::storage::v1;
using ::google::cloud::testing_util::IsProtoEqual;

TEST(GrpcClientBucketMetadata, BucketAccessControlFrom) {
  auto constexpr kText = R"pb(
    role: "test-role"
    etag: "test-etag"
    id: "test-id"
    bucket: "test-bucket"
    entity: "test-entity"
    entity_id: "test-entity-id"
    email: "test-email"
    domain: "test-domain"
    project_team: { project_number: "test-project-number" team: "test-team" }
  )pb";
  google::storage::v1::BucketAccessControl input;
  EXPECT_TRUE(google::protobuf::TextFormat::ParseFromString(kText, &input));

  auto const expected = BucketAccessControlParser::FromString(R"""({
     "role": "test-role",
     "etag": "test-etag",
     "id": "test-id",
     "kind": "storage#bucketAccessControl",
     "bucket": "test-bucket",
     "entity": "test-entity",
     "entityId": "test-entity-id",
     "email": "test-email",
     "domain": "test-domain",
     "projectTeam": {
       "projectNumber": "test-project-number",
       "team": "test-team"
     }
  })""");
  ASSERT_STATUS_OK(expected);

  auto actual = GrpcClient::FromProto(input);
  EXPECT_EQ(*expected, actual);
}

TEST(GrpcClientBucketMetadata, BucketAccessControlToProto) {
  auto acl = BucketAccessControlParser::FromString(R"""({
     "role": "test-role",
     "etag": "test-etag",
     "id": "test-id",
     "kind": "storage#bucketAccessControl",
     "bucket": "test-bucket",
     "entity": "test-entity",
     "entityId": "test-entity-id",
     "email": "test-email",
     "domain": "test-domain",
     "projectTeam": {
       "projectNumber": "test-project-number",
       "team": "test-team"
     }
  })""");
  ASSERT_STATUS_OK(acl);
  auto actual = GrpcClient::ToProto(*acl);

  auto constexpr kText = R"pb(
    role: "test-role"
    etag: "test-etag"
    id: "test-id"
    bucket: "test-bucket"
    entity: "test-entity"
    entity_id: "test-entity-id"
    email: "test-email"
    domain: "test-domain"
    project_team: { project_number: "test-project-number" team: "test-team" }
  )pb";
  google::storage::v1::BucketAccessControl expected;
  EXPECT_TRUE(google::protobuf::TextFormat::ParseFromString(kText, &expected));

  EXPECT_THAT(actual, IsProtoEqual(expected));
}

TEST(GrpcClientBucketMetadata, BucketAccessControlMinimalFields) {
  BucketAccessControl acl;
  acl.set_role("test-role");
  acl.set_entity("test-entity");
  auto actual = GrpcClient::ToProto(acl);

  google::storage::v1::BucketAccessControl expected;
  auto constexpr kText = R"pb(
    role: "test-role" entity: "test-entity"
  )pb";
  EXPECT_TRUE(google::protobuf::TextFormat::ParseFromString(kText, &expected));

  EXPECT_THAT(actual, IsProtoEqual(expected));
}

TEST(GrpcClientBucketMetadata, BucketAllFields) {
  storage_proto::Bucket input;
  EXPECT_TRUE(google::protobuf::TextFormat::ParseFromString(R"""(
# TODO(#4174) - convert acl() field.
# TODO(#4173) - convert default_object_acl() field.
# TODO(#4165) - convert lifecycle
    time_created: {
      seconds: 1565194924
      nanos: 123456789
    }
    id: "test-bucket-id"
    name: "test-bucket"
    project_number: 123456
    metageneration: 1234567
# TODO(#4169) - convert cors() field.
    location: "test-location"
    storage_class: "test-storage-class"
    etag: "test-etag"
    updated: {
      seconds: 1565194924
      nanos: 123456789
    }
    default_event_based_hold: true
    labels: { key: "test-key-1" value: "test-value-1" }
    labels: { key: "test-key-2" value: "test-value-2" }
# TODO(#4168) - convert website() field.
# TODO(#4167) - convert versioning() field.
# TODO(#4172) - convert logging() field.
# TODO(#4170) - convert owner() field.
# TODO(#4171) - convert encryption() field.
# TODO(#4164) - convert billing() field.
# TODO(#4166) - convert retention_policy() field.
)""",
                                                            &input));

  // To get the dates in RFC-3339 format I used:
  //     date --rfc-3339=seconds --date=@1565194924
  auto expected = BucketMetadataParser::FromString(R"""({
    "timeCreated": "2019-08-07T16:22:04.123456789Z",
    "id": "test-bucket-id",
    "kind": "storage#bucket",
    "name": "test-bucket",
    "projectNumber": 123456,
    "metageneration": 1234567,
    "location": "test-location",
    "storageClass": "test-storage-class",
    "etag": "test-etag",
    "updated": "2019-08-07T16:22:04.123456789Z",
    "defaultEventBasedHold": true,
    "labels": {
        "test-key-1": "test-value-1",
        "test-key-2": "test-value-2"
    }
})""");
  EXPECT_STATUS_OK(expected);

  auto actual = GrpcClient::FromProto(input);
  EXPECT_EQ(actual, *expected);
}

TEST(GrpcClientBucketMetadata, BucketMetadata) {
  auto input = BucketMetadataParser::FromString(R"""({
    "name": "test-bucket"
})""");
  EXPECT_STATUS_OK(input);

  storage_proto::Bucket expected;
  EXPECT_TRUE(google::protobuf::TextFormat::ParseFromString(R"""(
    name: "test-bucket"
# TODO(#4173) - convert the other fields.
)""",
                                                            &expected));

  auto actual = GrpcClient::ToProto(*input);
  EXPECT_THAT(actual, IsProtoEqual(expected));
}

TEST(GrpcClientBucketMetadata, BucketBillingRoundtrip) {
  auto constexpr kText = R"pb(
    requester_pays: true
  )pb";
  google::storage::v1::Bucket::Billing start;
  EXPECT_TRUE(google::protobuf::TextFormat::ParseFromString(kText, &start));
  auto const expected = BucketBilling{true};
  auto const middle = GrpcClient::FromProto(start);
  EXPECT_EQ(middle, expected);
  auto const end = GrpcClient::ToProto(middle);
  EXPECT_THAT(end, IsProtoEqual(start));
}

TEST(GrpcClientBucketMetadata, BucketCorsRoundtrip) {
  auto constexpr kText = R"pb(
    origin: "test-origin-1"
    origin: "test-origin-2"
    method: "GET"
    method: "PUT"
    response_header: "test-header-1"
    response_header: "test-header-2"
    max_age_seconds: 3600
  )pb";
  google::storage::v1::Bucket::Cors start;
  EXPECT_TRUE(google::protobuf::TextFormat::ParseFromString(kText, &start));
  auto const expected = CorsEntry{3600,
                                  {"GET", "PUT"},
                                  {"test-origin-1", "test-origin-2"},
                                  {"test-header-1", "test-header-2"}};
  auto const middle = GrpcClient::FromProto(start);
  EXPECT_EQ(middle, expected);
  auto const end = GrpcClient::ToProto(middle);
  EXPECT_THAT(end, IsProtoEqual(start));
}

TEST(GrpcClientBucketMetadata, BucketEncryptionRoundtrip) {
  auto constexpr kText = R"pb(
    default_kms_key_name: "projects/test-p/locations/us/keyRings/test-kr/cryptoKeys/test-key"
  )pb";
  google::storage::v1::Bucket::Encryption start;
  EXPECT_TRUE(google::protobuf::TextFormat::ParseFromString(kText, &start));
  auto const expected = BucketEncryption{
      "projects/test-p/locations/us/keyRings/test-kr/cryptoKeys/test-key"};
  auto const middle = GrpcClient::FromProto(start);
  EXPECT_EQ(middle, expected);
  auto const end = GrpcClient::ToProto(middle);
  EXPECT_THAT(end, IsProtoEqual(start));
}

TEST(GrpcClientBucketMetadata, BucketIamConfigurationRoundtrip) {
  auto constexpr kText = R"pb(
    uniform_bucket_level_access {
      enabled: true
      locked_time { seconds: 1234 nanos: 5678000 }
    }
  )pb";
  google::storage::v1::Bucket::IamConfiguration start;
  EXPECT_TRUE(google::protobuf::TextFormat::ParseFromString(kText, &start));
  auto tp = std::chrono::system_clock::time_point{} +
            std::chrono::duration_cast<std::chrono::system_clock::duration>(
                std::chrono::seconds(1234) + std::chrono::nanoseconds(5678000));
  auto const expected =
      BucketIamConfiguration{{}, UniformBucketLevelAccess{true, tp}};
  auto const middle = GrpcClient::FromProto(start);
  EXPECT_EQ(middle, expected);
  auto const end = GrpcClient::ToProto(middle);
  EXPECT_THAT(end, IsProtoEqual(start));
}

TEST(GrpcClientBucketMetadata, BucketLoggingRoundtrip) {
  auto constexpr kText = R"pb(
    log_bucket: "test-bucket-name"
    log_object_prefix: "test-object-prefix/"
  )pb";
  google::storage::v1::Bucket::Logging start;
  EXPECT_TRUE(google::protobuf::TextFormat::ParseFromString(kText, &start));
  auto const expected =
      BucketLogging{"test-bucket-name", "test-object-prefix/"};
  auto const middle = GrpcClient::FromProto(start);
  EXPECT_EQ(middle, expected);
  auto const end = GrpcClient::ToProto(middle);
  EXPECT_THAT(end, IsProtoEqual(start));
}

TEST(GrpcClientBucketMetadata, BucketRetentionPolicyRoundtrip) {
  auto constexpr kText = R"pb(
    retention_period: 3600
    effective_time { seconds: 1234 nanos: 5678000 }
    is_locked: true
  )pb";
  google::storage::v1::Bucket::RetentionPolicy start;
  EXPECT_TRUE(google::protobuf::TextFormat::ParseFromString(kText, &start));
  auto tp = std::chrono::system_clock::time_point{} +
            std::chrono::duration_cast<std::chrono::system_clock::duration>(
                std::chrono::seconds(1234) + std::chrono::nanoseconds(5678000));
  auto const expected =
      BucketRetentionPolicy{std::chrono::seconds(3600), tp, true};
  auto const middle = GrpcClient::FromProto(start);
  EXPECT_EQ(middle, expected);
  auto const end = GrpcClient::ToProto(middle);
  EXPECT_THAT(end, IsProtoEqual(start));
}

TEST(GrpcClientBucketMetadata, BucketVersioningRoundtrip) {
  auto constexpr kText = R"pb(
    enabled: true
  )pb";
  google::storage::v1::Bucket::Versioning start;
  EXPECT_TRUE(google::protobuf::TextFormat::ParseFromString(kText, &start));
  auto const expected = BucketVersioning{true};
  auto const middle = GrpcClient::FromProto(start);
  EXPECT_EQ(middle, expected);
  auto const end = GrpcClient::ToProto(middle);
  EXPECT_THAT(end, IsProtoEqual(start));
}

TEST(GrpcClientBucketMetadata, BucketWebsiteRoundtrip) {
  auto constexpr kText = R"pb(
    main_page_suffix: "index.html"
    not_found_page: "404.html"
  )pb";
  google::storage::v1::Bucket::Website start;
  EXPECT_TRUE(google::protobuf::TextFormat::ParseFromString(kText, &start));
  auto const expected = BucketWebsite{"index.html", "404.html"};
  auto const middle = GrpcClient::FromProto(start);
  EXPECT_EQ(middle, expected);
  auto const end = GrpcClient::ToProto(middle);
  EXPECT_THAT(end, IsProtoEqual(start));
}

}  // namespace
}  // namespace internal
}  // namespace STORAGE_CLIENT_NS
}  // namespace storage
}  // namespace cloud
}  // namespace google
