import { Alert, AlertDescription, AlertIcon, Stack } from "@chakra-ui/react";
import React, { useEffect } from "react";
import { ReactTable } from "../../../components/table/ReactTable";
import { TableControl } from "../../../components/table/TableControl";
import {
  CollectionsPermissionsTable,
  DatabaseTableType
} from "./CollectionsPermissionsTable";
import { SystemDatabaseWarningModal } from "./SystemDatabaseWarningModal";
import { useUsername } from "./useFetchDatabasePermissions";
import {
  TABLE_COLUMNS,
  UserPermissionsContextProvider,
  useUserPermissionsContext
} from "./UserPermissionsContext";

export const UserPermissionsTable = () => {
  return (
    <UserPermissionsContextProvider>
      <UserPermissionsTableInner />
    </UserPermissionsContextProvider>
  );
};

const UserPermissionsTableInner = () => {
  const { tableInstance } = useUserPermissionsContext();
  const { username } = useUsername();
  useEffect(() => {
    window.arangoHelper.buildUserSubNav(username, "Permissions");
  }, [username]);

  const { isManagedUser, isRootUser } = tableInstance.options.meta as any;

  return (
    <Stack padding="4">
      <SystemDatabaseWarningModal />
      <TableControl<DatabaseTableType>
        columns={TABLE_COLUMNS}
        table={tableInstance}
        showColumnSelector={false}
      />
      {isManagedUser ? (
        <Alert status="error">
          <AlertIcon />
          <AlertDescription>
            This user's permissions are managed by ArangoGraph and cannot be
            modified in this deployment.
          </AlertDescription>
        </Alert>
      ) : null}
      <ReactTable<DatabaseTableType>
        tableWidth="auto"
        table={tableInstance}
        layout="fixed"
        emptyStateMessage="No database permissions found"
        getCellProps={cell => {
          if (cell.column.id === "databaseName") {
            return {
              padding: "0",
              height: "1px" // hack to make div take full height
            };
          }
        }}
        renderSubComponent={row => {
          return (
            <CollectionsPermissionsTable
              row={row}
              isManagedUser={isManagedUser}
              isRootUser={isRootUser}
            />
          );
        }}
      />
    </Stack>
  );
};
