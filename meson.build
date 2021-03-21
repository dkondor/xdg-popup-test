project(
	'popuptest',
	'c',
	version: '0.0.1',
	license: 'MIT',
	meson_version: '>=0.51.0',
	default_options: [
		'c_std=c11',
		'warning_level=2',
		'werror=false',
	],
)

# dependencies for loadable plugin
wlroots = dependency('wlroots')
wlclient = dependency('wayland-client')
wlserver = dependency('wayland-server')
cairo = dependency('cairo')

wayland_protos = dependency('wayland-protocols')
wayland_scanner = find_program('wayland-scanner')
wl_protocol_dir = wayland_protos.get_pkgconfig_variable('pkgdatadir')

wayland_scanner_code = generator(
	wayland_scanner,
	output: '@BASENAME@-protocol.c',
	arguments: ['private-code', '@INPUT@', '@OUTPUT@'],
)

wayland_scanner_client = generator(
	wayland_scanner,
	output: '@BASENAME@-client-protocol.h',
	arguments: ['client-header', '@INPUT@', '@OUTPUT@'],
)

client_protocols = [
    [wl_protocol_dir, 'stable/xdg-shell/xdg-shell.xml']
]

wl_protos_client_src = []
wl_protos_headers = []

foreach p : client_protocols
	xml = join_paths(p)
	wl_protos_headers += wayland_scanner_client.process(xml)
	wl_protos_client_src += wayland_scanner_code.process(xml)
endforeach

lib_protos = static_library('wl_protos', wl_protos_client_src + wl_protos_headers,
	dependencies: [wlclient]) # for the include directory

protos = declare_dependency(
	link_with: lib_protos,
	sources: wl_protos_headers,
)

pt = executable('pt', 'popup_test.c',
        dependencies: [wlclient, wlserver, cairo, protos])


       
