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
