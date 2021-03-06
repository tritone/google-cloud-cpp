// Copyright 2019 Google LLC
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
#include "google/cloud/storage/internal/grpc_object_read_source.h"
#include "google/cloud/storage/internal/grpc_resumable_upload_session.h"
#include "google/cloud/storage/internal/openssl_util.h"
#include "google/cloud/storage/internal/resumable_upload_session.h"
#include "google/cloud/storage/internal/sha256_hash.h"
#include "google/cloud/storage/oauth2/anonymous_credentials.h"
#include "google/cloud/grpc_error_delegate.h"
#include "google/cloud/internal/big_endian.h"
#include "google/cloud/internal/getenv.h"
#include "google/cloud/internal/invoke_result.h"
#include "google/cloud/log.h"
#include "absl/algorithm/container.h"
#include <crc32c/crc32c.h>
#include <grpcpp/grpcpp.h>
#include <algorithm>

namespace google {
namespace cloud {
namespace storage {
inline namespace STORAGE_CLIENT_NS {
namespace internal {

std::size_t constexpr GrpcClient::kMaxInsertObjectWriteRequestSize;

std::string GrpcEndpoint() {
  auto env = google::cloud::internal::GetEnv("CLOUD_STORAGE_GRPC_ENDPOINT");
  if (env.has_value()) {
    return env.value();
  }
  return "storage.googleapis.com";
}

std::shared_ptr<grpc::ChannelCredentials> GrpcCredentials(
    ClientOptions const& options) {
  auto env = google::cloud::internal::GetEnv("CLOUD_STORAGE_GRPC_ENDPOINT");
  if (env.has_value()) {
    return grpc::InsecureChannelCredentials();
  }
  if (dynamic_cast<oauth2::AnonymousCredentials*>(
          options.credentials().get()) != nullptr) {
    return grpc::InsecureChannelCredentials();
  }
  return grpc::GoogleDefaultCredentials();
}

GrpcClient::GrpcClient(ClientOptions options) : options_(std::move(options)) {
  auto channel = grpc::CreateChannel(GrpcEndpoint(), GrpcCredentials(options_));
  stub_ = google::storage::v1::Storage::NewStub(channel);
}

std::unique_ptr<GrpcClient::UploadWriter> GrpcClient::CreateUploadWriter(
    grpc::ClientContext& context, google::storage::v1::Object& result) {
  auto concrete_writer = stub_->InsertObject(&context, &result);
  return std::unique_ptr<GrpcClient::UploadWriter>(concrete_writer.release());
}

StatusOr<ResumableUploadResponse> GrpcClient::QueryResumableUpload(
    QueryResumableUploadRequest const& request) {
  grpc::ClientContext context;
  auto const proto_request = ToProto(request);
  google::storage::v1::QueryWriteStatusResponse response;
  auto status = stub_->QueryWriteStatus(&context, proto_request, &response);
  if (!status.ok()) return google::cloud::MakeStatusFromRpcError(status);

  return ResumableUploadResponse{
      {},
      static_cast<std::uint64_t>(response.committed_size()),
      // TODO(b/146890058) - `response` should include the object metadata.
      ObjectMetadata{},
      response.complete() ? ResumableUploadResponse::kDone
                          : ResumableUploadResponse::kInProgress,
      {}};
}

ClientOptions const& GrpcClient::client_options() const { return options_; }

StatusOr<ListBucketsResponse> GrpcClient::ListBuckets(
    ListBucketsRequest const& request) {
  grpc::ClientContext context;
  auto proto_request = ToProto(request);
  google::storage::v1::ListBucketsResponse response;
  auto status = stub_->ListBuckets(&context, proto_request, &response);
  if (!status.ok()) return google::cloud::MakeStatusFromRpcError(status);

  ListBucketsResponse res;
  res.next_page_token = std::move(*response.mutable_next_page_token());
  for (auto& item : *response.mutable_items()) {
    res.items.emplace_back(BucketMetadata().set_name(item.name()));
  }

  return res;
}

StatusOr<BucketMetadata> GrpcClient::CreateBucket(
    CreateBucketRequest const& request) {
  grpc::ClientContext context;
  auto proto_request = ToProto(request);
  google::storage::v1::Bucket response;
  auto status = stub_->InsertBucket(&context, proto_request, &response);
  if (!status.ok()) return google::cloud::MakeStatusFromRpcError(status);

  return FromProto(response);
}

StatusOr<BucketMetadata> GrpcClient::GetBucketMetadata(
    GetBucketMetadataRequest const& request) {
  grpc::ClientContext context;
  google::storage::v1::Bucket response;
  auto proto_request = ToProto(request);
  auto status = stub_->GetBucket(&context, proto_request, &response);
  if (!status.ok()) return google::cloud::MakeStatusFromRpcError(status);

  return FromProto(std::move(response));
}

StatusOr<EmptyResponse> GrpcClient::DeleteBucket(
    DeleteBucketRequest const& request) {
  grpc::ClientContext context;
  auto proto_request = ToProto(request);
  google::protobuf::Empty response;
  auto status = stub_->DeleteBucket(&context, proto_request, &response);
  if (!status.ok()) return google::cloud::MakeStatusFromRpcError(status);

  return EmptyResponse{};
}

StatusOr<BucketMetadata> GrpcClient::UpdateBucket(UpdateBucketRequest const&) {
  return Status(StatusCode::kUnimplemented, __func__);
}

StatusOr<BucketMetadata> GrpcClient::PatchBucket(PatchBucketRequest const&) {
  return Status(StatusCode::kUnimplemented, __func__);
}

StatusOr<IamPolicy> GrpcClient::GetBucketIamPolicy(
    GetBucketIamPolicyRequest const&) {
  return Status(StatusCode::kUnimplemented, __func__);
}

StatusOr<NativeIamPolicy> GrpcClient::GetNativeBucketIamPolicy(
    GetBucketIamPolicyRequest const&) {
  return Status(StatusCode::kUnimplemented, __func__);
}

StatusOr<IamPolicy> GrpcClient::SetBucketIamPolicy(
    SetBucketIamPolicyRequest const&) {
  return Status(StatusCode::kUnimplemented, __func__);
}

StatusOr<NativeIamPolicy> GrpcClient::SetNativeBucketIamPolicy(
    SetNativeBucketIamPolicyRequest const&) {
  return Status(StatusCode::kUnimplemented, __func__);
}

StatusOr<TestBucketIamPermissionsResponse> GrpcClient::TestBucketIamPermissions(
    TestBucketIamPermissionsRequest const&) {
  return Status(StatusCode::kUnimplemented, __func__);
}

StatusOr<BucketMetadata> GrpcClient::LockBucketRetentionPolicy(
    LockBucketRetentionPolicyRequest const&) {
  return Status(StatusCode::kUnimplemented, __func__);
}

StatusOr<ObjectMetadata> GrpcClient::InsertObjectMedia(
    InsertObjectMediaRequest const& request) {
  grpc::ClientContext context;
  google::storage::v1::Object response;
  auto stream = stub_->InsertObject(&context, &response);
  auto proto_request = ToProto(request);
  // This limit is for the *message*, not just the payload. It includes any
  // additional information such as checksums. We need to use a stricter limit,
  // a chunk quantum seems to work in practice.
  std::size_t const maximum_buffer_size =
      kMaxInsertObjectWriteRequestSize - UploadChunkRequest::kChunkSizeQuantum;
  auto const& contents = request.contents();

  // This loop must run at least once because we need to send at least one
  // Write() call for empty objects.
  std::size_t offset = 0;
  do {
    proto_request.set_write_offset(offset);
    auto& data = *proto_request.mutable_checksummed_data();
    auto const n = (std::min)(contents.size() - offset, maximum_buffer_size);
    data.set_content(contents.substr(offset, n));
    data.mutable_crc32c()->set_value(crc32c::Crc32c(data.content()));

    grpc::WriteOptions options;
    if (offset + n >= contents.size()) {
      options.set_last_message();
      proto_request.set_finish_write(true);
    }
    if (!stream->Write(proto_request, options)) break;
    // After the first message, clear the object specification and checksums,
    // there is no need to resend it.
    proto_request.clear_insert_object_spec();
    proto_request.clear_object_checksums();
    offset += n;
  } while (offset < contents.size());

  auto status = stream->Finish();
  if (!status.ok()) return google::cloud::MakeStatusFromRpcError(status);

  return FromProto(std::move(response));
}

StatusOr<ObjectMetadata> GrpcClient::CopyObject(CopyObjectRequest const&) {
  return Status(StatusCode::kUnimplemented, __func__);
}

StatusOr<ObjectMetadata> GrpcClient::GetObjectMetadata(
    GetObjectMetadataRequest const&) {
  return Status(StatusCode::kUnimplemented, __func__);
}

StatusOr<std::unique_ptr<ObjectReadSource>> GrpcClient::ReadObject(
    ReadObjectRangeRequest const& request) {
  // With the REST API this condition was detected by the server as an error,
  // generally we prefer the server to detect errors because its answers are
  // authoritative. In this case, the server cannot: with gRPC '0' is the same
  // as "not set" and the server would send back the full file, which was
  // unlikely to be the customer's intent.
  if (request.HasOption<ReadLast>() &&
      request.GetOption<ReadLast>().value() == 0) {
    return Status(
        StatusCode::kOutOfRange,
        "ReadLast(0) is invalid in REST and produces incorrect output in gRPC");
  }
  auto const proto_request = ToProto(request);
  auto create_stream = [&proto_request, this](grpc::ClientContext& context) {
    return stub_->GetObjectMedia(&context, proto_request);
  };

  return std::unique_ptr<ObjectReadSource>(
      new GrpcObjectReadSource(create_stream));
}

StatusOr<ListObjectsResponse> GrpcClient::ListObjects(
    ListObjectsRequest const&) {
  return Status(StatusCode::kUnimplemented, __func__);
}

StatusOr<EmptyResponse> GrpcClient::DeleteObject(
    DeleteObjectRequest const& request) {
  grpc::ClientContext context;
  auto proto_request = ToProto(request);
  google::protobuf::Empty response;
  auto status = stub_->DeleteObject(&context, proto_request, &response);
  if (!status.ok()) return google::cloud::MakeStatusFromRpcError(status);

  return EmptyResponse{};
}

StatusOr<ObjectMetadata> GrpcClient::UpdateObject(UpdateObjectRequest const&) {
  return Status(StatusCode::kUnimplemented, __func__);
}

StatusOr<ObjectMetadata> GrpcClient::PatchObject(PatchObjectRequest const&) {
  return Status(StatusCode::kUnimplemented, __func__);
}

StatusOr<ObjectMetadata> GrpcClient::ComposeObject(
    ComposeObjectRequest const&) {
  return Status(StatusCode::kUnimplemented, __func__);
}

StatusOr<RewriteObjectResponse> GrpcClient::RewriteObject(
    RewriteObjectRequest const&) {
  return Status(StatusCode::kUnimplemented, __func__);
}

StatusOr<std::unique_ptr<ResumableUploadSession>>
GrpcClient::CreateResumableSession(ResumableUploadRequest const& request) {
  if (request.HasOption<UseResumableUploadSession>()) {
    auto session_id = request.GetOption<UseResumableUploadSession>().value();
    if (!session_id.empty()) {
      return RestoreResumableSession(session_id);
    }
  }

  grpc::ClientContext context;
  auto proto_request = ToProto(request);
  google::storage::v1::StartResumableWriteResponse response;
  auto status = stub_->StartResumableWrite(&context, proto_request, &response);
  if (!status.ok()) return google::cloud::MakeStatusFromRpcError(status);

  auto self = shared_from_this();
  return std::unique_ptr<ResumableUploadSession>(
      new GrpcResumableUploadSession(self, response.upload_id()));
}

StatusOr<std::unique_ptr<ResumableUploadSession>>
GrpcClient::RestoreResumableSession(std::string const& upload_id) {
  auto self = shared_from_this();
  auto session = std::unique_ptr<ResumableUploadSession>(
      new GrpcResumableUploadSession(self, upload_id));
  auto response = session->ResetSession();
  if (response.status().ok()) {
    return session;
  }
  return std::move(response).status();
}

StatusOr<ListBucketAclResponse> GrpcClient::ListBucketAcl(
    ListBucketAclRequest const&) {
  return Status(StatusCode::kUnimplemented, __func__);
}

StatusOr<BucketAccessControl> GrpcClient::GetBucketAcl(
    GetBucketAclRequest const&) {
  return Status(StatusCode::kUnimplemented, __func__);
}

StatusOr<BucketAccessControl> GrpcClient::CreateBucketAcl(
    CreateBucketAclRequest const&) {
  return Status(StatusCode::kUnimplemented, __func__);
}

StatusOr<EmptyResponse> GrpcClient::DeleteBucketAcl(
    DeleteBucketAclRequest const&) {
  return Status(StatusCode::kUnimplemented, __func__);
}

StatusOr<ListObjectAclResponse> GrpcClient::ListObjectAcl(
    ListObjectAclRequest const&) {
  return Status(StatusCode::kUnimplemented, __func__);
}

StatusOr<BucketAccessControl> GrpcClient::UpdateBucketAcl(
    UpdateBucketAclRequest const&) {
  return Status(StatusCode::kUnimplemented, __func__);
}

StatusOr<BucketAccessControl> GrpcClient::PatchBucketAcl(
    PatchBucketAclRequest const&) {
  return Status(StatusCode::kUnimplemented, __func__);
}

StatusOr<ObjectAccessControl> GrpcClient::CreateObjectAcl(
    CreateObjectAclRequest const&) {
  return Status(StatusCode::kUnimplemented, __func__);
}

StatusOr<EmptyResponse> GrpcClient::DeleteObjectAcl(
    DeleteObjectAclRequest const&) {
  return Status(StatusCode::kUnimplemented, __func__);
}

StatusOr<ObjectAccessControl> GrpcClient::GetObjectAcl(
    GetObjectAclRequest const&) {
  return Status(StatusCode::kUnimplemented, __func__);
}

StatusOr<ObjectAccessControl> GrpcClient::UpdateObjectAcl(
    UpdateObjectAclRequest const&) {
  return Status(StatusCode::kUnimplemented, __func__);
}

StatusOr<ObjectAccessControl> GrpcClient::PatchObjectAcl(
    PatchObjectAclRequest const&) {
  return Status(StatusCode::kUnimplemented, __func__);
}

StatusOr<ListDefaultObjectAclResponse> GrpcClient::ListDefaultObjectAcl(
    ListDefaultObjectAclRequest const&) {
  return Status(StatusCode::kUnimplemented, __func__);
}

StatusOr<ObjectAccessControl> GrpcClient::CreateDefaultObjectAcl(
    CreateDefaultObjectAclRequest const&) {
  return Status(StatusCode::kUnimplemented, __func__);
}

StatusOr<EmptyResponse> GrpcClient::DeleteDefaultObjectAcl(
    DeleteDefaultObjectAclRequest const&) {
  return Status(StatusCode::kUnimplemented, __func__);
}

StatusOr<ObjectAccessControl> GrpcClient::GetDefaultObjectAcl(
    GetDefaultObjectAclRequest const&) {
  return Status(StatusCode::kUnimplemented, __func__);
}

StatusOr<ObjectAccessControl> GrpcClient::UpdateDefaultObjectAcl(
    UpdateDefaultObjectAclRequest const&) {
  return Status(StatusCode::kUnimplemented, __func__);
}

StatusOr<ObjectAccessControl> GrpcClient::PatchDefaultObjectAcl(
    PatchDefaultObjectAclRequest const&) {
  return Status(StatusCode::kUnimplemented, __func__);
}

StatusOr<ServiceAccount> GrpcClient::GetServiceAccount(
    GetProjectServiceAccountRequest const&) {
  return Status(StatusCode::kUnimplemented, __func__);
}

StatusOr<ListHmacKeysResponse> GrpcClient::ListHmacKeys(
    ListHmacKeysRequest const&) {
  return Status(StatusCode::kUnimplemented, __func__);
}

StatusOr<CreateHmacKeyResponse> GrpcClient::CreateHmacKey(
    CreateHmacKeyRequest const&) {
  return Status(StatusCode::kUnimplemented, __func__);
}

StatusOr<EmptyResponse> GrpcClient::DeleteHmacKey(DeleteHmacKeyRequest const&) {
  return Status(StatusCode::kUnimplemented, __func__);
}

StatusOr<HmacKeyMetadata> GrpcClient::GetHmacKey(GetHmacKeyRequest const&) {
  return Status(StatusCode::kUnimplemented, __func__);
}

StatusOr<HmacKeyMetadata> GrpcClient::UpdateHmacKey(
    UpdateHmacKeyRequest const&) {
  return Status(StatusCode::kUnimplemented, __func__);
}

StatusOr<SignBlobResponse> GrpcClient::SignBlob(SignBlobRequest const&) {
  return Status(StatusCode::kUnimplemented, __func__);
}

StatusOr<ListNotificationsResponse> GrpcClient::ListNotifications(
    ListNotificationsRequest const&) {
  return Status(StatusCode::kUnimplemented, __func__);
}

StatusOr<NotificationMetadata> GrpcClient::CreateNotification(
    CreateNotificationRequest const&) {
  return Status(StatusCode::kUnimplemented, __func__);
}

StatusOr<NotificationMetadata> GrpcClient::GetNotification(
    GetNotificationRequest const&) {
  return Status(StatusCode::kUnimplemented, __func__);
}

StatusOr<EmptyResponse> GrpcClient::DeleteNotification(
    DeleteNotificationRequest const&) {
  return Status(StatusCode::kUnimplemented, __func__);
}

template <typename GrpcRequest, typename StorageRequest>
void SetCommonParameters(GrpcRequest& request, StorageRequest const& req) {
  if (req.template HasOption<UserProject>()) {
    request.mutable_common_request_params()->set_user_project(
        req.template GetOption<UserProject>().value());
  }
  // The gRPC has a single field for the `QuotaUser` parameter, while the JSON
  // API has two:
  //    https://cloud.google.com/storage/docs/json_api/v1/parameters#quotaUser
  // Fortunately the semantics are to use `quotaUser` if set, so we can set
  // the `UserIp` value into the `quota_user` field, and overwrite it if
  // `QuotaUser` is also set. A bit bizarre, but at least it is backwards
  // compatible.
  if (req.template HasOption<UserIp>()) {
    request.mutable_common_request_params()->set_quota_user(
        req.template GetOption<UserIp>().value());
  }
  if (req.template HasOption<QuotaUser>()) {
    request.mutable_common_request_params()->set_quota_user(
        req.template GetOption<QuotaUser>().value());
  }
  // TODO(#4215) - what do we do with FieldMask, as the representation for
  // `fields` is different.
}

template <typename GrpcRequest, typename StorageRequest>
void SetCommonObjectParameters(GrpcRequest& request,
                               StorageRequest const& req) {
  if (req.template HasOption<EncryptionKey>()) {
    auto data = req.template GetOption<EncryptionKey>().value();
    request.mutable_common_object_request_params()->set_encryption_algorithm(
        std::move(data.algorithm));
    request.mutable_common_object_request_params()->set_encryption_key(
        std::move(data.key));
    request.mutable_common_object_request_params()->set_encryption_key_sha256(
        std::move(data.sha256));
  }
}

template <typename GrpcRequest, typename StorageRequest>
void SetProjection(GrpcRequest& request, StorageRequest const& req) {
  if (req.template HasOption<Projection>()) {
    request.set_projection(
        GrpcClient::ToProto(req.template GetOption<Projection>()));
  }
}

template <typename GrpcRequest>
struct GetPredefinedAcl {
  auto operator()(GrpcRequest const& q) -> decltype(q.predefined_acl());
};

template <
    typename GrpcRequest, typename StorageRequest,
    typename std::enable_if<
        std::is_same<google::storage::v1::CommonEnums::PredefinedBucketAcl,
                     google::cloud::internal::invoke_result_t<
                         GetPredefinedAcl<GrpcRequest>, GrpcRequest>>::value,
        int>::type = 0>
void SetPredefinedAcl(GrpcRequest& request, StorageRequest const& req) {
  if (req.template HasOption<PredefinedAcl>()) {
    request.set_predefined_acl(
        GrpcClient::ToProtoBucket(req.template GetOption<PredefinedAcl>()));
  }
}

template <
    typename GrpcRequest, typename StorageRequest,
    typename std::enable_if<
        std::is_same<google::storage::v1::CommonEnums::PredefinedObjectAcl,
                     google::cloud::internal::invoke_result_t<
                         GetPredefinedAcl<GrpcRequest>, GrpcRequest>>::value,
        int>::type = 0>
void SetPredefinedAcl(GrpcRequest& request, StorageRequest const& req) {
  if (req.template HasOption<PredefinedAcl>()) {
    request.set_predefined_acl(
        GrpcClient::ToProtoObject(req.template GetOption<PredefinedAcl>()));
  }
}

template <typename GrpcRequest, typename StorageRequest>
void SetPredefinedDefaultObjectAcl(GrpcRequest& request,
                                   StorageRequest const& req) {
  if (req.template HasOption<PredefinedAcl>()) {
    request.set_predefined_default_object_acl(GrpcClient::ToProto(
        req.template GetOption<PredefinedDefaultObjectAcl>()));
  }
}

template <typename GrpcRequest, typename StorageRequest>
void SetMetagenerationConditions(GrpcRequest& request,
                                 StorageRequest const& req) {
  if (req.template HasOption<IfMetagenerationMatch>()) {
    request.mutable_if_metageneration_match()->set_value(
        req.template GetOption<IfMetagenerationMatch>().value());
  }
  if (req.template HasOption<IfMetagenerationNotMatch>()) {
    request.mutable_if_metageneration_not_match()->set_value(
        req.template GetOption<IfMetagenerationNotMatch>().value());
  }
}

template <typename GrpcRequest, typename StorageRequest>
void SetGenerationConditions(GrpcRequest& request, StorageRequest const& req) {
  if (req.template HasOption<IfGenerationMatch>()) {
    request.mutable_if_generation_match()->set_value(
        req.template GetOption<IfGenerationMatch>().value());
  }
  if (req.template HasOption<IfGenerationNotMatch>()) {
    request.mutable_if_generation_not_match()->set_value(
        req.template GetOption<IfGenerationNotMatch>().value());
  }
}

template <typename StorageRequest>
void SetResourceOptions(google::storage::v1::Object& resource,
                        StorageRequest const& request) {
  if (request.template HasOption<ContentEncoding>()) {
    resource.set_content_encoding(
        request.template GetOption<ContentEncoding>().value());
  }
  if (request.template HasOption<ContentType>()) {
    resource.set_content_type(
        request.template GetOption<ContentType>().value());
  }

  if (request.template HasOption<Crc32cChecksumValue>()) {
    resource.mutable_crc32c()->set_value(GrpcClient::Crc32cToProto(
        request.template GetOption<Crc32cChecksumValue>().value()));
  }
  if (request.template HasOption<MD5HashValue>()) {
    resource.set_md5_hash(request.template GetOption<MD5HashValue>().value());
  }
  if (request.template HasOption<KmsKeyName>()) {
    resource.set_kms_key_name(request.template GetOption<KmsKeyName>().value());
  }
}

template <typename StorageRequest>
void SetObjectMetadata(google::storage::v1::Object& resource,
                       StorageRequest const& req) {
  if (!req.template HasOption<WithObjectMetadata>()) {
    return;
  }
  auto metadata = req.template GetOption<WithObjectMetadata>().value();
  if (!metadata.content_encoding().empty()) {
    resource.set_content_encoding(metadata.content_encoding());
  }
  if (!metadata.content_disposition().empty()) {
    resource.set_content_disposition(metadata.content_disposition());
  }
  if (!metadata.cache_control().empty()) {
    resource.set_cache_control(metadata.cache_control());
  }
  for (auto const& acl : metadata.acl()) {
    *resource.add_acl() = GrpcClient::ToProto(acl);
  }
  if (!metadata.content_language().empty()) {
    resource.set_content_language(metadata.content_language());
  }
  if (!metadata.content_type().empty()) {
    resource.set_content_type(metadata.content_type());
  }
  if (metadata.event_based_hold()) {
    resource.mutable_event_based_hold()->set_value(metadata.event_based_hold());
  }

  for (auto const& kv : metadata.metadata()) {
    (*resource.mutable_metadata())[kv.first] = kv.second;
  }

  if (!metadata.storage_class().empty()) {
    resource.set_storage_class(metadata.storage_class());
  }
  resource.set_temporary_hold(metadata.temporary_hold());
}

std::chrono::system_clock::time_point AsChronoTimepoint(
    google::protobuf::Timestamp const& ts) {
  return std::chrono::system_clock::from_time_t(ts.seconds()) +
         std::chrono::duration_cast<std::chrono::system_clock::duration>(
             std::chrono::nanoseconds(ts.nanos()));
}

google::protobuf::Timestamp ChronoTimepointToProtoTimestamp(
    std::chrono::system_clock::time_point tp) {
  auto d = tp.time_since_epoch();
  using std::chrono::duration_cast;
  auto seconds = duration_cast<std::chrono::seconds>(d);
  auto nanos = duration_cast<std::chrono::nanoseconds>(d - seconds);
  google::protobuf::Timestamp ts;
  ts.set_seconds(seconds.count());
  ts.set_nanos(nanos.count());
  return ts;
}

BucketMetadata GrpcClient::FromProto(google::storage::v1::Bucket bucket) {
  BucketMetadata metadata;
  // TODO(#4174) - convert acl() field.
  // TODO(#4173) - convert default_object_acl() field.
  // TODO(#4165) - convert lifecycle
  if (bucket.has_time_created()) {
    metadata.time_created_ = AsChronoTimepoint(bucket.time_created());
  }
  metadata.id_ = std::move(*bucket.mutable_id());
  metadata.kind_ = "storage#bucket";
  metadata.name_ = std::move(*bucket.mutable_name());
  if (bucket.has_owner()) {
    Owner o;
    o.entity = std::move(*bucket.mutable_owner()->mutable_entity());
    o.entity_id = std::move(*bucket.mutable_owner()->mutable_entity_id());
    metadata.owner_ = std::move(o);
  }
  metadata.project_number_ = bucket.project_number();
  metadata.metageneration_ = bucket.metageneration();
  // TODO(#4169) - convert cors() field.
  metadata.location_ = std::move(*bucket.mutable_location());
  metadata.storage_class_ = std::move(*bucket.mutable_storage_class());
  metadata.etag_ = std::move(*bucket.mutable_etag());
  if (bucket.has_updated()) {
    metadata.updated_ = AsChronoTimepoint(bucket.updated());
  }
  metadata.default_event_based_hold_ = bucket.default_event_based_hold();
  for (auto& kv : *bucket.mutable_labels()) {
    metadata.labels_.emplace(std::make_pair(kv.first, std::move(kv.second)));
  }
  // TODO(#4168) - convert website() field.
  // TODO(#4167) - convert versioning() field.
  // TODO(#4172) - convert logging() field.
  // TODO(#4170) - convert owner() field.
  // TODO(#4171) - convert encryption() field.
  // TODO(#4164) - convert billing() field.
  // TODO(#4166) - convert retention_policy() field.

  return metadata;
}

CustomerEncryption GrpcClient::FromProto(
    google::storage::v1::Object::CustomerEncryption rhs) {
  CustomerEncryption result;
  result.encryption_algorithm = std::move(*rhs.mutable_encryption_algorithm());
  result.key_sha256 = std::move(*rhs.mutable_key_sha256());
  return result;
}

google::storage::v1::Object::CustomerEncryption GrpcClient::ToProto(
    CustomerEncryption rhs) {
  google::storage::v1::Object::CustomerEncryption result;
  result.set_encryption_algorithm(std::move(rhs.encryption_algorithm));
  result.set_key_sha256(std::move(rhs.key_sha256));
  return result;
}

ObjectMetadata GrpcClient::FromProto(google::storage::v1::Object object) {
  ObjectMetadata metadata;
  metadata.etag_ = std::move(*object.mutable_etag());
  metadata.id_ = std::move(*object.mutable_id());
  metadata.kind_ = "storage#object";
  metadata.metageneration_ = object.metageneration();
  metadata.name_ = std::move(*object.mutable_name());
  if (object.has_owner()) {
    Owner o;
    o.entity = std::move(*object.mutable_owner()->mutable_entity());
    o.entity_id = std::move(*object.mutable_owner()->mutable_entity_id());
    metadata.owner_ = std::move(o);
  }
  metadata.storage_class_ = std::move(*object.mutable_storage_class());
  if (object.has_time_created()) {
    metadata.time_created_ = AsChronoTimepoint(object.time_created());
  }
  if (object.has_updated()) {
    metadata.updated_ = AsChronoTimepoint(object.updated());
  }
  std::vector<ObjectAccessControl> acl;
  acl.reserve(object.acl_size());
  for (auto& item : *object.mutable_acl()) {
    acl.push_back(FromProto(std::move(item)));
  }
  metadata.acl_ = std::move(acl);
  metadata.bucket_ = std::move(*object.mutable_bucket());
  metadata.cache_control_ = std::move(*object.mutable_cache_control());
  metadata.component_count_ = object.component_count();
  metadata.content_disposition_ =
      std::move(*object.mutable_content_disposition());
  metadata.content_encoding_ = std::move(*object.mutable_content_encoding());
  metadata.content_language_ = std::move(*object.mutable_content_language());
  metadata.content_type_ = std::move(*object.mutable_content_type());
  if (object.has_crc32c()) {
    metadata.crc32c_ = Crc32cFromProto(object.crc32c());
  }
  if (object.has_customer_encryption()) {
    metadata.customer_encryption_ =
        FromProto(std::move(*object.mutable_customer_encryption()));
  }
  if (object.has_event_based_hold()) {
    metadata.event_based_hold_ = object.event_based_hold().value();
  }
  metadata.generation_ = object.generation();
  metadata.kms_key_name_ = std::move(*object.mutable_kms_key_name());
  metadata.md5_hash_ = object.md5_hash();
  for (auto& kv : object.metadata()) {
    metadata.metadata_[kv.first] = kv.second;
  }
  if (object.has_retention_expiration_time()) {
    metadata.retention_expiration_time_ =
        AsChronoTimepoint(object.retention_expiration_time());
  }
  metadata.size_ = static_cast<std::uint64_t>(object.size());
  metadata.temporary_hold_ = object.temporary_hold();
  if (object.has_time_deleted()) {
    metadata.time_deleted_ = AsChronoTimepoint(object.time_deleted());
  }
  if (object.has_time_storage_class_updated()) {
    metadata.time_storage_class_updated_ =
        AsChronoTimepoint(object.time_storage_class_updated());
  }

  return metadata;
}

google::storage::v1::ObjectAccessControl GrpcClient::ToProto(
    ObjectAccessControl const& acl) {
  google::storage::v1::ObjectAccessControl result;
  result.set_role(acl.role());
  result.set_etag(acl.etag());
  result.set_id(acl.id());
  result.set_bucket(acl.bucket());
  result.set_object(acl.object());
  result.set_generation(acl.generation());
  result.set_entity(acl.entity());
  result.set_entity_id(acl.entity_id());
  result.set_email(acl.email());
  result.set_domain(acl.domain());
  if (acl.has_project_team()) {
    result.mutable_project_team()->set_project_number(
        acl.project_team().project_number);
    result.mutable_project_team()->set_team(acl.project_team().team);
  }
  return result;
}

ObjectAccessControl GrpcClient::FromProto(
    google::storage::v1::ObjectAccessControl acl) {
  ObjectAccessControl result;
  result.bucket_ = std::move(*acl.mutable_bucket());
  result.domain_ = std::move(*acl.mutable_domain());
  result.email_ = std::move(*acl.mutable_email());
  result.entity_ = std::move(*acl.mutable_entity());
  result.entity_id_ = std::move(*acl.mutable_entity_id());
  result.etag_ = std::move(*acl.mutable_etag());
  result.id_ = std::move(*acl.mutable_id());
  result.kind_ = "storage#objectAccessControl";
  if (acl.has_project_team()) {
    result.project_team_ = ProjectTeam{
        std::move(*acl.mutable_project_team()->mutable_project_number()),
        std::move(*acl.mutable_project_team()->mutable_team()),
    };
  }
  result.role_ = std::move(*acl.mutable_role());
  result.self_link_.clear();
  result.object_ = std::move(*acl.mutable_object());
  result.generation_ = acl.generation();

  return result;
}

google::storage::v1::BucketAccessControl GrpcClient::ToProto(
    BucketAccessControl const& acl) {
  google::storage::v1::BucketAccessControl result;
  result.set_role(acl.role());
  result.set_etag(acl.etag());
  result.set_id(acl.id());
  result.set_bucket(acl.bucket());
  result.set_entity(acl.entity());
  result.set_entity_id(acl.entity_id());
  result.set_email(acl.email());
  result.set_domain(acl.domain());
  if (acl.has_project_team()) {
    result.mutable_project_team()->set_project_number(
        acl.project_team().project_number);
    result.mutable_project_team()->set_team(acl.project_team().team);
  }
  return result;
}

BucketAccessControl GrpcClient::FromProto(
    google::storage::v1::BucketAccessControl acl) {
  BucketAccessControl result;
  result.bucket_ = std::move(*acl.mutable_bucket());
  result.domain_ = std::move(*acl.mutable_domain());
  result.email_ = std::move(*acl.mutable_email());
  result.entity_ = std::move(*acl.mutable_entity());
  result.entity_id_ = std::move(*acl.mutable_entity_id());
  result.etag_ = std::move(*acl.mutable_etag());
  result.id_ = std::move(*acl.mutable_id());
  result.kind_ = "storage#bucketAccessControl";
  if (acl.has_project_team()) {
    result.project_team_ = ProjectTeam{
        std::move(*acl.mutable_project_team()->mutable_project_number()),
        std::move(*acl.mutable_project_team()->mutable_team()),
    };
  }
  result.role_ = std::move(*acl.mutable_role());
  result.self_link_.clear();

  return result;
}

google::storage::v1::Bucket::Billing GrpcClient::ToProto(
    BucketBilling const& rhs) {
  google::storage::v1::Bucket::Billing result;
  result.set_requester_pays(rhs.requester_pays);
  return result;
}

BucketBilling GrpcClient::FromProto(
    google::storage::v1::Bucket::Billing const& rhs) {
  BucketBilling result;
  result.requester_pays = rhs.requester_pays();
  return result;
}

google::storage::v1::Bucket::Cors GrpcClient::ToProto(CorsEntry const& rhs) {
  google::storage::v1::Bucket::Cors result;
  for (auto const& v : rhs.origin) {
    result.add_origin(v);
  }
  for (auto const& v : rhs.method) {
    result.add_method(v);
  }
  for (auto const& v : rhs.response_header) {
    result.add_response_header(v);
  }
  if (rhs.max_age_seconds.has_value()) {
    result.set_max_age_seconds(*rhs.max_age_seconds);
  }
  return result;
}

CorsEntry GrpcClient::FromProto(google::storage::v1::Bucket::Cors const& rhs) {
  CorsEntry result;
  absl::c_copy(rhs.origin(), std::back_inserter(result.origin));
  absl::c_copy(rhs.method(), std::back_inserter(result.method));
  absl::c_copy(rhs.response_header(),
               std::back_inserter(result.response_header));
  result.max_age_seconds = rhs.max_age_seconds();
  return result;
}

google::storage::v1::Bucket::Encryption GrpcClient::ToProto(
    BucketEncryption const& rhs) {
  google::storage::v1::Bucket::Encryption result;
  result.set_default_kms_key_name(rhs.default_kms_key_name);
  return result;
}

BucketEncryption GrpcClient::FromProto(
    google::storage::v1::Bucket::Encryption const& rhs) {
  BucketEncryption result;
  result.default_kms_key_name = rhs.default_kms_key_name();
  return result;
}

google::storage::v1::Bucket::IamConfiguration GrpcClient::ToProto(
    BucketIamConfiguration const& rhs) {
  google::storage::v1::Bucket::IamConfiguration result;
  if (rhs.uniform_bucket_level_access.has_value()) {
    auto& ubla = *result.mutable_uniform_bucket_level_access();
    *ubla.mutable_locked_time() = ChronoTimepointToProtoTimestamp(
        rhs.uniform_bucket_level_access->locked_time);
    ubla.set_enabled(rhs.uniform_bucket_level_access->enabled);
  }
  return result;
}

BucketIamConfiguration GrpcClient::FromProto(
    google::storage::v1::Bucket::IamConfiguration const& rhs) {
  BucketIamConfiguration result;
  if (rhs.has_uniform_bucket_level_access()) {
    UniformBucketLevelAccess ubla;
    ubla.enabled = rhs.uniform_bucket_level_access().enabled();
    ubla.locked_time =
        AsChronoTimepoint(rhs.uniform_bucket_level_access().locked_time());
    result.uniform_bucket_level_access = std::move(ubla);
  }
  return result;
}

google::storage::v1::Bucket::Logging GrpcClient::ToProto(
    BucketLogging const& rhs) {
  google::storage::v1::Bucket::Logging result;
  result.set_log_bucket(rhs.log_bucket);
  result.set_log_object_prefix(rhs.log_object_prefix);
  return result;
}

BucketLogging GrpcClient::FromProto(
    google::storage::v1::Bucket::Logging const& rhs) {
  BucketLogging result;
  result.log_bucket = rhs.log_bucket();
  result.log_object_prefix = rhs.log_object_prefix();
  return result;
}

google::storage::v1::Bucket::RetentionPolicy GrpcClient::ToProto(
    BucketRetentionPolicy const& rhs) {
  google::storage::v1::Bucket::RetentionPolicy result;
  *result.mutable_effective_time() =
      ChronoTimepointToProtoTimestamp(rhs.effective_time);
  result.set_is_locked(rhs.is_locked);
  result.set_retention_period(rhs.retention_period.count());
  return result;
}

BucketRetentionPolicy GrpcClient::FromProto(
    google::storage::v1::Bucket::RetentionPolicy const& rhs) {
  BucketRetentionPolicy result;
  result.effective_time = AsChronoTimepoint(rhs.effective_time());
  result.is_locked = rhs.is_locked();
  result.retention_period = std::chrono::seconds(rhs.retention_period());
  return result;
}

google::storage::v1::Bucket::Versioning GrpcClient::ToProto(
    BucketVersioning const& rhs) {
  google::storage::v1::Bucket::Versioning result;
  result.set_enabled(rhs.enabled);
  return result;
}

BucketVersioning GrpcClient::FromProto(
    google::storage::v1::Bucket::Versioning const& rhs) {
  BucketVersioning result;
  result.enabled = rhs.enabled();
  return result;
}

google::storage::v1::Bucket::Website GrpcClient::ToProto(BucketWebsite rhs) {
  google::storage::v1::Bucket::Website result;
  result.set_main_page_suffix(std::move(rhs.main_page_suffix));
  result.set_not_found_page(std::move(rhs.not_found_page));
  return result;
}

BucketWebsite GrpcClient::FromProto(google::storage::v1::Bucket::Website rhs) {
  BucketWebsite result;
  result.main_page_suffix = std::move(*rhs.mutable_main_page_suffix());
  result.not_found_page = std::move(*rhs.mutable_not_found_page());
  return result;
}

google::storage::v1::CommonEnums::Projection GrpcClient::ToProto(
    Projection const& p) {
  if (p.value() == Projection::NoAcl().value()) {
    return google::storage::v1::CommonEnums::NO_ACL;
  }
  if (p.value() == Projection::Full().value()) {
    return google::storage::v1::CommonEnums::FULL;
  }
  GCP_LOG(ERROR) << "Unknown projection value " << p;
  return google::storage::v1::CommonEnums::FULL;
}

google::storage::v1::CommonEnums::PredefinedBucketAcl GrpcClient::ToProtoBucket(
    PredefinedAcl const& acl) {
  if (acl.value() == PredefinedAcl::AuthenticatedRead().value()) {
    return google::storage::v1::CommonEnums::BUCKET_ACL_AUTHENTICATED_READ;
  }
  if (acl.value() == PredefinedAcl::Private().value()) {
    return google::storage::v1::CommonEnums::BUCKET_ACL_PRIVATE;
  }
  if (acl.value() == PredefinedAcl::ProjectPrivate().value()) {
    return google::storage::v1::CommonEnums::BUCKET_ACL_PROJECT_PRIVATE;
  }
  if (acl.value() == PredefinedAcl::PublicRead().value()) {
    return google::storage::v1::CommonEnums::BUCKET_ACL_PUBLIC_READ;
  }
  if (acl.value() == PredefinedAcl::PublicReadWrite().value()) {
    return google::storage::v1::CommonEnums::BUCKET_ACL_PUBLIC_READ_WRITE;
  }
  GCP_LOG(ERROR) << "Unknown predefinedAcl value " << acl;
  return google::storage::v1::CommonEnums::PREDEFINED_BUCKET_ACL_UNSPECIFIED;
}

google::storage::v1::CommonEnums::PredefinedObjectAcl GrpcClient::ToProtoObject(
    PredefinedAcl const& acl) {
  if (acl.value() == PredefinedAcl::AuthenticatedRead().value()) {
    return google::storage::v1::CommonEnums::OBJECT_ACL_AUTHENTICATED_READ;
  }
  if (acl.value() == PredefinedAcl::Private().value()) {
    return google::storage::v1::CommonEnums::OBJECT_ACL_PRIVATE;
  }
  if (acl.value() == PredefinedAcl::ProjectPrivate().value()) {
    return google::storage::v1::CommonEnums::OBJECT_ACL_PROJECT_PRIVATE;
  }
  if (acl.value() == PredefinedAcl::PublicRead().value()) {
    return google::storage::v1::CommonEnums::OBJECT_ACL_PUBLIC_READ;
  }
  if (acl.value() == PredefinedAcl::PublicReadWrite().value()) {
    GCP_LOG(ERROR) << "Invalid predefinedAcl value " << acl;
    return google::storage::v1::CommonEnums::PREDEFINED_OBJECT_ACL_UNSPECIFIED;
  }
  GCP_LOG(ERROR) << "Unknown predefinedAcl value " << acl;
  return google::storage::v1::CommonEnums::PREDEFINED_OBJECT_ACL_UNSPECIFIED;
}

google::storage::v1::CommonEnums::PredefinedObjectAcl GrpcClient::ToProto(
    PredefinedDefaultObjectAcl const& acl) {
  if (acl.value() == PredefinedDefaultObjectAcl::AuthenticatedRead().value()) {
    return google::storage::v1::CommonEnums::OBJECT_ACL_AUTHENTICATED_READ;
  }
  if (acl.value() ==
      PredefinedDefaultObjectAcl::BucketOwnerFullControl().value()) {
    return google::storage::v1::CommonEnums::
        OBJECT_ACL_BUCKET_OWNER_FULL_CONTROL;
  }
  if (acl.value() == PredefinedDefaultObjectAcl::BucketOwnerRead().value()) {
    return google::storage::v1::CommonEnums::OBJECT_ACL_BUCKET_OWNER_READ;
  }
  if (acl.value() == PredefinedDefaultObjectAcl::Private().value()) {
    return google::storage::v1::CommonEnums::OBJECT_ACL_PRIVATE;
  }
  if (acl.value() == PredefinedDefaultObjectAcl::ProjectPrivate().value()) {
    return google::storage::v1::CommonEnums::OBJECT_ACL_PROJECT_PRIVATE;
  }
  if (acl.value() == PredefinedDefaultObjectAcl::PublicRead().value()) {
    return google::storage::v1::CommonEnums::OBJECT_ACL_PUBLIC_READ;
  }
  GCP_LOG(ERROR) << "Unknown predefinedAcl value " << acl;
  return google::storage::v1::CommonEnums::PREDEFINED_OBJECT_ACL_UNSPECIFIED;
}

google::storage::v1::Bucket GrpcClient::ToProto(
    BucketMetadata const& metadata) {
  google::storage::v1::Bucket bucket;
  bucket.set_name(metadata.name());
  // TODO(#4173) - convert the other fields.
  return bucket;
}

google::storage::v1::InsertBucketRequest GrpcClient::ToProto(
    CreateBucketRequest const& request) {
  google::storage::v1::InsertBucketRequest r;
  SetPredefinedAcl(r, request);
  SetPredefinedDefaultObjectAcl(r, request);
  r.set_project(request.project_id());
  SetProjection(r, request);
  *r.mutable_bucket() = ToProto(request.metadata());
  r.mutable_bucket()->set_name(request.metadata().name());
  SetCommonParameters(r, request);
  return r;
}

google::storage::v1::ListBucketsRequest GrpcClient::ToProto(
    ListBucketsRequest const& request) {
  google::storage::v1::ListBucketsRequest r;
  if (request.HasOption<MaxResults>()) {
    // The maximum page size is 1,000 anyway, if this cast
    // fails the request was invalid (but it can mask errors)
    r.set_max_results(static_cast<google::protobuf::int32>(
        request.GetOption<MaxResults>().value()));
  }
  r.set_page_token(request.page_token());
  r.set_project(request.project_id());
  if (request.HasOption<Prefix>()) {
    r.set_prefix(request.GetOption<Prefix>().value());
  }
  SetProjection(r, request);
  SetCommonParameters(r, request);
  return r;
}

google::storage::v1::GetBucketRequest GrpcClient::ToProto(
    GetBucketMetadataRequest const& request) {
  google::storage::v1::GetBucketRequest r;
  r.set_bucket(request.bucket_name());
  SetMetagenerationConditions(r, request);
  SetProjection(r, request);
  SetCommonParameters(r, request);

  return r;
}

google::storage::v1::DeleteBucketRequest GrpcClient::ToProto(
    DeleteBucketRequest const& request) {
  google::storage::v1::DeleteBucketRequest r;
  r.set_bucket(request.bucket_name());
  SetMetagenerationConditions(r, request);
  SetCommonParameters(r, request);
  return r;
}

google::storage::v1::InsertObjectRequest GrpcClient::ToProto(
    InsertObjectMediaRequest const& request) {
  google::storage::v1::InsertObjectRequest r;
  auto& object_spec = *r.mutable_insert_object_spec();
  auto& resource = *object_spec.mutable_resource();
  SetResourceOptions(resource, request);
  SetObjectMetadata(resource, request);
  SetPredefinedAcl(object_spec, request);
  SetGenerationConditions(object_spec, request);
  SetMetagenerationConditions(object_spec, request);
  SetProjection(object_spec, request);
  SetCommonObjectParameters(r, request);
  SetCommonParameters(r, request);

  resource.set_bucket(request.bucket_name());
  resource.set_name(request.object_name());
  r.set_write_offset(0);

  auto& checksums = *r.mutable_object_checksums();
  // TODO(#4156) - use the crc32c value in the request options.
  checksums.mutable_crc32c()->set_value(crc32c::Crc32c(request.contents()));
  // TODO(#4157) - use the MD5 hash value in the request options.
  checksums.set_md5_hash(MD5ToProto(ComputeMD5Hash(request.contents())));

  return r;
}

google::storage::v1::DeleteObjectRequest GrpcClient::ToProto(
    DeleteObjectRequest const& request) {
  google::storage::v1::DeleteObjectRequest r;
  r.set_bucket(request.bucket_name());
  r.set_object(request.object_name());
  if (request.HasOption<Generation>()) {
    r.set_generation(request.GetOption<Generation>().value());
  }
  SetGenerationConditions(r, request);
  SetMetagenerationConditions(r, request);
  SetCommonParameters(r, request);
  return r;
}

google::storage::v1::StartResumableWriteRequest GrpcClient::ToProto(
    ResumableUploadRequest const& request) {
  google::storage::v1::StartResumableWriteRequest result;

  auto& object_spec = *result.mutable_insert_object_spec();
  auto& resource = *object_spec.mutable_resource();
  SetResourceOptions(resource, request);
  SetObjectMetadata(resource, request);
  SetPredefinedAcl(object_spec, request);
  SetGenerationConditions(object_spec, request);
  SetMetagenerationConditions(object_spec, request);
  SetProjection(object_spec, request);
  SetCommonParameters(result, request);
  SetCommonObjectParameters(result, request);

  resource.set_bucket(request.bucket_name());
  resource.set_name(request.object_name());

  return result;
}

google::storage::v1::QueryWriteStatusRequest GrpcClient::ToProto(
    QueryResumableUploadRequest const& request) {
  google::storage::v1::QueryWriteStatusRequest r;
  r.set_upload_id(request.upload_session_url());
  return r;
}

google::storage::v1::GetObjectMediaRequest GrpcClient::ToProto(
    ReadObjectRangeRequest const& request) {
  google::storage::v1::GetObjectMediaRequest r;
  r.set_object(request.object_name());
  r.set_bucket(request.bucket_name());
  if (request.HasOption<Generation>()) {
    r.set_generation(request.GetOption<Generation>().value());
  }
  if (request.HasOption<ReadRange>()) {
    auto const range = request.GetOption<ReadRange>().value();
    r.set_read_offset(range.begin);
    r.set_read_limit(range.end - range.begin);
  }
  if (request.HasOption<ReadLast>()) {
    auto const offset = request.GetOption<ReadLast>().value();
    r.set_read_offset(-offset);
  }
  if (request.HasOption<ReadFromOffset>()) {
    auto const offset = request.GetOption<ReadFromOffset>().value();
    if (offset > r.read_offset()) {
      if (r.read_limit() > 0) {
        r.set_read_limit(offset - r.read_offset());
      }
      r.set_read_offset(offset);
    }
  }
  SetGenerationConditions(r, request);
  SetMetagenerationConditions(r, request);
  SetCommonObjectParameters(r, request);
  SetCommonParameters(r, request);

  return r;
}

std::string GrpcClient::Crc32cFromProto(
    google::protobuf::UInt32Value const& v) {
  auto endian_encoded = google::cloud::internal::EncodeBigEndian(v.value());
  return Base64Encode(endian_encoded);
}

std::uint32_t GrpcClient::Crc32cToProto(std::string const& v) {
  auto decoded = Base64Decode(v);
  return google::cloud::internal::DecodeBigEndian<std::uint32_t>(
             std::string(decoded.begin(), decoded.end()))
      .value();
}

std::string GrpcClient::MD5FromProto(std::string const& v) {
  if (v.empty()) return {};
  auto binary = internal::HexDecode(v);
  return internal::Base64Encode(binary);
}

std::string GrpcClient::MD5ToProto(std::string const& v) {
  if (v.empty()) return {};
  auto binary = internal::Base64Decode(v);
  return internal::HexEncode(binary);
}

}  // namespace internal
}  // namespace STORAGE_CLIENT_NS
}  // namespace storage
}  // namespace cloud
}  // namespace google
