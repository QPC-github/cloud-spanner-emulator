//
// Copyright 2020 Google LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//

#ifndef THIRD_PARTY_CLOUD_SPANNER_EMULATOR_BACKEND_SCHEMA_CATALOG_FOREIGN_KEY_H_
#define THIRD_PARTY_CLOUD_SPANNER_EMULATOR_BACKEND_SCHEMA_CATALOG_FOREIGN_KEY_H_

#include <vector>

#include "absl/status/status.h"
#include "backend/schema/graph/schema_node.h"
#include "absl/status/status.h"

namespace google {
namespace spanner {
namespace emulator {
namespace backend {

class Column;
class Index;
class Table;
class SchemaValidationContext;

// Foreign key relationship between two tables.
class ForeignKey : public SchemaNode {
 public:
  // Returns the name of this foreign key. Constraint names are optional for
  // foreign keys. A name is generated by Spanner for unnamed foreign keys.
  const std::string& Name() const {
    return constraint_name_.empty() ? generated_name_ : constraint_name_;
  }

  // Returns the constraint name if any; empty if this foreign key is unnamed.
  const std::string& constraint_name() const { return constraint_name_; }
  // Returns the generated name if any; empty if this foreign key is named.
  const std::string& generated_name() const { return generated_name_; }

  // Returns the table that this foreign key is defined on.
  const Table* referencing_table() const { return referencing_table_; }
  // Returns the referencing table's columns.
  absl::Span<const Column* const> const referencing_columns() const {
    return referencing_columns_;
  }
  // Returns the managed referencing backing index if any. Returns nullptr if
  // the referencing table's primary key is used.
  const Index* referencing_index() const { return referencing_index_; }
  // Returns the referencing index data table if one exists. Returns the
  // referencing table if the primary key is used instead.
  const Table* referencing_data_table() const;

  // Returns the table that this foreign key references.
  const Table* referenced_table() const { return referenced_table_; }
  // Returns the referenced table's columns.
  absl::Span<const Column* const> const referenced_columns() const {
    return referenced_columns_;
  }
  // Returns the managed referenced backing index if any. Returns nullptr if
  // the referenced table's primary key is used.
  const Index* referenced_index() const { return referenced_index_; }
  // Returns the referenced index data table if one exists. Returns the
  // referenced table if the primary key is used instead.
  const Table* referenced_data_table() const;

  // SchemaNode interface implementation.
  // ------------------------------------

  std::optional<SchemaNameInfo> GetSchemaNameInfo() const override {
    return SchemaNameInfo{
        .name = Name(), .kind = "Foreign Key", .global = true};
  }

  absl::Status Validate(SchemaValidationContext* context) const override;

  absl::Status ValidateUpdate(const SchemaNode* orig,
                              SchemaValidationContext* context) const override;

  std::string DebugString() const override;

  class Builder;
  class Editor;

 private:
  friend class ForeignKeyValidator;

  using ValidationFn =
      std::function<absl::Status(const ForeignKey*, SchemaValidationContext*)>;
  using UpdateValidationFn = std::function<absl::Status(
      const ForeignKey*, const ForeignKey*, SchemaValidationContext*)>;

  // Constructors are private and only friend classes are able to build.
  ForeignKey(const ValidationFn& validate,
             const UpdateValidationFn& validate_update)
      : validate_(validate), validate_update_(validate_update) {}
  ForeignKey(const ForeignKey&) = default;

  std::unique_ptr<SchemaNode> ShallowClone() const override {
    return absl::WrapUnique(new ForeignKey(*this));
  }

  absl::Status DeepClone(SchemaGraphEditor* editor,
                         const SchemaNode* orig) override;

  // Validation delegates.
  const ValidationFn validate_;
  const UpdateValidationFn validate_update_;

  // Constraint name if any; empty if unnamed.
  std::string constraint_name_;
  // Genenerated name for an unnamed foreign key; empty if named.
  std::string generated_name_;

  // Table that this foreign key is define on.
  const Table* referencing_table_ = nullptr;
  // Referencing table's columns.
  std::vector<const Column*> referencing_columns_;
  // Referencing managed backing index. Null if the primary key is used.
  const Index* referencing_index_ = nullptr;

  // Table that this foreign key references.
  const Table* referenced_table_ = nullptr;
  // Referenced table's columns.
  std::vector<const Column*> referenced_columns_;
  // Referenced managed backing index. Null if the primary key is used.
  const Index* referenced_index_ = nullptr;
};

}  // namespace backend
}  // namespace emulator
}  // namespace spanner
}  // namespace google

#endif  // THIRD_PARTY_CLOUD_SPANNER_EMULATOR_BACKEND_SCHEMA_CATALOG_FOREIGN_KEY_H_
