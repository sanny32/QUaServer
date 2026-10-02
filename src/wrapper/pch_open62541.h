#ifndef PCH_OPEN62541_H
#define PCH_OPEN62541_H

// open62541 public API used by QUaServer (replaces the former amalgamated open62541.h)
#include <open62541/types.h>
#include <open62541/util.h>
#include <open62541/server.h>
#include <open62541/server_config_default.h>
#include <open62541/plugin/log.h>
#include <open62541/plugin/log_stdout.h>
#include <open62541/plugin/accesscontrol.h>
#include <open62541/plugin/accesscontrol_default.h>
#include <open62541/plugin/nodestore.h>
#include <open62541/plugin/certificategroup.h>
#include <open62541/plugin/certificategroup_default.h>
#include <open62541/plugin/securitypolicy.h>
#include <open62541/plugin/securitypolicy_default.h>
#ifdef UA_ENABLE_HISTORIZING
#include <open62541/plugin/historydatabase.h>
#include <open62541/plugin/historydata/history_data_backend.h>
#include <open62541/plugin/historydata/history_data_backend_memory.h>
#include <open62541/plugin/historydata/history_data_gathering.h>
#include <open62541/plugin/historydata/history_data_gathering_default.h>
#include <open62541/plugin/historydata/history_database_default.h>
#endif // UA_ENABLE_HISTORIZING

#endif // PCH_OPEN62541_H
