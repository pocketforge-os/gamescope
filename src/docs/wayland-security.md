# Wayland and X11 client trust

Gamescope's named Wayland socket is an application boundary. A client accepted
through that socket is untrusted even when it has the same UID as Gamescope or
is a descendant of a compositor-launched process. Process names and PIDs are
metadata, not authority.

## Privileged Wayland connections

Gamescope owns a private `SOCK_SEQPACKET` broker endpoint. Only the compositor-
launched system-overlay reaper inherits the other endpoint. Before each initial
launch or respawn, the reaper requests a new connection. Gamescope creates an
AF_UNIX socketpair, creates the server-side `wl_client`, records that exact
connection in its trusted-client registry, and only then passes the connected
client fd to the reaper with `SCM_RIGHTS`. The child consumes it through
`WAYLAND_SOCKET`. Client destruction removes the registry entry.

The global filter exposes these interfaces only to a registered connection:

- `zwlr_layer_shell_v1`
- `gamescope_input_method_manager`
- `gamescope_control`
- `gamescope_pipewire`
- `gamescope_private`
- `zwp_input_method_manager_v2`
- `zwp_virtual_keyboard_manager_v1`

The last two names are reserved before those protocols are implemented. The
filter also covers a direct `wl_registry.bind` using a guessed global name, so
registry-name obscurity is not part of the design. Primary application commands
remain on the public connection path and are not trusted merely because
Gamescope launched them.

## Legacy X11 control properties

PocketForge builds with the `legacy_x11_control_atoms` Meson option disabled by
default. In that configuration, Gamescope does not intern or act on these X11
properties:

- `STEAM_OVERLAY`
- `GAMESCOPE_EXTERNAL_OVERLAY`
- `STEAM_INPUT_FOCUS`
- `GAMESCOPECTRL_BASELAYER_APPID`
- `GAMESCOPECTRL_BASELAYER_WINDOW`
- `GAMESCOPECTRL_REQUEST_SCREENSHOT`
- `GAMESCOPECTRL_DEBUG_REQUEST_SCREENSHOT`

The disabled code includes initial window-map reads as well as later property
notifications. Consequently, an X11 client cannot classify itself as system UI,
redirect focus, or invoke the legacy `/tmp/gamescope.png` capture path. System
overlays use layer-shell over a provenance-checked Wayland connection instead.
The option is an explicit compatibility escape hatch for deployments that put a
Steam client and all other X11 clients in one trusted domain; it must not be
enabled for PocketForge images.

## Xwayland transport boundary

At wlroots commit `edbc105b776f4735924e46666b23969c8551f54f`,
`xwayland/sockets.c:129-141` creates a Linux abstract listener and a filesystem
listener. `xwayland/server.c:496-506` creates those listeners synchronously but
defers the Xwayland fork to an idle callback. Gamescope uses that interval to
inspect the already-created AF_UNIX listener fds with `getsockname()`. Every
abstract listener is replaced with an unnamed, inherited-only listening socket;
the filesystem listener is retained. If the descriptors cannot be verified or
the replacement cannot be made, Gamescope refuses to start Xwayland.

The deployment must keep Gamescope's `/tmp` private from application sandboxes.
Removing the abstract listener prevents a network-namespace-visible endpoint
from bypassing that filesystem boundary.

This does not make clients on one Xwayland mutually isolated. XI2 raw events and
XTEST operate across X11 clients, so one Xwayland instance is one trust domain.
Do not co-host mutually untrusted X11 applications; prefer native Wayland for
untrusted applications. Per-application Xwayland enforcement is tracked by
PocketForge bead `tsp-op5a.440.12`.
