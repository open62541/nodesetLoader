/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 *    Copyright 2019 (c) Matthias Konnerth
 */

#ifndef NODESET_H
#define NODESET_H
#include <CharAllocator.h>
#include <NodesetLoader/NodesetLoader.h>
#include <stdbool.h>
#include <stddef.h>

struct Nodeset;
typedef struct Nodeset Nodeset;
struct Alias;
struct TParserCtx;
typedef struct TParserCtx TParserCtx;

struct NamespaceList;

struct NodeContainer;
struct AliasList;
struct SortContext;
struct Nodeset
{
    CharArenaAllocator *charArena;
    struct AliasList *aliasList;
    struct NodeContainer *nodes[NODECLASS_COUNT];
    struct NamespaceList *namespaces;
    struct SortContext *sortCtx;
    BiDirectionalReference *hasEncodingRefs;
    NodesetLoader_Logger* logger;
    struct NodeContainer *nodesWithUnknownRefs;
    struct NodeContainer *refTypesWithUnknownRefs;
    RefService* refService;
};

Nodeset *Nodeset_new(addNamespaceCb nsCallback, NodesetLoader_Logger* logger, RefService* refService);
void Nodeset_cleanup(Nodeset *nodeset);
bool Nodeset_sort(Nodeset *nodeset);
TNode *Nodeset_newNode(Nodeset *nodeset, NL_NodeClass nodeClass,
                       int attributeSize, const char **attributes);
void Nodeset_newNodeFinish(Nodeset *nodeset, TNode *node);
Reference *Nodeset_newReference(Nodeset *nodeset, TNode *node,
                                int attributeSize, const char **attributes);
void Nodeset_newReferenceFinish(Nodeset *nodeset, Reference *ref, TNode *node,
                                char *targetId);
struct Alias *Nodeset_newAlias(Nodeset *nodeset, int attributeSize,
                               const char **attribute);
void Nodeset_newAliasFinish(Nodeset *nodeset, struct Alias *alias,
                            char *idString);
void Nodeset_newNamespaceFinish(Nodeset *nodeset, void *userContext,
                                char *namespaceUri);
void Nodeset_addDataTypeDefinition(Nodeset *nodeset, TNode *node, int attributeSize,
                              const char **attributes);
void Nodeset_addDataTypeField(Nodeset *nodeset, TNode *node, int attributeSize,
                              const char **attributes);
void Nodeset_setDisplayName(Nodeset *nodeset, TNode *node, int attributeSize,
                            const char **attributes);
void Nodeset_DisplayNameFinish(const Nodeset *nodeset, TNode *node, char *text);
void Nodeset_setDescription(Nodeset *nodeset, TNode *node, int attributeSize,
                            const char **attributes);
void Nodeset_DescriptionFinish(const Nodeset *nodeset, TNode *node, char *text);
void Nodeset_setInverseName(Nodeset *nodeset, TNode *node, int attributeSize,
                            const char **attributes);
void Nodeset_InverseNameFinish(const Nodeset *nodeset, TNode *node, char *text);
const BiDirectionalReference *
Nodeset_getBiDirectionalRefs(const Nodeset *nodeset);
size_t Nodeset_forEachNode(Nodeset *nodeset, NL_NodeClass nodeClass,
                           void *context, NodesetLoader_forEachNode_Func fn);
#endif
