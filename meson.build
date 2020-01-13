project('fwupdate', 'cpp',
	default_options: [
	  'warning_level=3',
	  'werror=true',
	  'cpp_std=c++17',
	],
    license: 'Apache-2.0',
	version: '0.1',
)

conf = configuration_data()
conf.set_quoted('PROJECT_VERSION', meson.project_version())

conf.set_quoted('MAPPER_BUSNAME', get_option('mapper-busname'))
conf.set_quoted('MAPPER_PATH', get_option('mapper-path'))
conf.set_quoted('MAPPER_INTERFACE', get_option('mapper-interface'))

conf.set_quoted('SYSTEMD_PROPERTIES_INTERFACE',
                get_option('systemd-properties-interface'))

conf.set_quoted('SOFTWARE_OBJPATH', get_option('software-objpath'))
conf.set_quoted('ACTIVATION_IFACE', get_option('activation-iface'))
conf.set_quoted('VERSION_IFACE', get_option('version-iface'))
conf.set_quoted('EXTENDED_VERSION_IFACE', get_option('extended-version-iface'))

configure_file(output: 'config.h', configuration: conf)

executable(
    'fwupdate',
    'main.cpp',
    'utils/dbus.cpp',
    dependencies: [
        dependency('sdbusplus'),
    ],
    install: true,
)
