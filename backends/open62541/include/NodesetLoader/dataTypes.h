/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 *    Copyright 2020 (c) Matthias Konnerth
 */

#ifndef __NODESETLOADER_BACKEND_OPEN62541_DATATYPES_H__
#define __NODESETLOADER_BACKEND_OPEN62541_DATATYPES_H__
#include <open62541/types.h>

#if defined(_WIN32)
#ifdef __GNUC__
#define LOADER_EXPORT __attribute__((dllexport))
#else
#define LOADER_EXPORT __declspec(dllexport)
#endif
#else /* non win32 */
#if __GNUC__ || __clang__
#define LOADER_EXPORT __attribute__((visibility("default")))
#endif
#endif
#ifndef LOADER_EXPORT
#define LOADER_EXPORT /* fallback to default */
#endif

#ifdef __cplusplus
extern "C" {
#endif

struct UA_Server;
LOADER_EXPORT const struct UA_DataType *
NodesetLoader_getCustomDataType(struct UA_Server *server, const UA_NodeId *typeId);

#ifdef __cplusplus
}
#endif

#endif
