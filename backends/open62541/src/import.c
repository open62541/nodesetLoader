/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 *    Copyright 2020 (c) Matthias Konnerth
 */

#include "DataTypeImporter.h"
#include "Value.h"
#include "conversion.h"
#include <NodesetLoader/NodesetLoader.h>
#include <NodesetLoader/backendOpen62541.h>
#include <NodesetLoader/dataTypes.h>
#include <RefServiceImpl.h>
#include <assert.h>
#include <open62541/server.h>
#include <open62541/server.h>

int NodesetLoader_BackendOpen62541_addNamespace(void *userContext, const char *namespaceUri);

static UA_NodeId getParentDataType(UA_Server *server, const UA_NodeId id)
{
    UA_BrowseDescription bd;
    UA_BrowseDescription_init(&bd);
    bd.nodeId = id;
    bd.browseDirection = UA_BROWSEDIRECTION_INVERSE;
    bd.nodeClassMask = UA_NODECLASS_DATATYPE;

    UA_BrowseResult br = UA_Server_browse(server, 10, &bd);
    if (br.statusCode != UA_STATUSCODE_GOOD || br.referencesSize != 1)
    {
        return UA_NODEID_NULL;
    }
    UA_NodeId parentId = br.references[0].nodeId.nodeId;
    UA_BrowseResult_clear(&br);
    return parentId;
}

static bool isKnownParent(const UA_NodeId typeId)
{
    if (typeId.namespaceIndex == 0 &&
        typeId.identifierType == UA_NODEIDTYPE_NUMERIC &&
        typeId.identifier.numeric <= 30)
    {
        return true;
    }
    UA_NodeId optionSetId = UA_NODEID_NUMERIC(0, UA_NS0ID_OPTIONSET);
    if (UA_NodeId_equal(&typeId, &optionSetId))
    {
        return true;
    }
    return false;
}

static UA_NodeId getParentType(UA_Server *server, const UA_NodeId dataTypeId)
{
    UA_NodeId current = dataTypeId;
    while (!isKnownParent(current))
    {
        current = getParentDataType(server, current);
    }
    return current;
}

static UA_NodeId getReferenceTypeId(const Reference *ref)
{
    if (!ref)
    {
        return UA_NODEID_NULL;
    }
    return getNodeIdFromChars(ref->refType);
}

static UA_NodeId getReferenceTarget(const Reference *ref)
{
    if (!ref)
    {
        return UA_NODEID_NULL;
    }
    return getNodeIdFromChars(ref->target);
}

static Reference *getHierachicalInverseReference(const TNode *node)
{

    Reference *hierachicalRef = node->hierachicalRefs;
    while (hierachicalRef)
    {
        if (!hierachicalRef->isForward)
        {
            return hierachicalRef;
        }
        hierachicalRef = hierachicalRef->next;
    }
    return NULL;
}

static UA_NodeId getParentId(const TNode *node, UA_NodeId *parentRefId)
{
    UA_NodeId parentId = UA_NODEID_NULL;
    if (node->nodeClass == NODECLASS_OBJECT)
    {
        parentId =
            getNodeIdFromChars(((const TObjectNode *)node)->parentNodeId);
    }
    else if (node->nodeClass == NODECLASS_VARIABLE)
    {
        parentId =
            getNodeIdFromChars(((const TVariableNode *)node)->parentNodeId);
    }
    Reference *ref = getHierachicalInverseReference((const TNode *)node);
    *parentRefId = getReferenceTypeId(ref);
    if (UA_NodeId_equal(&parentId, &UA_NODEID_NULL))
    {
        parentId = getReferenceTarget(ref);
    }
    return parentId;
}

static void
handleObjectNode(const TObjectNode *node, UA_NodeId *id,
                 const UA_NodeId *parentId, const UA_NodeId *parentReferenceId,
                 const UA_LocalizedText *lt, const UA_QualifiedName *qn,
                 const UA_LocalizedText *description, UA_Server *server)
{
    UA_ObjectAttributes oAttr = UA_ObjectAttributes_default;
    oAttr.displayName = *lt;
    oAttr.description = *description;
    oAttr.eventNotifier = (UA_Byte)atoi(node->eventNotifier);

    UA_NodeId typeDefId = getNodeIdFromChars(node->refToTypeDef->target);

    // addNode_begin is used, otherwise all mandatory childs from type are
    // instantiated
    UA_Server_addNode_begin(server, UA_NODECLASS_OBJECT, *id, *parentId,
                            *parentReferenceId, *qn, typeDefId, &oAttr,
                            &UA_TYPES[UA_TYPES_OBJECTATTRIBUTES],
                            node->extension, NULL);
}

static void
handleViewNode(const TViewNode *node, UA_NodeId *id, const UA_NodeId *parentId,
               const UA_NodeId *parentReferenceId, const UA_LocalizedText *lt,
               const UA_QualifiedName *qn, const UA_LocalizedText *description,
               UA_Server *server)
{
    UA_ViewAttributes attr = UA_ViewAttributes_default;
    attr.displayName = *lt;
    attr.description = *description;
    attr.eventNotifier = (UA_Byte)atoi(node->eventNotifier);
    attr.containsNoLoops = isTrue(node->containsNoLoops);
    UA_Server_addViewNode(server, *id, *parentId, *parentReferenceId, *qn, attr,
                          node->extension, NULL);
}

static void
handleMethodNode(const TMethodNode *node, UA_NodeId *id,
                 const UA_NodeId *parentId, const UA_NodeId *parentReferenceId,
                 const UA_LocalizedText *lt, const UA_QualifiedName *qn,
                 const UA_LocalizedText *description, UA_Server *server)
{
    UA_MethodAttributes attr = UA_MethodAttributes_default;
    attr.executable = isTrue(node->executable);
    attr.userExecutable = isTrue(node->userExecutable);
    attr.displayName = *lt;
    attr.description = *description;

    UA_Server_addMethodNode(server, *id, *parentId, *parentReferenceId, *qn,
                            attr, NULL, 0, NULL, 0, NULL, node->extension,
                            NULL);
}

static size_t getArrayDimensions(const char *s, UA_UInt32 **dims)
{
    size_t length = strlen(s);
    size_t arrSize = 0;
    if (0 == length)
    {
        return 0;
    }
    // add the first one
    int val = atoi(s);
    arrSize++;
    *dims = (UA_UInt32 *)malloc(sizeof(UA_UInt32));
    *dims[0] = (UA_UInt32)val;

    const char *subString = strchr(s, ';');

    while (subString != NULL)
    {
        arrSize++;
        *dims = (UA_UInt32 *)realloc(*dims, arrSize * sizeof(UA_UInt32));
        *dims[arrSize - 1] = (UA_UInt32)atoi(subString + 1);
        subString = strchr(subString + 1, ';');
    }
    return arrSize;
}

static void handleVariableNode(const TVariableNode *node, UA_NodeId *id,
                               const UA_NodeId *parentId,
                               const UA_NodeId *parentReferenceId,
                               const UA_LocalizedText *lt,
                               const UA_QualifiedName *qn,
                               const UA_LocalizedText *description,
                               UA_Server *server)
{
    UA_VariableAttributes attr = UA_VariableAttributes_default;
    attr.displayName = *lt;
    attr.dataType = getNodeIdFromChars(node->datatype);
    attr.valueRank = atoi(node->valueRank);
    UA_UInt32 *arrDims = NULL;
    attr.arrayDimensionsSize =
        getArrayDimensions(node->arrayDimensions, &arrDims);
    attr.arrayDimensions = arrDims;
    attr.accessLevel = (UA_Byte)atoi(node->accessLevel);
    attr.userAccessLevel = (UA_Byte)atoi(node->userAccessLevel);
    attr.description = *description;
    attr.historizing = isTrue(node->historizing);

    // this case is only needed for the euromap83 comparison, think the nodeset
    // is not valid
    if (attr.arrayDimensions == NULL && attr.valueRank == 1)
    {
        attr.arrayDimensionsSize = 1;
        attr.arrayDimensions = UA_UInt32_new();
        *attr.arrayDimensions = 0;
    }

    if (attr.arrayDimensionsSize == 0 && node->value && node->value->isArray)
    {
        attr.arrayDimensions = UA_UInt32_new();
        *attr.arrayDimensions =
            (UA_UInt32)node->value->data->val.complexData.membersSize;
        attr.arrayDimensionsSize = 1;
    }
    RawData *data = NULL;
    if (node->value)
    {
        const UA_DataType *dataType = UA_findDataType(&attr.dataType);
        if (!dataType)
        {
            // try it with custom types
            dataType = NodesetLoader_getCustomDataType(server, &attr.dataType);
            // try it with parent
            if (!dataType)
            {
                const UA_NodeId parent = getParentType(server, attr.dataType);
                dataType = UA_findDataType(&parent);
            }
        }

        UA_ServerConfig *config = UA_Server_getConfig(server);
        const UA_DataTypeArray *types = config->customDataTypes;

        data = Value_getData(node->value, dataType, types->types);

        if (data)
        {
            if (node->value->isArray)
            {
                UA_Variant_setArray(
                    &attr.value, data->mem,
                    node->value->data->val.complexData.membersSize, dataType);
            }
            else
            {
                UA_Variant_setScalar(&attr.value, data->mem, dataType);
            }
        }
    }
    UA_NodeId typeDefId = getNodeIdFromChars(node->refToTypeDef->target);

    UA_Server_addNode_begin(server, UA_NODECLASS_VARIABLE, *id, *parentId,
                            *parentReferenceId, *qn, typeDefId, &attr,
                            &UA_TYPES[UA_TYPES_VARIABLEATTRIBUTES],
                            node->extension, NULL);
    RawData_delete(data);
    UA_free(attr.arrayDimensions);
}

static void handleObjectTypeNode(const TObjectTypeNode *node, UA_NodeId *id,
                                 const UA_NodeId *parentId,
                                 const UA_NodeId *parentReferenceId,
                                 const UA_LocalizedText *lt,
                                 const UA_QualifiedName *qn,
                                 const UA_LocalizedText *description,
                                 UA_Server *server)
{
    UA_ObjectTypeAttributes oAttr = UA_ObjectTypeAttributes_default;
    oAttr.displayName = *lt;
    oAttr.isAbstract = isTrue(node->isAbstract);
    oAttr.description = *description;

    UA_Server_addObjectTypeNode(server, *id, *parentId, *parentReferenceId, *qn,
                                oAttr, node->extension, NULL);
}

static void handleReferenceTypeNode(const TReferenceTypeNode *node,
                                    UA_NodeId *id, const UA_NodeId *parentId,
                                    const UA_NodeId *parentReferenceId,
                                    const UA_LocalizedText *lt,
                                    const UA_QualifiedName *qn,
                                    const UA_LocalizedText *description,
                                    UA_Server *server)
{
    UA_ReferenceTypeAttributes attr = UA_ReferenceTypeAttributes_default;
    attr.symmetric = isTrue(node->symmetric);
    attr.displayName = *lt;
    attr.description = *description;
    attr.inverseName =
        UA_LOCALIZEDTEXT(node->inverseName.locale, node->inverseName.text);

    UA_Server_addReferenceTypeNode(server, *id, *parentId, *parentReferenceId,
                                   *qn, attr, node->extension, NULL);
}

static void handleVariableTypeNode(const TVariableTypeNode *node, UA_NodeId *id,
                                   const UA_NodeId *parentId,
                                   const UA_NodeId *parentReferenceId,
                                   const UA_LocalizedText *lt,
                                   const UA_QualifiedName *qn,
                                   const UA_LocalizedText *description,
                                   UA_Server *server)
{
    UA_VariableTypeAttributes attr = UA_VariableTypeAttributes_default;
    attr.displayName = *lt;
    attr.description = *description;
    attr.valueRank = atoi(node->valueRank);
    attr.isAbstract = isTrue(node->isAbstract);
    if (attr.valueRank >= 0)
    {
        if (!strcmp(node->arrayDimensions, ""))
        {
            attr.arrayDimensionsSize = 1;
            UA_UInt32 arrayDimensions[1];
            arrayDimensions[0] = 0;
            attr.arrayDimensions = &arrayDimensions[0];
        }
    }

    UA_Server_addNode_begin(server, UA_NODECLASS_VARIABLETYPE, *id, *parentId,
                            *parentReferenceId, *qn, UA_NODEID_NULL, &attr,
                            &UA_TYPES[UA_TYPES_VARIABLETYPEATTRIBUTES],
                            node->extension, NULL);
}

static void handleDataTypeNode(const TDataTypeNode *node, UA_NodeId *id,
                               const UA_NodeId *parentId,
                               const UA_NodeId *parentReferenceId,
                               const UA_LocalizedText *lt,
                               const UA_QualifiedName *qn,
                               const UA_LocalizedText *description,
                               UA_Server *server)
{
    UA_DataTypeAttributes attr = UA_DataTypeAttributes_default;
    attr.displayName = *lt;
    attr.description = *description;
    attr.isAbstract = isTrue(node->isAbstract);

    UA_Server_addDataTypeNode(server, *id, *parentId, *parentReferenceId, *qn,
                              attr, node->extension, NULL);
}

static void addNode(UA_Server *server, const TNode *node)
{
    UA_NodeId id = getNodeIdFromChars(node->id);
    UA_NodeId parentReferenceId = UA_NODEID_NULL;
    UA_NodeId parentId = getParentId(node, &parentReferenceId);
    UA_LocalizedText lt =
        UA_LOCALIZEDTEXT(node->displayName.locale, node->displayName.text);
    UA_QualifiedName qn =
        UA_QUALIFIEDNAME(node->browseName.nsIdx, node->browseName.name);
    UA_LocalizedText description =
        UA_LOCALIZEDTEXT(node->description.locale, node->description.text);

    switch (node->nodeClass)
    {
    case NODECLASS_OBJECT:
        handleObjectNode((const TObjectNode *)node, &id, &parentId,
                         &parentReferenceId, &lt, &qn, &description, server);
        break;

    case NODECLASS_METHOD:
        handleMethodNode((const TMethodNode *)node, &id, &parentId,
                         &parentReferenceId, &lt, &qn, &description, server);
        break;

    case NODECLASS_OBJECTTYPE:
        handleObjectTypeNode((const TObjectTypeNode *)node, &id, &parentId,
                             &parentReferenceId, &lt, &qn, &description,
                             server);
        break;

    case NODECLASS_REFERENCETYPE:
        handleReferenceTypeNode((const TReferenceTypeNode *)node, &id,
                                &parentId, &parentReferenceId, &lt, &qn,
                                &description, server);
        break;

    case NODECLASS_VARIABLETYPE:
        handleVariableTypeNode((const TVariableTypeNode *)node, &id, &parentId,
                               &parentReferenceId, &lt, &qn, &description,
                               server);
        break;

    case NODECLASS_VARIABLE:
        handleVariableNode((const TVariableNode *)node, &id, &parentId,
                           &parentReferenceId, &lt, &qn, &description, server);
        break;
    case NODECLASS_DATATYPE:
        handleDataTypeNode((const TDataTypeNode *)node, &id, &parentId,
                           &parentReferenceId, &lt, &qn, &description, server);
        break;
    case NODECLASS_VIEW:
        handleViewNode((const TViewNode *)node, &id, &parentId,
                       &parentReferenceId, &lt, &qn, &description, server);
        break;
    }
}

int NodesetLoader_BackendOpen62541_addNamespace(void *userContext, const char *namespaceUri)
{
    int idx =
        (int)UA_Server_addNamespace((UA_Server *)userContext, namespaceUri);
    return idx;
}

static void logToOpen(void *context, enum NodesetLoader_LogLevel level,
                      const char *message, ...)
{
    UA_Logger *logger = (UA_Logger *)context;
    va_list vl;
    va_start(vl, message);
    UA_LogLevel uaLevel = UA_LOGLEVEL_DEBUG;
    switch (level)
    {
    case NODESETLOADER_LOGLEVEL_DEBUG:
        uaLevel = UA_LOGLEVEL_DEBUG;
        break;
    case NODESETLOADER_LOGLEVEL_ERROR:
        uaLevel = UA_LOGLEVEL_ERROR;
        break;
    case NODESETLOADER_LOGLEVEL_WARNING:
        uaLevel = UA_LOGLEVEL_WARNING;
        break;
    }
    logger->log(logger->context, uaLevel, UA_LOGCATEGORY_USERLAND, message, vl);
    va_end(vl);
}

struct DataTypeImportCtx
{
    DataTypeImporter *importer;
    const BiDirectionalReference *hasEncodingRef;
    UA_Server *server;
};

static void addDataType(struct DataTypeImportCtx *ctx, TNode *node)
{
    // add only the types
    const BiDirectionalReference *r = ctx->hasEncodingRef;
    while (r)
    {
        if (!TNodeId_cmp(&r->source, &node->id))
        {
            Reference *ref = (Reference *)calloc(1, sizeof(Reference));
            ref->refType = r->refType;
            ref->target = r->target;

            Reference *lastRef = node->nonHierachicalRefs;
            node->nonHierachicalRefs = ref;
            ref->next = lastRef;
            break;
        }
        r = r->next;
    }
    const UA_NodeId parent =
        getParentType(ctx->server, getNodeIdFromChars(node->id));
    DataTypeImporter_addCustomDataType(ctx->importer, (TDataTypeNode *)node,
                                       parent);
}

static void importDataTypes(NodesetLoader *loader, UA_Server *server)
{
    // add datatypes
    const BiDirectionalReference *hasEncodingRef =
        NodesetLoader_getBidirectionalRefs(loader);
    DataTypeImporter *importer = DataTypeImporter_new(server);
    struct DataTypeImportCtx ctx;
    ctx.hasEncodingRef = hasEncodingRef;
    ctx.server = server;
    ctx.importer = importer;
    NodesetLoader_forEachNode(loader, NODECLASS_DATATYPE, &ctx,
                              (NodesetLoader_forEachNode_Func)addDataType);

    DataTypeImporter_initMembers(importer);
    DataTypeImporter_delete(importer);
}

static void addNonHierachicalRefs(UA_Server *server, TNode *node)
{
    Reference *ref = node->nonHierachicalRefs;
    while (ref)
    {

        UA_NodeId src = getNodeIdFromChars(node->id);
        UA_ExpandedNodeId target = UA_EXPANDEDNODEID_NULL;
        target.nodeId = getNodeIdFromChars(ref->target);
        UA_NodeId refType = getNodeIdFromChars(ref->refType);
        UA_Server_addReference(server, src, refType, target, ref->isForward);
        ref = ref->next;
    }
    // brute force, maybe not the best way to do this
    ref = node->hierachicalRefs;
    while (ref)
    {
        UA_NodeId src = getNodeIdFromChars(node->id);
        UA_ExpandedNodeId target = UA_EXPANDEDNODEID_NULL;
        target.nodeId = getNodeIdFromChars(ref->target);
        UA_NodeId refType = getNodeIdFromChars(ref->refType);
        UA_Server_addReference(server, src, refType, target, ref->isForward);
        ref = ref->next;
    }
}

static void addNodes(NodesetLoader *loader, UA_Server *server,
                     NodesetLoader_Logger *logger)
{
    const TNodeClass order[NODECLASS_COUNT] = {
        NODECLASS_REFERENCETYPE, NODECLASS_DATATYPE, NODECLASS_OBJECTTYPE,
        NODECLASS_OBJECT,        NODECLASS_METHOD,   NODECLASS_VARIABLETYPE,
        NODECLASS_VARIABLE,      NODECLASS_VIEW};

    for (size_t i = 0; i < NODECLASS_COUNT; i++)
    {
        const TNodeClass classToImport = order[i];
        size_t cnt =
            NodesetLoader_forEachNode(loader, classToImport, server,
                                      (NodesetLoader_forEachNode_Func)addNode);
        if (classToImport == NODECLASS_DATATYPE)
        {
            importDataTypes(loader, server);
        }
        logger->log(logger->context, NODESETLOADER_LOGLEVEL_DEBUG,
                    "imported %ss: %zu", NODECLASS_NAME[classToImport], cnt);
    }

    for (size_t i = 0; i < NODECLASS_COUNT; i++)
    {
        const TNodeClass classToImport = order[i];
        NodesetLoader_forEachNode(
            loader, classToImport, server,
            (NodesetLoader_forEachNode_Func)addNonHierachicalRefs);
    }
}

bool NodesetLoader_loadFile(struct UA_Server *server, const char *path,
                            NodesetLoader_ExtensionInterface *extensionHandling)
{
    if (!server)
    {
        return false;
    }
    if (!path)
    {
        return false;
    }
    FileContext handler;
    handler.addNamespace = NodesetLoader_BackendOpen62541_addNamespace;
    handler.userContext = server;
    handler.file = path;
    handler.extensionHandling = extensionHandling;

    UA_ServerConfig *config = UA_Server_getConfig(server);
    NodesetLoader_Logger *logger =
        (NodesetLoader_Logger *)calloc(1, sizeof(NodesetLoader_Logger));
    logger->context = &config->logger;
    logger->log = &logToOpen;
    RefService *refService = RefServiceImpl_new(server);

    NodesetLoader *loader = NodesetLoader_new(logger, refService);
    logger->log(logger->context, NODESETLOADER_LOGLEVEL_DEBUG,
                "Start import nodeset: %s", path);
    bool importStatus = NodesetLoader_importFile(loader, &handler);
    bool sortStatus = NodesetLoader_sort(loader);
    bool status = importStatus && sortStatus;
    if (status && sortStatus)
    {
        addNodes(loader, server, logger);
    }
    else
    {
        logger->log(logger->context, NODESETLOADER_LOGLEVEL_ERROR,
                    "importing the nodeset failed, nodes were not added");
    }
    RefServiceImpl_delete(refService);
    NodesetLoader_delete(loader);
    free(logger);
    return status;
}
