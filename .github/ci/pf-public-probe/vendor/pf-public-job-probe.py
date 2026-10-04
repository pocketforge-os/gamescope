#!/usr/bin/env python3
"""Collect/evaluate the post-deploy isolation contract from a pf-public job."""

import argparse
import ipaddress
import json
import pathlib
import socket
import struct
import subprocess
import sys
import urllib.request


PRIVATE_ROUTES = tuple(ipaddress.ip_network(value) for value in (
    "10.0.0.0/8", "100.64.0.0/10", "127.0.0.0/8", "169.254.0.0/16",
    "172.16.0.0/12", "192.168.0.0/16",
))


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


def overlaps_private(route):
    return any(route.subnet_of(private) or private.subnet_of(route) for private in PRIVATE_ROUTES)


def evaluate(contract, observations):
    connected = ipaddress.ip_network(contract["connected_subnet"])
    gateway = ipaddress.ip_address(contract["gateway_dns_dhcp"])
    routes = observations.get("routes")
    route_ok = isinstance(routes, list)
    default_seen = False
    connected_seen = False
    if route_ok:
        for route in routes:
            if not isinstance(route, dict) or not isinstance(route.get("dst"), str):
                route_ok = False
                break
            if route["dst"] == "default":
                if route.get("gateway") != str(gateway):
                    route_ok = False
                default_seen = True
                continue
            try:
                network = ipaddress.ip_network(route["dst"], strict=False)
            except ValueError:
                route_ok = False
                break
            if network == connected:
                connected_seen = True
            elif network.version == 4 and overlaps_private(network):
                route_ok = False
            if route.get("gateway") and route["gateway"] != str(gateway):
                try:
                    route_gateway = ipaddress.ip_address(route["gateway"])
                except ValueError:
                    route_ok = False
                else:
                    if any(route_gateway in private for private in PRIVATE_ROUTES):
                        route_ok = False
    route_ok = route_ok and default_seen and connected_seen

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
