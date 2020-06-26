#include <check.h>
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include <open62541/server.h>
#include <open62541/server_config_default.h>
#include <open62541/types.h>

#include "check.h"
#include "unistd.h"

#include "testHelper.h"
#include <NodesetLoader/backendOpen62541.h>

UA_Server *server;
char *nodesetPath = NULL;

static void setup(void)
{
    printf("path to testnodesets %s\n", nodesetPath);
    server = UA_Server_new();
    UA_ServerConfig *config = UA_Server_getConfig(server);
    UA_ServerConfig_setDefault(config);
}

static void teardown(void)
{
    UA_Server_run_shutdown(server);
    cleanupCustomTypes(UA_Server_getConfig(server)->customDataTypes);
    UA_Server_delete(server);
}

START_TEST(import_ValueRank)
{
    ck_assert(NodesetLoader_loadFile(server, nodesetPath, NULL));

    UA_Variant var;
    UA_Variant_init(&var);
    ck_assert(
        UA_STATUSCODE_GOOD ==
            UA_Server_readValue(server, UA_NODEID_NUMERIC(2, 6002), &var));
    ck_assert(1==*(int*)var.data);
    UA_Variant_clear(&var);
    ck_assert(
        UA_STATUSCODE_GOOD ==
            UA_Server_readValue(server, UA_NODEID_NUMERIC(2, 6003), &var));
    ck_assert(13 == ((int *)var.data)[1]);
    UA_Variant_clear(&var);
    ck_assert(
        UA_STATUSCODE_GOOD ==
            UA_Server_readValue(server, UA_NODEID_NUMERIC(2, 6004), &var));
    ck_assert(300 == ((int *)var.data)[2]);
    UA_Variant_clear(&var);
    ck_assert(UA_STATUSCODE_GOOD ==
              UA_Server_readValue(server, UA_NODEID_NUMERIC(2, 6005), &var));
    ck_assert(4 == ((int *)var.data)[4]);
    UA_Variant_clear(&var);
    //should this really work?
    //value rank = 1, no arrayDimensions and scalar value
    ck_assert(UA_STATUSCODE_GOOD ==
              UA_Server_readValue(server, UA_NODEID_NUMERIC(2, 6006), &var));
    ck_assert(1 == *((int *)var.data));
    UA_Variant_clear(&var);
}
END_TEST

static Suite *testSuite_Client(void)
{
    Suite *s = suite_create("server nodeset import");
    TCase *tc_server = tcase_create("server nodeset import");
    tcase_add_unchecked_fixture(tc_server, setup, teardown);
    tcase_add_test(tc_server, import_ValueRank);
    suite_add_tcase(s, tc_server);
    return s;
}

int main(int argc, char *argv[])
{
    printf("%s", argv[0]);
    if (!(argc > 1))
        return 1;
    nodesetPath = argv[1];
    Suite *s = testSuite_Client();
    SRunner *sr = srunner_create(s);
    srunner_set_fork_status(sr, CK_NOFORK);
    srunner_run_all(sr, CK_NORMAL);
    int number_failed = srunner_ntests_failed(sr);
    srunner_free(sr);
    return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
