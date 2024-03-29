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
    'src/fwupdate.cpp',
    'src/fwupdbase.cpp',
    'src/main.cpp',
    'src/signature.cpp',
    'src/subprocess.cpp',
]

fwupdate_dependencies = [
    dependency('sdbusplus'),
    dependency('openssl'),
    meson.get_compiler('cpp').find_library('stdc++fs'),
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

conf.set_quoted('CHASSIS_STATE_PATH', get_option('chassis-state-path'))
conf.set_quoted('CHASSIS_STATE_IFACE', get_option('chassis-state-iface'))
conf.set_quoted('CHASSIS_STATE_OFF', get_option('chassis-state-off'))

reboot_guard_support = get_option('reboot-guard-support')
conf.set('REBOOT_GUARD_SUPPORT', reboot_guard_support)

if reboot_guard_support
    conf.set_quoted('REBOOT_GUARD_ENABLE', get_option('reboot-guard-enable'))
    conf.set_quoted('REBOOT_GUARD_DISABLE', get_option('reboot-guard-disable'))
endif

conf.set('GOLDEN_FLASH_SUPPORT', get_option('golden-flash-support'))

host_image_type = get_option('host-image-type')
if host_image_type == 'openpower'
    conf.set('OPENPOWER_SUPPORT', true)
    conf.set_quoted('HIOMAPD_PATH', get_option('hiomapd-path'))
    conf.set_quoted('HIOMAPD_IFACE', get_option('hiomapd-iface'))
    conf.set_quoted('PFLASH_CMD', get_option('pflash-cmd'))
    conf.set_quoted('PNOR_FILE_EXT', get_option('pnor-file-ext'))

    fwupdate_sources += [
        'src/image_openpower.cpp',
    ]
elif host_image_type == 'intel-c62x'
    conf.set('INTEL_C62X_SUPPORT', true)

    conf.set_quoted('PCH_POWER_PIN', get_option('pch-power-pin'))
    conf.set('PCH_POWER_DOWN_VALUE', get_option('pch-power-down-value'))
    use_pca9698_oepol = get_option('use-pca9698-oepol')
    conf.set('USE_PCA9698_OEPOL', use_pca9698_oepol)
    if use_pca9698_oepol
      fwupdate_dependencies += [
        meson.get_compiler('cpp').find_library('i2c'),
      ]
    endif

    fwupdate_sources += [
        'src/image_bios.cpp',
    ]
    fwupdate_dependencies += [
        dependency('libgpiodcxx'),
    ]

    intel_x722_support = get_option('intel-x722-support')
    conf.set('INTEL_X722_SUPPORT', intel_x722_support)
    if intel_x722_support
        fwupdate_sources += [
            'src/nvm_x722.cpp',
        ]
    endif
endif

conf.set_quoted('TAR_CMD', get_option('tar-cmd'))
conf.set_quoted('REBOOT_CMD', get_option('reboot-cmd'))
conf.set_quoted('FW_SETENV_CMD', get_option('fw-setenv-cmd'))
conf.set_quoted('FW_PRINTENV_CMD', get_option('fw-printenv-cmd'))
conf.set_quoted('FLASHCP_CMD', get_option('flashcp-cmd'))
conf.set_quoted('FLASH_ERASE_CMD', get_option('flash-erase-cmd'))
conf.set_quoted('NANDDUMP_CMD', get_option('nanddump-cmd'))
conf.set_quoted('NANDWRITE_CMD', get_option('nandwrite-cmd'))

conf.set_quoted('OS_RELEASE_FILE', get_option('os-release-file'))
conf.set_quoted('MANIFEST_FILE_NAME', get_option('manifest-file-name'))
conf.set_quoted('PUBLICKEY_FILE_NAME', get_option('publickey-file-name'))
conf.set_quoted('HASH_FILE_NAME', get_option('hash-file-name'))
conf.set_quoted('SIGNED_IMAGE_CONF_PATH', get_option('signed-image-conf-path'))
conf.set_quoted('SIGNATURE_FILE_EXT', get_option('signature-file-ext'))

bmc_image_type = get_option('bmc-image-type')
if bmc_image_type == 'obmc-phosphor-image'
    conf.set('OBMC_PHOSPHOR_IMAGE', true)
    conf.set_quoted('OPENBMC_FLASH_PATH', get_option('openbmc-flash-path'))
    conf.set_quoted('OPENBMC_WHITELIST_FILE_NAME',
                    get_option('openbmc-whitelist-file-name'))
    fwupdate_sources += [
        'src/image_openbmc.cpp',
    ]
elif bmc_image_type == 'intel-platforms'
    conf.set('INTEL_PLATFORMS', true)
    fwupdate_sources += [
        'src/image_intel.cpp',
    ]
endif

configure_file(output: 'config.h', configuration: conf)

executable(
    'fwupdate',
    fwupdate_sources,
    dependencies: fwupdate_dependencies,
    install: true,
    install_dir: get_option('sbindir'),
)
