/*
 * Copyright (c) 2018 Particle Industries, Inc.  All rights reserved.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation, either
 * version 3 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#include "system_openthread.h"

#include "system_error.h"
#include "logging.h"

#include "openthread-core-config.h"

#include "openthread/openthread.h"
#include "openthread/thread.h"
#include "openthread/instance.h"
#include "openthread/commissioner.h"
#include "openthread/joiner.h"
#include "openthread/platform.h"

extern "C" {

#include "openthread/platform-softdevice.h"

}

#include "nrf_sdh_soc.h"

#define CHECK_THREAD(_expr) \
        do { \
            const otError ret = _expr; \
            if (ret != OT_ERROR_NONE) { \
                LOG(ERROR, #_expr " failed: %d", (int)ret); \
                return systemError(ret); \
            } \
        } while (false)

LOG_SOURCE_CATEGORY("system.ot")

namespace particle {

namespace system {

namespace {

otInstance* g_thread = nullptr;

int systemError(otError error) {
    switch (error) {
    // TODO
    default:
        return SYSTEM_ERROR_UNKNOWN;
    }
}

const char* deviceRoleStr(otDeviceRole role) {
    switch (role) {
    case OT_DEVICE_ROLE_DISABLED:
        return "disabled";
    case OT_DEVICE_ROLE_DETACHED:
        return "detached";
    case OT_DEVICE_ROLE_CHILD:
        return "child";
    case OT_DEVICE_ROLE_ROUTER:
        return "router";
    case OT_DEVICE_ROLE_LEADER:
        return "leader";
    default:
        return "unknown";
    }
}

const char* commissionerStateStr(otCommissionerState state) {
    switch (state) {
    case OT_COMMISSIONER_STATE_DISABLED:
        return "disabled";
    case OT_COMMISSIONER_STATE_PETITION:
        return "petition";
    case OT_COMMISSIONER_STATE_ACTIVE:
        return "active";
    default:
        return "unknown";
    }
}

const char* joinerStateStr(otJoinerState state) {
    switch (state) {
    case OT_JOINER_STATE_IDLE:
        return "idle";
    case OT_JOINER_STATE_DISCOVER:
        return "discover";
    case OT_JOINER_STATE_CONNECT:
        return "connect";
    case OT_JOINER_STATE_CONNECTED:
        return "connected";
    case OT_JOINER_STATE_ENTRUST:
        return "entrust";
    case OT_JOINER_STATE_JOINED:
        return "joined";
    default:
        return "unknown";
    }
}

void threadStateChanged(uint32_t flags, void* data) {
    const auto thread = (otInstance*)data;
    if (flags & OT_CHANGED_IP6_ADDRESS_ADDED) {
        LOG(TRACE, "IPv6 address was added");
    }
    if (flags & OT_CHANGED_IP6_ADDRESS_REMOVED) {
        LOG(TRACE, "IPv6 address was removed");
    }
    if (flags & OT_CHANGED_THREAD_ROLE) {
        const otDeviceRole role = otThreadGetDeviceRole(thread);
        LOG(INFO, "Role changed: %s", deviceRoleStr(role));
    }
    if (flags & OT_CHANGED_THREAD_LL_ADDR) {
        LOG(TRACE, "Link-local address changed");
    }
    if (flags & OT_CHANGED_THREAD_ML_ADDR) {
        LOG(TRACE, "Mesh-local address changed");
    }
    if (flags & OT_CHANGED_THREAD_RLOC_ADDED) {
        LOG(TRACE, "RLOC was added");
    }
    if (flags & OT_CHANGED_THREAD_RLOC_REMOVED) {
        LOG(TRACE, "RLOC was removed");
    }
    if (flags & OT_CHANGED_THREAD_PARTITION_ID) {
        LOG(TRACE, "Partition ID changed");
    }
    if (flags & OT_CHANGED_THREAD_KEY_SEQUENCE_COUNTER) {
        LOG(TRACE, "Thread key sequence changed");
    }
    if (flags & OT_CHANGED_THREAD_NETDATA) {
        LOG(TRACE, "Thread network data changed");
    }
    if (flags & OT_CHANGED_THREAD_CHILD_ADDED) {
        LOG(TRACE, "Child was added");
    }
    if (flags & OT_CHANGED_THREAD_CHILD_REMOVED) {
        LOG(TRACE, "Child was removed");
    }
    if (flags & OT_CHANGED_IP6_MULTICAST_SUBSRCRIBED) {
        LOG(TRACE, "Subscribed to IPv6 multicast address");
    }
    if (flags & OT_CHANGED_IP6_MULTICAST_UNSUBSRCRIBED) {
        LOG(TRACE, "Unsubscribed from IPv6 multicast address");
    }
    if (flags & OT_CHANGED_COMMISSIONER_STATE) {
        const otCommissionerState state = otCommissionerGetState(thread);
        LOG(INFO, "Commissioner state changed: %s", commissionerStateStr(state));
    }
    if (flags & OT_CHANGED_JOINER_STATE) {
        const otJoinerState state = otJoinerGetState(thread);
        LOG(INFO, "Joiner state changed: %s", joinerStateStr(state));
    }
    if (flags & OT_CHANGED_THREAD_CHANNEL) {
        LOG(TRACE, "Thread network channel changed");
    }
    if (flags & OT_CHANGED_THREAD_PANID) {
        LOG(TRACE, "Thread network PAN ID changed");
    }
    if (flags & OT_CHANGED_THREAD_NETWORK_NAME) {
        LOG(TRACE, "Thread network name changed");
    }
    if (flags & OT_CHANGED_THREAD_EXT_PANID) {
        LOG(TRACE, "Thread network extended PAN ID changed");
    }
    if (flags & OT_CHANGED_MASTER_KEY) {
        LOG(TRACE, "Master key changed");
    }
    if (flags & OT_CHANGED_PSKC) {
        LOG(TRACE, "PSKc changed");
    }
    if (flags & OT_CHANGED_SECURITY_POLICY) {
        LOG(TRACE, "Security policy changed");
    }
}

otInstance* allocInstance() {
#if OPENTHREAD_ENABLE_MULTIPLE_INSTANCES
    size_t size = 0;
    otInstanceInit(nullptr, &size);
    void* const buf = calloc(1, size);
    if (!buf) {
        return nullptr;
    }
    return otInstanceInit(buf, &size);
#else
    return otInstanceInitSingle();
#endif
}

void processSocEvent(uint32_t event, void* data) {
    PlatformSoftdeviceSocEvtHandler(event);
}

} // particle::system::

int threadInit() {
    PlatformInit(0, nullptr);
    otInstance* const thread = allocInstance();
    if (!thread) {
        LOG(ERROR, "Unable to initialize OpenThread");
        return SYSTEM_ERROR_UNKNOWN;
    }
    LOG(INFO, "OpenThread version: %s", otGetVersionString());
    CHECK_THREAD(otSetStateChangedCallback(thread, threadStateChanged, thread));
    otLinkModeConfig mode = {};
    mode.mRxOnWhenIdle = true;
    mode.mSecureDataRequests = true;
    mode.mDeviceType = true;
    mode.mNetworkData = true;
    CHECK_THREAD(otThreadSetLinkMode(thread, mode));
    if (otDatasetIsCommissioned(thread)) {
        CHECK_THREAD(otIp6SetEnabled(thread, true));
        CHECK_THREAD(otThreadSetEnabled(thread, true));
        LOG(INFO, "Network name: %s", otThreadGetNetworkName(thread));
        LOG(INFO, "802.15.4 channel: %d", (int)otLinkGetChannel(thread));
        LOG(INFO, "802.15.4 PAN ID: 0x%04x", (unsigned)otLinkGetPanId(thread));
    }
    // Register a handler for SOC events
    NRF_SDH_SOC_OBSERVER(socObserver, NRF_SDH_SOC_STACK_OBSERVER_PRIO, processSocEvent, nullptr);
    g_thread = thread;
    return 0;
}

void threadProcess() {
    if (g_thread) {
        otTaskletsProcess(g_thread);
        PlatformProcessDrivers(g_thread);
    }
}

otInstance* threadInstance() {
    return g_thread;
}

} // particle::system

} // particle