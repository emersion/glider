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

wayland_server = dependency('wayland-server')
liftoff = dependency('liftoff', fallback: ['libliftoff', 'liftoff'])

# Try first to find wlroots as a subproject, then as a system dependency
wlroots_version = ['>=0.8.1', '<0.9.0']
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

executable(
	'glider',
	files(
		'backend/backend.c',
		'main.c',
	),
	dependencies: [
		liftoff,
		wlroots,
	],
	include_directories: glider_inc,
	install: true,
)
