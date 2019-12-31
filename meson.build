project(
	'glider',
	'c',
	version: '0.0.0',
	license: 'MIT',
	meson_version: '>=0.47.0',
	default_options: [
		'c_std=c11',
		'warning_level=2',
		'werror=true',
	],
)

cc = meson.get_compiler('c')
add_project_arguments(cc.get_supported_arguments([
	'-D_POSIX_C_SOURCE=200809L',
	'-DWLR_USE_UNSTABLE',

	'-Wno-missing-braces',
	'-Wno-missing-field-initializers',
	'-Wno-unused-parameter',
]), language: 'c')

wayland_protocols = dependency('wayland-protocols')
wayland_server = dependency('wayland-server')
liftoff = dependency('liftoff', fallback: ['libliftoff', 'liftoff'])
gbm = dependency('gbm')
egl = dependency('egl')
glesv2 = dependency('glesv2')

# Try first to find wlroots as a subproject, then as a system dependency
wlroots_version = ['>=0.9.0', '<0.10.0']
wlroots_proj = subproject(
	'wlroots',
	default_options: ['examples=false'],
	required: false,
	version: wlroots_version,
)
if wlroots_proj.found()
	wlroots = wlroots_proj.get_variable('wlroots')
else
	wlroots = dependency('wlroots', version: wlroots_version)
endif

glider_inc = include_directories('include')

subdir('protocol')

executable(
	'glider',
	files(
		'allocator.c',
		'backend/backend.c',
		'backend/connector.c',
		'backend/crtc.c',
		'backend/device.c',
		'backend/plane.c',
		'backend/prop.c',
		'drm_dumb_allocator.c',
		'gbm_allocator.c',
		'input.c',
		'main.c',
		'output.c',
		'gl_renderer.c',
		'swapchain.c',
		'wlr_buffer.c',
		'xdg_shell.c',
	),
	dependencies: [
		egl,
		gbm,
		glesv2,
		liftoff,
		wl_protos,
		wlroots,
	],
	include_directories: glider_inc,
	install: true,
)
