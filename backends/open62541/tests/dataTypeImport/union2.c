/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include <open62541/server.h>
#include <open62541/server_config_default.h>
#include <open62541/types.h>

#include "check.h"
#include "unistd.h"

#include "../testHelper.h"
#include "open62541/types_union2_generated.h"
#include <NodesetLoader/backendOpen62541.h>
#include <NodesetLoader/dataTypes.h>

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
    const UA_DataTypeArray *customTypes =
        UA_Server_getConfig(server)->customDataTypes;
    UA_Server_delete(server);
    cleanupCustomTypes(customTypes);
}

START_TEST(compareUnion)
{
    ck_assert(NodesetLoader_loadFile(server, nodesetPath, NULL));

    UA_ServerConfig *config = UA_Server_getConfig(server);
    ck_assert(config->customDataTypes);

    ck_assert(config->customDataTypes->typesSize == UA_TYPES_UNION2_COUNT);

    for (const UA_DataType *generatedType = UA_TYPES_UNION2;
         generatedType != UA_TYPES_UNION2 + UA_TYPES_UNION2_COUNT;
         generatedType++)
    {
        const UA_DataType *importedType =
            NodesetLoader_getCustomDataType(server, &generatedType->typeId);
        ck_assert(importedType != NULL);
        typesAreMatching(generatedType, importedType, &UA_TYPES_UNION2[0],
                         config->customDataTypes->types);
    }
}
END_TEST

START_TEST(readUnionValue)
{
    UA_Variant var;
    UA_Variant_init(&var);

    UA_StatusCode status =
        UA_Server_readValue(server, UA_NODEID_NUMERIC(2, 6018), &var);
    ck_assert(status == UA_STATUSCODE_GOOD);

    UA_MyUnion u;
    u = *(UA_MyUnion*)var.data;

    ck_assert(u.switchField==UA_MYUNIONSWITCH_X);
    ck_assert_int_eq(u.fields.x, 70000);

    UA_Variant_clear(&var);

    status =
        UA_Server_readValue(server, UA_NODEID_NUMERIC(2, 6021), &var);
    ck_assert(status == UA_STATUSCODE_GOOD);

    u = *(UA_MyUnion *)var.data;

    ck_assert(u.switchField == UA_MYUNIONSWITCH_Y);
    ck_assert_int_eq(u.fields.y, -1000);
    UA_Variant_clear(&var);
}
END_TEST

static Suite *testSuite_Client(void)
{
    Suite *s = suite_create("datatype Import");
    TCase *tc_server = tcase_create("server nodeset import");
    tcase_add_unchecked_fixture(tc_server, setup, teardown);
    tcase_add_test(tc_server, compareUnion);
    tcase_add_test(tc_server, readUnionValue);
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
