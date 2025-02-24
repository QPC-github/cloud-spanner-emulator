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

#include <memory>
#include <string>
#include <vector>

#include "google/longrunning/operations.pb.h"
#include "google/protobuf/empty.pb.h"
#include "google/protobuf/timestamp.pb.h"
#include "google/spanner/admin/database/v1/spanner_database_admin.pb.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "absl/time/time.h"
#include "backend/database/database.h"
#include "backend/schema/ddl/operations.pb.h"
#include "backend/schema/parser/ddl_parser.h"
#include "backend/schema/printer/print_ddl.h"
#include "backend/schema/updater/schema_updater.h"
#include "common/errors.h"
#include "frontend/common/uris.h"
#include "frontend/converters/time.h"
#include "frontend/entities/database.h"
#include "frontend/server/handler.h"
#include "zetasql/base/status_macros.h"

namespace google {
namespace spanner {
namespace emulator {
namespace frontend {

namespace {

namespace database_api = ::google::spanner::admin::database::v1;
namespace operations_api = ::google::longrunning;
namespace protobuf_api = ::google::protobuf;

}  // namespace

// Lists all databases in an instance.
absl::Status ListDatabases(RequestContext* ctx,
                           const database_api::ListDatabasesRequest* request,
                           database_api::ListDatabasesResponse* response) {
  // Validate that the ListDatabases request is for a valid instance.
  ZETASQL_ASSIGN_OR_RETURN(std::shared_ptr<Instance> instance,
                   GetInstance(ctx, request->parent()));

  // Validate that the page_token provided is a valid database_uri.
  if (!request->page_token().empty()) {
    absl::string_view project_id, instance_id, database_id;
    ZETASQL_RETURN_IF_ERROR(ParseDatabaseUri(request->page_token(), &project_id,
                                     &instance_id, &database_id));
  }

  ZETASQL_ASSIGN_OR_RETURN(
      std::vector<std::shared_ptr<Database>> databases,
      ctx->env()->database_manager()->ListDatabases(request->parent()));

  int32_t page_size = request->page_size();
  static const int32_t kMaxPageSize = 1000;
  if (page_size <= 0 || page_size > kMaxPageSize) {
    page_size = kMaxPageSize;
  }

  // Databases returned from database manager are sorted by database_uri and
  // thus we use database uri of first database in next page as next_page_token.
  for (const auto& database : databases) {
    if (response->databases_size() >= page_size) {
      response->set_next_page_token(database->database_uri());
      break;
    }
    if (database->database_uri() >= request->page_token()) {
      ZETASQL_RETURN_IF_ERROR(database->ToProto(response->add_databases()));
    }
  }
  return absl::OkStatus();
}
REGISTER_GRPC_HANDLER(DatabaseAdmin, ListDatabases);

// Creates a new database within an instance.
absl::Status CreateDatabase(RequestContext* ctx,
                            const database_api::CreateDatabaseRequest* request,
                            operations_api::Operation* response) {
  // Validate the request.
  ZETASQL_ASSIGN_OR_RETURN(std::shared_ptr<Instance> instance,
                   GetInstance(ctx, request->parent()));
  if (request->create_statement().empty()) {
    return error::CreateDatabaseMissingCreateStatement();
  }

  // Extract database name from create statement.
  ZETASQL_ASSIGN_OR_RETURN(
      backend::ddl::CreateDatabase stmt,
      backend::ddl::ParseCreateDatabase(request->create_statement()));
  std::string database_name = stmt.database_name();

  // Validate database name.
  ZETASQL_RETURN_IF_ERROR(ValidateDatabaseId(database_name));

  // Create the database.
  std::string database_uri = MakeDatabaseUri(request->parent(), database_name);
  std::vector<std::string> create_statements;
  for (const std::string& statement : request->extra_statements()) {
    create_statements.push_back(statement);
  }
  ZETASQL_ASSIGN_OR_RETURN(std::shared_ptr<Database> database,
                   ctx->env()->database_manager()->CreateDatabase(
                       database_uri, backend::SchemaChangeOperation{
                                         .statements = create_statements,
                                     }));

  // Create an operation tracking the database creation.
  ZETASQL_ASSIGN_OR_RETURN(std::shared_ptr<Operation> operation,
                   ctx->env()->operation_manager()->CreateOperation(
                       database_uri, OperationManager::kAutoGeneratedId));

  database_api::CreateDatabaseMetadata metadata;
  metadata.set_database(database_uri);
  operation->SetMetadata(metadata);
  database_api::Database response_database;
  ZETASQL_RETURN_IF_ERROR(database->ToProto(&response_database));
  operation->SetResponse(response_database);
  operation->ToProto(response);

  return absl::OkStatus();
}
REGISTER_GRPC_HANDLER(DatabaseAdmin, CreateDatabase);

// Gets the current state of a database.
absl::Status GetDatabase(RequestContext* ctx,
                         const database_api::GetDatabaseRequest* request,
                         database_api::Database* response) {
  ZETASQL_ASSIGN_OR_RETURN(std::shared_ptr<Database> database,
                   GetDatabase(ctx, request->name()));
  return database->ToProto(response);
}
REGISTER_GRPC_HANDLER(DatabaseAdmin, GetDatabase);

// Updates the schema of a database.
absl::Status UpdateDatabaseDdl(
    RequestContext* ctx, const database_api::UpdateDatabaseDdlRequest* request,
    operations_api::Operation* response) {
  // Validate request URI.
  absl::string_view project_id, instance_id, database_id;
  ZETASQL_RETURN_IF_ERROR(ParseDatabaseUri(request->database(), &project_id,
                                   &instance_id, &database_id));

  // Check for request replay.
  if (!request->operation_id().empty()) {
    if (!IsValidOperationId(request->operation_id())) {
      return error::InvalidOperationId(request->operation_id());
    }
    const std::string operation_uri =
        MakeOperationUri(request->database(), request->operation_id());
    auto maybe_operation =
        ctx->env()->operation_manager()->GetOperation(operation_uri);
    if (maybe_operation.ok()) {
      return error::OperationAlreadyExists(operation_uri);
    }
  }

  // Lookup the database by URI.
  ZETASQL_ASSIGN_OR_RETURN(std::shared_ptr<Database> database,
                   GetDatabase(ctx, request->database()));

  std::vector<std::string> statements;
  for (const std::string& statement : request->statements()) {
    statements.push_back(statement);
  }

  backend::Database* backend_database = database->backend();
  int num_succesful_statements;
  absl::Time commit_timestamp;
  absl::Status backfill_status;
  ZETASQL_RETURN_IF_ERROR(backend_database->UpdateSchema(
      backend::SchemaChangeOperation{
          .statements = statements,
      },
      &num_succesful_statements, &commit_timestamp, &backfill_status));

  // Populate ResultSet metadata.
  // For simplicity in emulator, we have implemented the schema updates in such
  // a way that all the statements in update ddl execute at the same commit
  // timestamp.
  database_api::UpdateDatabaseDdlMetadata update_md;
  update_md.set_database(request->database());
  for (const std::string& statement : statements) {
    update_md.add_statements(statement);
  }

  // Only the timestamps of the successful statements are reported.
  for (int i = 0; i < num_succesful_statements; ++i) {
    ZETASQL_ASSIGN_OR_RETURN(*update_md.add_commit_timestamps(),
                     TimestampToProto(commit_timestamp));
  }

  // Create operation to be returned as part of the response.
  // A user-supplied operation_id would have already been validated above.
  ZETASQL_ASSIGN_OR_RETURN(std::shared_ptr<Operation> operation,
                   ctx->env()->operation_manager()->CreateOperation(
                       request->database(), request->operation_id()));
  operation->SetMetadata(update_md);
  if (backfill_status.ok()) {
    operation->SetResponse(protobuf_api::Empty());
  } else {
    operation->SetError(backfill_status);
  }
  operation->ToProto(response);

  return absl::OkStatus();
}
REGISTER_GRPC_HANDLER(DatabaseAdmin, UpdateDatabaseDdl);

// Drops (aka deletes) a database.
absl::Status DropDatabase(RequestContext* ctx,
                          const database_api::DropDatabaseRequest* request,
                          protobuf_api::Empty* response) {
  // Validate the request.
  absl::string_view project_id, instance_id, database_id;
  ZETASQL_RETURN_IF_ERROR(ParseDatabaseUri(request->database(), &project_id,
                                   &instance_id, &database_id));
  ZETASQL_ASSIGN_OR_RETURN(std::shared_ptr<Instance> instance,
                   GetInstance(ctx, MakeInstanceUri(project_id, instance_id)));

  // Clean up resources associated with the database.
  auto maybe_database =
      ctx->env()->database_manager()->GetDatabase(request->database());
  if (maybe_database.ok()) {
    ZETASQL_ASSIGN_OR_RETURN(
        std::vector<std::shared_ptr<Session>> sessions,
        ctx->env()->session_manager()->ListSessions(request->database()));
    for (const auto& session : sessions) {
      ZETASQL_RETURN_IF_ERROR(
          ctx->env()->session_manager()->DeleteSession(session->session_uri()));
    }
  }

  // Clean up the database.
  return ctx->env()->database_manager()->DeleteDatabase(request->database());
}
REGISTER_GRPC_HANDLER(DatabaseAdmin, DropDatabase);

// Returns the schema of a database as a list of formatted DDL statements.
absl::Status GetDatabaseDdl(RequestContext* ctx,
                            const database_api::GetDatabaseDdlRequest* request,
                            database_api::GetDatabaseDdlResponse* response) {
  ZETASQL_ASSIGN_OR_RETURN(std::shared_ptr<Database> database,
                   GetDatabase(ctx, request->database()));

  for (const auto& statement :
       backend::PrintDDLStatements(database->backend()->GetLatestSchema())) {
    response->add_statements(statement);
  }
  return absl::OkStatus();
}
REGISTER_GRPC_HANDLER(DatabaseAdmin, GetDatabaseDdl);

}  // namespace frontend
}  // namespace emulator
}  // namespace spanner
}  // namespace google
