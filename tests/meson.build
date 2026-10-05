catch2_dep = dependency('catch2-with-main', required: true)

srcdir = '../src'

test_output_staging = executable(
	'test_output_staging',
	[srcdir / 'output_staging.cpp', 'test_output_staging.cpp'],
	include_directories: [srcdir],
)
test('output-staging', test_output_staging)

test_output_rotation = executable(
	'test_output_rotation',
	[srcdir / 'output_rotation.cpp', 'test_output_rotation.cpp'],
	include_directories: [srcdir],
)
test('output-rotation', test_output_rotation)

test_system_overlay_auth = executable(
	'test_system_overlay_auth',
	[srcdir / 'system_overlay_auth.cpp', 'test_system_overlay_auth.cpp'],
	include_directories: [srcdir],
)
test('system-overlay-auth', test_system_overlay_auth)

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
