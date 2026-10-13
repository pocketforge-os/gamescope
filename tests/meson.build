catch2_dep = dependency('catch2-with-main', required: true)

srcdir = '../src'

test_output_staging = executable(
	'test_output_staging',
	[srcdir / 'output_staging.cpp', 'test_output_staging.cpp'],
	include_directories: [srcdir],
)
test('output-staging', test_output_staging)

test_drm_device_selection = executable(
	'test_drm_device_selection',
	[srcdir / 'drm_device_selection.cpp', 'test_drm_device_selection.cpp'],
	include_directories: [srcdir],
)
test('drm-device-selection', test_drm_device_selection)

test_drm_commit_probe = executable(
	'test_drm_commit_probe',
	'test_drm_commit_probe.cpp',
	include_directories: [srcdir],
)
test('drm-commit-probe', test_drm_commit_probe)

test_pipeline_compile_probe = executable(
	'test_pipeline_compile_probe',
	'test_pipeline_compile_probe.cpp',
	include_directories: [srcdir],
)
test('pipeline-compile-probe', test_pipeline_compile_probe)

test_compositor_diagnostics = executable(
	'test_compositor_diagnostics',
	'test_compositor_diagnostics.cpp',
	include_directories: [srcdir],
)
test('compositor-diagnostics', test_compositor_diagnostics)

if drm_dep.found()
	test_drm_format_selection = executable(
		'test_drm_format_selection',
		[srcdir / 'drm_format_selection.cpp', 'test_drm_format_selection.cpp'],
		include_directories: [srcdir],
		dependencies: [drm_dep],
	)
	test('drm-format-selection', test_drm_format_selection)
endif

test_output_rotation = executable(
	'test_output_rotation',
	[srcdir / 'output_rotation.cpp', 'test_output_rotation.cpp'],
	include_directories: [srcdir],
)
test('output-rotation', test_output_rotation)

test_runtime_orientation = executable(
	'test_runtime_orientation',
	[
		srcdir / 'output_rotation.cpp',
		srcdir / 'runtime_orientation.cpp',
		'test_runtime_orientation.cpp',
	],
	include_directories: [srcdir],
	dependencies: [json_c_dep],
)
test('runtime-orientation', test_runtime_orientation)

test_xwayland_orientation = executable(
	'test_xwayland_orientation',
	[
		srcdir / 'output_rotation.cpp',
		srcdir / 'runtime_orientation.cpp',
		'test_xwayland_orientation.cpp',
	],
	include_directories: [srcdir],
	dependencies: [dep_wayland, dep_x11, dep_xrandr, json_c_dep, thread_dep],
)
test(
	'xwayland-orientation',
	test_xwayland_orientation,
	args: [gamescope_exe, gamescope_reaper_exe],
	timeout: 30,
)

test_privileged_wayland = executable(
	'test_privileged_wayland',
	[
		srcdir / 'wayland_security.cpp',
		srcdir / 'x11_control_policy.cpp',
		srcdir / 'xwayland_security.cpp',
		'test_privileged_wayland.cpp',
	],
	include_directories: [srcdir],
	dependencies: [dep_wayland, wayland_server],
	cpp_args: gamescope_cpp_args,
)
test('privileged-wayland', test_privileged_wayland)

test_input_protocol_globals = executable(
	'test_input_protocol_globals',
	[
		srcdir / 'input_protocols.cpp',
		'test_input_protocol_globals.cpp',
		# Generated protocol sources, as in src/meson.build: they put the
		# generated server headers (cursor-shape-v1-protocol.h, included by
		# wlr_cursor_shape_v1.h) on the include path and provide
		# zwp_tablet_tool_v2_interface for the cursor-shape private code.
		protocols_server_src,
	],
	include_directories: [srcdir],
	dependencies: [dep_wayland, wayland_server, wlroots_dep],
	cpp_args: gamescope_cpp_args,
)
test('input-protocol-globals', test_input_protocol_globals)

test_output_rotation_vulkan = executable(
	'test_output_rotation_vulkan',
	[spirv_shaders, 'test_output_rotation_vulkan.cpp'],
	include_directories: [srcdir],
	dependencies: [vulkan_dep],
)
test('output-rotation-vulkan', test_output_rotation_vulkan)

test_output_staging_vulkan = executable(
	'test_output_staging_vulkan',
	'test_output_staging_vulkan.cpp',
	dependencies: [vulkan_dep],
)
test('output-staging-vulkan', test_output_staging_vulkan)

# Standalone lavapipe RCA probe. This intentionally is not a Meson test: its
# exact-case failure is evidence, while the neighboring cases bisect the cause.
vulkan_headless_allocation_probe = executable(
	'vulkan_headless_allocation_probe',
	'vulkan_headless_allocation_probe.cpp',
	dependencies: [vulkan_dep],
)

unittest_src = []
foreach src : gamescope_core_src + ['Script/Script.cpp', 'convar_script.cpp']
	unittest_src += srcdir / src
endforeach

test_convar = executable(
	'test_convar',
	unittest_src + ['test_convar.cpp'],
	include_directories: [srcdir, sol2_include],
	dependencies: [catch2_dep, luajit_dep, cap_dep, drm_dep, glm_dep, wlroots_dep],
	cpp_args: gamescope_cpp_args,
)
test('convar', test_convar)

test_vulkan_present_features = executable(
	'test_vulkan_present_features',
	['test_vulkan_present_features.cpp'],
	include_directories: [srcdir],
)
test('vulkan_present_features', test_vulkan_present_features)

test_stop_thread = executable(
	'test_stop_thread',
	'test_stop_thread.cpp',
	include_directories: [srcdir],
)
test('stop-thread', test_stop_thread)

test_vulkan_device_extensions = executable(
	'test_vulkan_device_extensions',
	'test_vulkan_device_extensions.cpp',
	include_directories: [srcdir],
)
test('vulkan-device-extensions', test_vulkan_device_extensions)

test_commit_fence = executable(
	'test_commit_fence',
	'test_commit_fence.cpp',
	include_directories: [srcdir],
)
test('commit-fence', test_commit_fence)
