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

configure_file(output: 'config.h', configuration: conf)

executable(
    'fwupdate',
    'main.cpp',
    install: true,
)
