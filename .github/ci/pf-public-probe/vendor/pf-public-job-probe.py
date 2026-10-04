#!/usr/bin/env python3
"""Collect/evaluate the post-deploy isolation contract from a pf-public job."""

import argparse
import ipaddress
import json
import os
import pathlib
import re
import socket
import struct
import subprocess
import sys
import urllib.request


def tcp_reachable(host, port, timeout=1.5):
    try:
        with socket.create_connection((host, int(port)), timeout=timeout):
            return True
    except OSError:
        return False


def gateway_dns_ok(gateway, name, timeout=3.0):
    transaction = 0x5046
    labels = b"".join(bytes([len(part)]) + part.encode("ascii") for part in name.split(".")) + b"\0"
    query = struct.pack("!HHHHHH", transaction, 0x0100, 1, 0, 0, 0) + labels + struct.pack("!HH", 1, 1)
    try:
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as client:
            client.settimeout(timeout)
            client.sendto(query, (gateway, 53))
            response, source = client.recvfrom(4096)
        if source[0] != gateway or len(response) < 12:
            return False
        reply_id, flags, questions, answers, _, _ = struct.unpack("!HHHHHH", response[:12])
        return reply_id == transaction and flags & 0x8000 and flags & 0x000F == 0 and questions == 1 and answers > 0
    except (OSError, UnicodeError):
        return False


def dhcp_server_from_leases():
    servers = set()
    lease_root = pathlib.Path("/run/systemd/netif/leases")
    try:
        leases = list(lease_root.iterdir())
    except OSError:
        return None
    for lease in leases:
        try:
            for line in lease.read_text().splitlines():
                if line.startswith("SERVER_ADDRESS="):
                    servers.add(line.split("=", 1)[1])
        except OSError:
            continue
    return next(iter(servers)) if len(servers) == 1 else None


def configured_dns_servers():
    # Ubuntu may expose 127.0.0.53 in /etc/resolv.conf. The non-stub resolved
    # file is the bounded upstream evidence when it exists.
    for filename in ("/run/systemd/resolve/resolv.conf", "/etc/resolv.conf"):
        try:
            lines = pathlib.Path(filename).read_text().splitlines()
        except OSError:
            continue
        return [
            fields[1] for line in lines
            if len(fields := line.split()) >= 2 and fields[0] == "nameserver"
        ]
    return None


def collect(contract):
    route_result = subprocess.run(
        ["ip", "-json", "-4", "route", "show"],
        check=False,
        capture_output=True,
        text=True,
    )
    if route_result.returncode:
        raise RuntimeError("route_collection_failed")
    routes = json.loads(route_result.stdout)
    endpoints = contract["blocked_tcp_endpoints"]
    observations = {
        "routes": routes,
        "rules": ip_json(["ip", "-json", "-4", "rule", "show"], "rule_collection_failed"),
        "routes_all": ip_json(["ip", "-json", "-4", "route", "show", "table", "all"],
                              "route_table_collection_failed"),
        "links": link_facts({route.get("dev") for route in routes if isinstance(route, dict)}),
        "dhcp_server": dhcp_server_from_leases(),
        "dns_servers": configured_dns_servers(),
        "gateway_dns_ok": gateway_dns_ok(
            contract["gateway_dns_dhcp"], contract["dns_test_name"]),
        "public_https_ok": False,
        "tcp_reachable": {
            endpoint["name"]: tcp_reachable(endpoint["host"], endpoint["port"])
            for endpoint in endpoints
        },
    }
    try:
        request = urllib.request.Request(contract["public_https_url"], method="HEAD")
        with urllib.request.urlopen(request, timeout=10) as response:
            observations["public_https_ok"] = 200 <= response.status < 500
    except OSError:
        pass
    return observations


def ip_json(command, failure):
    result = subprocess.run(command, check=False, capture_output=True, text=True)
    if result.returncode:
        raise RuntimeError(failure)
    return json.loads(result.stdout)


DEFAULT_RULES = {(0, "local"), (32766, "main"), (32767, "default")}
RULE_KEYS = {"priority", "src", "table", "protocol"}


def policy_tables_ok(observations):
    """Policy routing may select only the kernel-default tables, each holding only what it should.

    Rules are exactly the kernel defaults (0 local, 32766 main, 32767 default); any
    other rule, selector or action fails. Table main must be the same table the v2
    route policy evaluated. Table local holds only `local` entries for the guest's own
    addresses (the uplink's and local bridges' connected-route sources, 127/8 on lo)
    and `broadcast` entries for those subnets. Table default is empty. Any other
    table, or anything unclassified, fails.
    """
    rules, routes_all, main_view = (observations.get("rules"), observations.get("routes_all"),
                                    observations.get("routes"))
    if not all(isinstance(item, list) for item in (rules, routes_all, main_view)):
        return False
    seen = set()
    for rule in rules:
        if (not isinstance(rule, dict) or not set(rule) <= RULE_KEYS or rule.get("src") != "all"
                or rule.get("protocol", "kernel") != "kernel"
                or not isinstance(rule.get("priority"), int) or isinstance(rule.get("priority"), bool)):
            return False
        seen.add((rule["priority"], rule.get("table")))
    if len(rules) != len(DEFAULT_RULES) or seen != DEFAULT_RULES:
        return False
    if not all(isinstance(route, dict) for route in routes_all + main_view):
        return False

    def key(route):
        return (route.get("dst"), route.get("dev"), route.get("gateway"), route.get("scope"))

    main_all = [route for route in routes_all if route.get("table", "main") == "main"]
    if sorted(map(key, main_all), key=repr) != sorted(map(key, main_view), key=repr):
        return False
    own = {}
    for route in main_view:
        if ("/" in str(route.get("dst")) and not route.get("gateway") and route.get("prefsrc")
                and isinstance(route.get("dev"), str)):
            try:
                own.setdefault(route["dev"], []).append(
                    (ipaddress.ip_address(route["prefsrc"]), ipaddress.ip_network(route["dst"])))
            except ValueError:
                return False
    loopback = ipaddress.ip_network("127.0.0.0/8")
    for route in routes_all:
        table = route.get("table", "main")
        if table == "main":
            continue
        if table != "local":
            return False
        kind, dev, dst = route.get("type"), route.get("dev"), route.get("dst")
        if kind not in ("local", "broadcast") or not isinstance(dev, str) or not isinstance(dst, str):
            return False
        try:
            network = ipaddress.ip_network(dst, strict=False)
        except ValueError:
            return False
        if network.version != 4:
            return False
        if dev == "lo":
            if not network.subnet_of(loopback) or (
                    kind == "broadcast" and network != ipaddress.ip_network("127.255.255.255/32")):
                return False
            continue
        entries = own.get(dev)
        if not entries or network.prefixlen != 32:
            return False
        address = network.network_address
        if kind == "local" and not any(address == source for source, _ in entries):
            return False
        if kind == "broadcast" and not any(address == subnet.broadcast_address for _, subnet in entries):
            return False
    return True


def link_facts(names):
    """Bridge membership for every route device, read from sysfs (unknown stays unknown)."""
    root = pathlib.Path(os.environ.get("PF_PUBLIC_PROBE_SYSFS", "/sys/class/net"))
    facts = {}
    for name in sorted(n for n in names if isinstance(n, str)):
        if not re.fullmatch(r"[A-Za-z0-9_.-]{1,15}", name):
            continue
        base = root / name
        if not base.exists():
            continue
        bridge = (base / "bridge").is_dir()
        try:
            brif = sorted(entry.name for entry in (base / "brif").iterdir()) if bridge else []
        except OSError:
            continue
        master = os.path.basename(os.readlink(base / "master")) if (base / "master").is_symlink() else None
        facts[name] = {"bridge": bridge, "brif": brif, "master": master}
    return facts


def route_policy_ok(contract, observations):
    """Classify every route by its egress device; anything unrecognised fails.

    The uplink (the device of the single default route) may carry exactly the
    default via the DHCP gateway, the connected pf-public subnet and an optional
    scope-link /32 to that gateway. Any other device must be a guest-local Linux
    bridge (docker0 / br-*) that the uplink is not enslaved to, carrying only a
    connected subnet that does not overlap pf-public. Link state is irrelevant.
    """
    connected = ipaddress.ip_network(contract["connected_subnet"])
    gateway = ipaddress.ip_address(contract["gateway_dns_dhcp"])
    bridge_name = re.compile(contract["local_bridge_pattern"])
    routes, links = observations.get("routes"), observations.get("links")
    if not isinstance(routes, list) or not isinstance(links, dict) or not routes:
        return False
    if not all(isinstance(r, dict) and isinstance(r.get("dst"), str) and isinstance(r.get("dev"), str)
               for r in routes):
        return False
    defaults = [r for r in routes if r["dst"] == "default"]
    if len(defaults) != 1 or defaults[0].get("gateway") != str(gateway):
        return False
    uplink = defaults[0]["dev"]
    uplink_facts = links.get(uplink)
    if not isinstance(uplink_facts, dict) or uplink_facts.get("master") is not None or uplink_facts.get("bridge"):
        return False
    connected_seen = False
    for route in routes:
        if route is defaults[0]:
            continue
        try:
            network = ipaddress.ip_network(route["dst"], strict=False)
        except ValueError:
            return False
        if network.version != 4:
            return False
        if route["dev"] == uplink:
            if route.get("gateway"):
                return False
            if network == connected:
                connected_seen = True
            elif network != ipaddress.ip_network(f"{gateway}/32") or route.get("scope") != "link":
                return False
            continue
        facts = links.get(route["dev"])
        if (not bridge_name.fullmatch(route["dev"]) or not isinstance(facts, dict)
                or facts.get("bridge") is not True or not isinstance(facts.get("brif"), list)
                or uplink in facts["brif"] or uplink_facts.get("master") == route["dev"]
                or route.get("gateway") or network.overlaps(connected)):
            return False
    return connected_seen


def evaluate(contract, observations):
    gateway = ipaddress.ip_address(contract["gateway_dns_dhcp"])
    route_ok = route_policy_ok(contract, observations) and policy_tables_ok(observations)

    expected_endpoints = {item["name"] for item in contract["blocked_tcp_endpoints"]}
    reachable = observations.get("tcp_reachable")
    endpoint_shape = isinstance(reachable, dict) and set(reachable) == expected_endpoints \
        and all(isinstance(value, bool) for value in reachable.values())
    ssh_names = {name for name in expected_endpoints if name.endswith("-ssh")}
    metadata_names = expected_endpoints - ssh_names
    checks = {
        "no_lan_route": route_ok,
        "no_ssh_to_host_or_pool": endpoint_shape and all(not reachable[name] for name in ssh_names),
        "no_metadata_or_credential_endpoint": (
            endpoint_shape and all(not reachable[name] for name in metadata_names)
        ),
        "gateway_dhcp": observations.get("dhcp_server") == str(gateway),
        "gateway_dns": (
            observations.get("dns_servers") == [str(gateway)]
            and observations.get("gateway_dns_ok") is True
        ),
        "public_egress": observations.get("public_https_ok") is True,
    }
    return checks


def load_contract(config_path):
    config = json.loads(pathlib.Path(config_path).read_text())
    return config["network_policies"]["pf-public"]["probe_contract"]


def main(argv=None):
    parser = argparse.ArgumentParser()
    parser.add_argument("--config", required=True)
    source = parser.add_mutually_exclusive_group(required=True)
    source.add_argument("--live", action="store_true")
    source.add_argument("--observations")
    args = parser.parse_args(argv)
    try:
        contract = load_contract(args.config)
        observations = collect(contract) if args.live else json.loads(
            pathlib.Path(args.observations).read_text())
        checks = evaluate(contract, observations)
    except (KeyError, OSError, RuntimeError, ValueError, json.JSONDecodeError) as exc:
        print(f"reason=public_probe_invalid detail={str(exc)[:160]}", file=sys.stderr)
        return 1
    if args.live:
        print("public_probe_observations=" + json.dumps(observations, sort_keys=True, separators=(",", ":")))
    failed = [name for name, passed in checks.items() if not passed]
    for name, passed in checks.items():
        stream = sys.stdout if passed else sys.stderr
        print(("public_probe_assertion=ok" if passed else "public_probe_failed") + f" name={name}", file=stream)
    if failed:
        return 1
    print(f"public_probe_status=ok assertions={len(checks)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
