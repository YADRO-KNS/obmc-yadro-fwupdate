project('fwupdate', 'cpp',
	default_options: [
	  'warning_level=3',
	  'werror=true',
	  'cpp_std=c++17',
	],
    license: 'Apache-2.0',
	version: '0.1',
)

fwupdate_sources = [
    'src/confirm.cpp',
    'src/dbus.cpp',
    'src/firmware.cpp',
    'src/fwupdate.cpp',
    'src/main.cpp',
    'src/openbmc.cpp',
    'src/signature.cpp',
    'src/subprocess.cpp',
    'src/tags.cpp',
]

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

conf.set_quoted('SYSTEMD_BUSNAME', get_option('systemd-busname'))
conf.set_quoted('SYSTEMD_PATH', get_option('systemd-path'))
conf.set_quoted('SYSTEMD_INTERFACE', get_option('systemd-interface'))

conf.set_quoted('REBOOT_GUARD_ENABLE', get_option('reboot-guard-enable'))
conf.set_quoted('REBOOT_GUARD_DISABLE', get_option('reboot-guard-disable'))

openpower_support = get_option('openpower-support')
conf.set('OPENPOWER_SUPPORT', openpower_support)

if openpower_support
    conf.set_quoted('HIOMAPD_PATH', get_option('hiomapd-path'))
    conf.set_quoted('HIOMAPD_IFACE', get_option('hiomapd-iface'))
    conf.set_quoted('PFLASH_CMD', get_option('pflash-cmd'))
    conf.set_quoted('PNOR_FILE_EXT', get_option('pnor-file-ext'))

    fwupdate_sources += [
        'src/openpower.cpp',
    ]
endif

conf.set_quoted('OS_RELEASE_FILE', get_option('os-release-file'))
conf.set_quoted('MANIFEST_FILE_NAME', get_option('manifest-file-name'))
conf.set_quoted('PUBLICKEY_FILE_NAME', get_option('publickey-file-name'))
conf.set_quoted('HASH_FILE_NAME', get_option('hash-file-name'))
conf.set_quoted('SIGNED_IMAGE_CONF_PATH', get_option('signed-image-conf-path'))
conf.set_quoted('SIGNATURE_FILE_EXT', get_option('signature-file-ext'))

conf.set_quoted('OPENBMC_FLASH_PATH', get_option('openbmc-flash-path'))
conf.set_quoted('OPENBMC_WHITELIST_FILE_NAME',
                get_option('openbmc-whitelist-file-name'))

configure_file(output: 'config.h', configuration: conf)

executable(
    'fwupdate',
    fwupdate_sources,
    dependencies: [
        dependency('sdbusplus'),
        dependency('openssl'),
    ],
    install: true,
)
