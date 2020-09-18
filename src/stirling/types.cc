#include "src/stirling/types.h"

#include <google/protobuf/repeated_field.h>

namespace pl {
namespace stirling {

using ::pl::stirling::dynamic_tracing::ir::logical::BPFTrace;
using ::pl::stirling::dynamic_tracing::ir::physical::Struct;

stirlingpb::Element DataElement::ToProto() const {
  stirlingpb::Element element_proto;
  element_proto.set_name(std::string(name_));
  element_proto.set_type(type_);
  element_proto.set_ptype(ptype_);
  element_proto.set_stype(stype_);
  element_proto.set_desc(std::string(desc_));
  if (decoder_ != nullptr) {
    google::protobuf::Map<int64_t, std::string>* decoder = element_proto.mutable_decoder();
    *decoder = google::protobuf::Map<int64_t, std::string>(decoder_->begin(), decoder_->end());
  }
  return element_proto;
}

stirlingpb::TableSchema DataTableSchema::ToProto() const {
  stirlingpb::TableSchema table_schema_proto;

  // Populate the proto with Elements.
  for (auto element : elements_) {
    stirlingpb::Element* element_proto_ptr = table_schema_proto.add_elements();
    element_proto_ptr->MergeFrom(element.ToProto());
  }
  table_schema_proto.set_name(std::string(name_));
  table_schema_proto.set_tabletized(tabletized_);
  table_schema_proto.set_tabletization_key(tabletization_key_);

  return table_schema_proto;
}

namespace {

std::vector<DataElement> CreateDataElements(
    const google::protobuf::RepeatedPtrField<::pl::stirling::dynamic_tracing::ir::physical::Field>&
        repeated_fields) {
  using dynamic_tracing::ir::shared::ScalarType;

  // clang-format off
  static const std::map<ScalarType, types::DataType> kTypeMap = {
          {ScalarType::BOOL, types::DataType::BOOLEAN},

          {ScalarType::SHORT, types::DataType::INT64},
          {ScalarType::USHORT, types::DataType::INT64},
          {ScalarType::INT, types::DataType::INT64},
          {ScalarType::UINT, types::DataType::INT64},
          {ScalarType::LONG, types::DataType::INT64},
          {ScalarType::ULONG, types::DataType::INT64},
          {ScalarType::LONGLONG, types::DataType::INT64},
          {ScalarType::ULONGLONG, types::DataType::INT64},

          {ScalarType::INT8, types::DataType::INT64},
          {ScalarType::INT16, types::DataType::INT64},
          {ScalarType::INT32, types::DataType::INT64},
          {ScalarType::INT64, types::DataType::INT64},
          {ScalarType::UINT8, types::DataType::INT64},
          {ScalarType::UINT16, types::DataType::INT64},
          {ScalarType::UINT32, types::DataType::INT64},
          {ScalarType::UINT64, types::DataType::INT64},

          {ScalarType::FLOAT, types::DataType::FLOAT64},
          {ScalarType::DOUBLE, types::DataType::FLOAT64},

          {ScalarType::STRING, types::DataType::STRING},

          // Will be converted to a hex string.
          {ScalarType::BYTE_ARRAY, types::DataType::STRING},

          // Will be converted to JSON string.
          {ScalarType::STRUCT_BLOB, types::DataType::STRING},
  };
  // clang-format on

  std::vector<DataElement> elements;

  // Insert the special upid column.
  // TODO(yzhao): Make sure to have a structured way to let the IR to express the upid.
  elements.emplace_back("upid", "upid", types::DataType::UINT128, types::SemanticType::ST_NONE,
                        types::PatternType::UNSPECIFIED);

  for (int i = 2; i < repeated_fields.size(); ++i) {
    const auto& field = repeated_fields[i];

    types::DataType data_type;

    auto iter = kTypeMap.find(field.type());
    if (iter == kTypeMap.end()) {
      LOG(DFATAL) << absl::Substitute("Unrecognized base type: $0", field.type());
      data_type = types::DataType::DATA_TYPE_UNKNOWN;
    } else {
      data_type = iter->second;
    }

    // TODO(oazizi): This is hacky. Fix.
    if (field.name() == "time_") {
      data_type = types::DataType::TIME64NS;
    }

    // TODO(oazizi): See if we need to find a way to define SemanticTypes and PatternTypes.
    elements.emplace_back(field.name(), field.name(), data_type, types::SemanticType::ST_NONE,
                          types::PatternType::UNSPECIFIED);
  }

  return elements;
}

std::vector<DataElement> CreateDataElements(
    const google::protobuf::RepeatedPtrField<
        ::pl::stirling::dynamic_tracing::ir::logical::BPFTrace::Output>& repeated_fields) {
  std::vector<DataElement> elements;
  for (const auto& field : repeated_fields) {
    elements.emplace_back(field.name(), field.name(), field.type(), types::SemanticType::ST_NONE,
                          types::PatternType::UNSPECIFIED);
  }

  return elements;
}

}  // namespace

std::unique_ptr<DynamicDataTableSchema> DynamicDataTableSchema::Create(
    const dynamic_tracing::BCCProgram::PerfBufferSpec& output_spec) {
  auto output_struct_ptr = std::make_unique<Struct>(output_spec.output);
  std::unique_ptr<BPFTrace> bpftrace_ptr;
  return absl::WrapUnique(
      new DynamicDataTableSchema(output_spec.name, CreateDataElements(output_struct_ptr->fields()),
                                 std::move(output_struct_ptr), std::move(bpftrace_ptr)));
}

std::unique_ptr<DynamicDataTableSchema> DynamicDataTableSchema::Create(
    std::string_view output_name, const dynamic_tracing::ir::logical::BPFTrace& bpftrace) {
  std::unique_ptr<Struct> output_struct_ptr;
  auto bpftrace_ptr = std::make_unique<BPFTrace>(bpftrace);
  return absl::WrapUnique(
      new DynamicDataTableSchema(output_name, CreateDataElements(bpftrace_ptr->outputs()),
                                 std::move(output_struct_ptr), std::move(bpftrace_ptr)));
}

}  // namespace stirling
}  // namespace pl
