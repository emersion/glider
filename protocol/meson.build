wp_dir = wayland_protocols.get_pkgconfig_variable('pkgdatadir')

wayland_scanner_dep = dependency('wayland-scanner', native: true)
wayland_scanner = find_program(
	wayland_scanner_dep.get_pkgconfig_variable('wayland_scanner'),
	native: true,
)

protocols = [
	[wp_dir, 'stable/xdg-shell/xdg-shell.xml'],
]

wl_protos_src = []
wl_protos_headers = []
foreach p : protocols
	xml = join_paths(p)
	wl_protos_src += custom_target(
		xml.underscorify() + '_server_c',
		input: xml,
		output: '@BASENAME@-protocol.c',
		command: [wayland_scanner, 'private-code', '@INPUT@', '@OUTPUT@'],
	)
	wl_protos_headers += custom_target(
		xml.underscorify() + '_server_h',
		input: xml,
		output: '@BASENAME@-protocol.h',
		command: [wayland_scanner, 'server-header', '@INPUT@', '@OUTPUT@'],
	)
endforeach

wl_protos_lib = static_library(
	'glider_protos',
	wl_protos_src + wl_protos_headers,
)

wl_protos = declare_dependency(
	link_with: wl_protos_lib,
	sources: wl_protos_headers,
)
