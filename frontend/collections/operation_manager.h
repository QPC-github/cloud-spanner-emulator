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

#ifndef THIRD_PARTY_CLOUD_SPANNER_EMULATOR_FRONTEND_COLLECTIONS_OPERATION_MANAGER_H_
#define THIRD_PARTY_CLOUD_SPANNER_EMULATOR_FRONTEND_COLLECTIONS_OPERATION_MANAGER_H_

#include <map>
#include <memory>
#include <string>

#include "absl/base/thread_annotations.h"
#include "absl/status/status.h"
#include "zetasql/base/statusor.h"
#include "absl/synchronization/mutex.h"
#include "frontend/entities/operation.h"
#include "absl/status/status.h"

namespace google {
namespace spanner {
namespace emulator {
namespace frontend {

// OperationManager manages the set of long running operations in the emulator.
//
// The emulator does not actually have long running operations - all operations
// complete immediately. However, these operations are still recorded and are
// accessible via the longrunning operations api.
//
// Cloud Spanner currently has the following long running operations:
// - Create an instance
// - Update an instance
// - Create a database
// - Update a database
//
// The emulator implementation of these operations executes the operation
// synchronously, but returns an incomplete operation response. The completed
// operation is registered with this OperationManager. This ensures that
// applications developed against the emulator don't assume that the operations
// finish immediately and have to query the operations api to get the status of
// the operation.
//
// The interface below does not implement the Cancel and Wait operations. Cancel
// returns success at the handler level as there is nothing to cancel. Wait is
// not implemented by Cloud Spanner, so we don't need to implement it here.
//
// For more details on the long running operations api, see
//     https://cloud.google.com/spanner/docs/reference/rpc/google.longrunning
class OperationManager {
 public:
  // A constant indicating that the operation id should be auto generated.
  static const char kAutoGeneratedId[];

  // Creates an operation. Some operations (like update database) allow the
  // user to specify the operation id. If the user specifies an operation id,
  // it is used as-is, otherwise a system generated operation id is used.
  // System generated ids always start with "_auto".
  zetasql_base::StatusOr<std::shared_ptr<Operation>> CreateOperation(
      const std::string& resource_uri, const std::string& operation_id)
      ABSL_LOCKS_EXCLUDED(mu_);

  // Gets the operation with the specified URI, or returns NOT_FOUND if no such
  // operation is registered with the manager.
  zetasql_base::StatusOr<std::shared_ptr<Operation>> GetOperation(
      const std::string& operation_uri) ABSL_LOCKS_EXCLUDED(mu_);

  // Deletes the operation with the specified URI. Delete is idempotent - OK is
  // returned even if the operation does not exist.
  absl::Status DeleteOperation(const std::string& operation_uri)
      ABSL_LOCKS_EXCLUDED(mu_);

  // Lists all the operations registered with the operation manager.
  zetasql_base::StatusOr<std::vector<std::shared_ptr<Operation>>> ListOperations(
      const std::string& resource_uri) ABSL_LOCKS_EXCLUDED(mu_);

 private:
  // Mutex to guard state below.
  absl::Mutex mu_;

  // Counter for the system assigned operation id.
  int next_operation_id_ ABSL_GUARDED_BY(mu_) = 0;

  // Map from operation URI to actual operation.
  std::map<std::string, std::shared_ptr<Operation>> operations_map_
      ABSL_GUARDED_BY(mu_);
};

}  // namespace frontend
}  // namespace emulator
}  // namespace spanner
}  // namespace google

#endif  // THIRD_PARTY_CLOUD_SPANNER_EMULATOR_FRONTEND_COLLECTIONS_OPERATION_MANAGER_H_
