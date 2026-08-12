#ifndef NX_USER_H
#define NX_USER_H

/* IPv4 UDP telemetry only. */
#define NX_DISABLE_IPV6
#define NX_ENABLE_INTERFACE_CAPABILITY

/*
 * Do not block the NetX IP helper for five seconds when the Ethernet cable
 * is absent at boot.  The application retries NX_LINK_ENABLE after a
 * physical link appears.
 */
#define PHY_LINK_TIMEOUT (0U)

/* Keep DHCP maintenance below the time-critical measurement thread. */
#define NX_DHCP_THREAD_PRIORITY (6u)

#endif /* NX_USER_H */
