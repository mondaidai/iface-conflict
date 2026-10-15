# iface-conflict

A small OpenWrt daemon that watches interface and address changes and emits a UBUS event when overlapping subnets are detected on different interfaces.

## What it does

The program keeps a live view of network interfaces and IP addresses from the Linux netlink socket. When a link or address update arrives, it re-evaluates all known interfaces and checks whether any IPv4 or IPv6 subnets overlap.

If a conflict is found, it sends a UBUS notification named `subnet_conflicts`.

## Typical use case

This is useful on routers or gateways where multiple interfaces should not accidentally share the same private network range. It helps detect misconfiguration early and exposes the issue through the system event bus.

## Project structure

- `src/` — C source, headers, and local build logic
- `src/src/` — implementation files
- `src/include/` — public/internal headers
- `files/etc/init.d/iface-conflict` — OpenWrt init/service script
- `Makefile` — OpenWrt package definition

## Main components

- `main.c` — initializes the daemon, registers signal handlers, starts the event loop, and shuts down cleanly
- `netlink.c` — listens for `RTM_NEWLINK`, `RTM_DELLINK`, `RTM_NEWADDR`, and `RTM_DELADDR` messages and keeps interface state updated
- `ifconf.c` — initializes the context, requests initial state, and performs cleanup
- `ifconf_internal.c` — stores interface data, compares addresses, builds subnet conflict groups, and frees memory
- `ubus_api.c` — emits the `subnet_conflicts` event over UBUS

## Build

This project targets OpenWrt/LEDE-style builds, but it also contains a small native Makefile under `src/` for local compilation.

For the native build:

```sh
cd src
make
```

For the OpenWrt package build, use the root `Makefile` in a compatible OpenWrt build environment.

## Runtime

The daemon runs as a background service and is started by the init script:

```sh
/etc/init.d/iface-conflict start
```

It uses `uloop` and `ubus` to stay reactive to netlink updates and to notify the system when conflicts appear.

## Notes

- IPv6 link-local addresses can be ignored via the `IFCONF_IGNORE_IPV6_LINKLOCAL` option in `ifconf.h`.
- The daemon logs messages to syslog and emits structured event data over UBUS.
- It is designed to monitor interface state continuously rather than as a one-shot scan.
