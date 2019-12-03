#!/usr/bin/env python

# OpenBMC/OpenPOWER firmware updater for VESNIN server.
#
# Copyright (c) 2019 YADRO
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

import argparse
import fcntl
import os
import re
import shutil
import subprocess
import sys
import tarfile
import dbus

# Terminal colors
if sys.stdout.isatty():
    CLR_ERROR = '\033[31;1m'  # red
    CLR_SUCCESS = '\033[32m'  # green
    CLR_RESET = '\033[0m'
else:
    CLR_ERROR = ''
    CLR_SUCCESS = ''
    CLR_RESET = ''


class DbusClient(object):
    """
    D-bus client definitions.
    """

    MAPPER_BUS = 'xyz.openbmc_project.ObjectMapper'
    MAPPER_PATH = '/xyz/openbmc_project/object_mapper'
    MAPPER_IFACE = 'xyz.openbmc_project.ObjectMapper'

    def __init__(self, bus=None):
        if not bus:
            bus = dbus.SystemBus()

        self._bus = bus

    def get_property(self, bus, path, iface, name):
        """
        Get D-bus property value
        :param bus: Bus name
        :param path: Object path
        :param iface: Property interface
        :param name: Property name
        :return: Property value
        """
        assert(self._bus)
        return self._bus.call_blocking(bus, path, dbus.PROPERTIES_IFACE,
                                       'Get', 'ss', [iface, name])

    def get_object(self, path, interfaces=None):
        """
        Obtain a dictionary of service -> implemented interface(s) for the
        given path.
        :param path: The object path for wich the result should be fetched.
        :param interfaces: An array of result set constraining interfaces.
        :return: A dictionary of services -> implemented interface(s).
        """
        if not interfaces:
            interfaces = []

        assert(self._bus)
        return self._bus.call_blocking(
            self.MAPPER_BUS, self.MAPPER_PATH, self.MAPPER_IFACE,
            'GetObject', 'sas', [path, interfaces])

    def get_subtree(self, path, interfaces=None):
        """
        Obtain a dictionary of path -> services where path is in subtree and
        services is of the type returned by the get_object method.
        :param path: The subtree path for wich the result should be fetched.
        :param interfaces: An array of result set constraining interfaces.
        :return: A dictionary of path -> services.
        """
        if not interfaces:
            interfaces = []

        assert(self._bus)
        return self._bus.call_blocking(
            self.MAPPER_BUS, self.MAPPER_PATH, self.MAPPER_IFACE,
            'GetSubTree', 'sias', [path, 0, interfaces])


class VersionInfo(DbusClient):
    """
    Firmware version.
    """

    VERSION_IFACE = 'xyz.openbmc_project.Software.Version'
    ACTIVATION_IFACE = 'xyz.openbmc_project.Software.Activation'
    EXT_VER_IFACE = 'xyz.openbmc_project.Software.ExtendedVersion'
    SOFTWARE_PATH = '/xyz/openbmc_project/software'

    def __init__(self, bus=None):
        DbusClient.__init__(self, bus)

    def show(self):
        """
        Print installed firmware version info.
        """
        objs = self.get_subtree(self.SOFTWARE_PATH, [self.ACTIVATION_IFACE])
        for path, bus_entry in objs.items():
            for bname in bus_entry:
                activation = self.get_property(bname, path,
                                               self.ACTIVATION_IFACE,
                                               'Activation')
                if activation != self.ACTIVATION_IFACE + '.Activations.Active':
                    continue

                version = self.get_property(bname, path,
                                            self.VERSION_IFACE,
                                            'Version')
                purpose = self.get_property(bname, path,
                                            self.VERSION_IFACE,
                                            'Purpose').split('.')[-1]
                print('{}: {}'.format(purpose, version))

                try:
                    ext_ver = self.get_property(bname, path,
                                                self.EXT_VER_IFACE,
                                                'ExtendedVersion')
                    for item in ext_ver.split(','):
                        print('{}  {}'.format(' ' * len(purpose), item))
                except dbus.exceptions.DBusException:
                    pass


class PNORLock(DbusClient):
    """
    PNOR flash access lock.
    """

    # Enable/disable locking
    USE_LOCK = True

    # Lock file path
    LOCK_FILE_PATH = '/var/lock/fwupdate.lock'

    CHASSIS_PATH = '/xyz/openbmc_project/state/chassis0'
    CHASSIS_IFACE = 'xyz.openbmc_project.State.Chassis'

    def __init__(self, bus=None):
        DbusClient.__init__(self, bus)
        self._lock_file = None

    def __enter__(self):
        if PNORLock.USE_LOCK:
            self.lock()
        return self

    def __exit__(self, exc_type, exc_val, exc_tb):
        self.unlock()

    def lock(self):
        """
        Check and lock access to the PNOR flash.
        """
        assert self._lock_file is None
        try:
            self._lock_file = open(self.LOCK_FILE_PATH, 'w')
            fcntl.lockf(self._lock_file, fcntl.LOCK_EX | fcntl.LOCK_NB)
            self._check_pflash()
            self._check_chassis_state()
        except Exception as e:
            self.unlock()
            raise Exception('Unable to lock PNOR flash access: ' + str(e))

    def unlock(self):
        """
        Unlock access to the PNOR flash.
        """
        if self._lock_file:
            try:
                fcntl.lockf(self._lock_file, fcntl.LOCK_UN)
                self._lock_file.close()
                self._lock_file = None
                os.remove(self.LOCK_FILE_PATH)
            except Exception:
                pass  # Ignore all errors

    @staticmethod
    def _check_pflash():
        """
        Check for running pflash utility.
        """
        with open(os.devnull, 'w') as DEVNULL:
            if subprocess.call(['/bin/pidof', 'pflash'], stdout=DEVNULL,
                               stderr=subprocess.STDOUT) == 0:
                raise Exception('pflash is running')

    def _check_chassis_state(self):
        """
        Check for chassis state.
        """
        try:
            chassis = self.get_object(self.CHASSIS_PATH,
                                      [self.CHASSIS_IFACE]).keys()[0]
            state = self.get_property(chassis, self.CHASSIS_PATH,
                                      self.CHASSIS_IFACE,
                                      'CurrentPowerState').split('.')[-1]
        except dbus.exceptions.DBusException:
            raise Exception('Unable to determine the chassis state!')

        if state != 'Off':
            raise Exception('Chassis state is not OFF ({})!'.format(state))


class Signature(object):
    """
    Digital signature verification.
    """

    # Enable/disable digital signature verification
    USE_VERIFICATION = True

    # Public key file (PEM format)
    # TODO: read data from EEPROM (BMC's VPD)
    PUBLIC_KEY = '/etc/yadro.public.pem'

    @staticmethod
    def verify(file, sign=None):
        """
        Check signature of specified firmware file.
        :param file: path to the file for verification
        :param sign: path to the signature file (digest)
        """
        if Signature.USE_VERIFICATION:
            try:
                if not sign:
                    sign = file + '.digest'
                subprocess.check_output(['/usr/bin/openssl', 'dgst', '-sha256',
                                         '-verify', Signature.PUBLIC_KEY,
                                         '-signature', sign,
                                         file], stderr=subprocess.STDOUT)
            except subprocess.CalledProcessError as e:
                raise Exception('Signature verification error ' + e.output)


class TaskTracker(object):
    """
    Task tracker.
    """

    def __init__(self, name):
        """
        Constructor.
        :param name: task name
        """
        self._done = False
        name += '...'
        sys.stdout.write(name)
        sys.stdout.write(' ' * (40 - len(name)))
        sys.stdout.flush()

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc_val, exc_tb):
        if exc_val:
            self.fail()
        else:
            self.success()

    def success(self):
        """
        Set task state as success.
        """
        if not self._done:
            print('[{}OK{}]'.format(CLR_SUCCESS, CLR_RESET))
            self._done = True

    def fail(self):
        """
        Set task state as failed.
        """
        if not self._done:
            print('[{}FAIL{}]'.format(CLR_ERROR, CLR_RESET))
            self._done = True


class FirmwareUpdate(object):
    """
    Firmware updater.
    """

    # Path to pflash utility
    PFLASH = '/usr/sbin/pflash'

    # Path to temporary files
    TMP_DIR = '/tmp/fwupdate'

    # Error code used for preinstall script
    EALREADY = 114

    def __init__(self, interactive, clean_install):
        """
        Constructor.
        :param interactive: flag to use interactive mode
                            (ask for user confirmation)
        :param clean_install: flag to perform clean installation
                              (reset all settings to manufacturing default)
        """
        self._interactive = interactive
        self._clean_install = clean_install

    @staticmethod
    def reset(interactive):
        """
        Reset all settings to manufacturing default.
        :param interactive: flag to use interactive mode
                            (ask for user confirmation)
        """
        if interactive:
            FirmwareUpdate._confirm(
                'All settings will be restored to manufacture default values.'
                '\nOpenBMC system will be rebooted automatically to apply '
                'changes.'
            )

        with TaskTracker('Lock PNOR access') as lock_task, PNORLock():
            lock_task.success()

            # OpenPOWER firmware reset
            FirmwareUpdate._execute('Clear NVRAM partition',
                                    FirmwareUpdate.PFLASH + ' -f -e -P NVRAM')
            FirmwareUpdate._execute('Clear GUARD partition',
                                    FirmwareUpdate.PFLASH + ' -f -c -P GUARD')
            FirmwareUpdate._execute('Clear DJVPD partition',
                                    FirmwareUpdate.PFLASH + ' -f -c -P DJVPD')
            FirmwareUpdate._execute('Clear HBEL partition',
                                    FirmwareUpdate.PFLASH + ' -f -c -P HBEL')

            # OpenBMC firmware reset
            with TaskTracker('Prepare temporary directory'):
                if not os.path.isdir(FirmwareUpdate.TMP_DIR):
                    os.mkdir(FirmwareUpdate.TMP_DIR)
            with TaskTracker('Create empty RW image'):
                # Get size of BMC RW partition
                with open('/sys/class/mtd/mtd5/size', 'r') as f:
                    rw_size = int(f.read())
                # Create empty image
                with open('/run/initramfs/image-rwfs', 'w') as f:
                    f.write('\xff' * rw_size)
            with TaskTracker('Clear white list'):
                open('/run/initramfs/whitelist', 'w').close()

        FirmwareUpdate._execute('Reboot BMC system', '/sbin/reboot')

    def update(self, fw_file):
        """
        Update firmware.
        :param fw_file: firmware package file
        """
        if not os.path.isfile(fw_file):
            raise Exception('Firmware package file not found: ' + fw_file)

        if self._interactive:
            title = 'OpenBMC and OpenPOWER firmwares will be updated.\n'
            if self._clean_install:
                title += 'All settings will be restored to manufacture '\
                         'default values.\n'
            title += 'OpenBMC system will be rebooted automatically to apply '\
                     'changes.\nPlease do not turn off the server during '\
                     'update!'
            FirmwareUpdate._confirm(title)

        with TaskTracker('Prepare temporary directory'):
            if os.path.isdir(FirmwareUpdate.TMP_DIR):
                shutil.rmtree(FirmwareUpdate.TMP_DIR)
            os.mkdir(FirmwareUpdate.TMP_DIR)

        with TaskTracker('Unpack firmware package'):
            fw_tar = tarfile.open(fw_file)
            fw_tar.extractall(FirmwareUpdate.TMP_DIR)
            fw_tar.close()

        # Prepare for update
        obmc_file = FirmwareUpdate.TMP_DIR + '/image-bmc'
        with TaskTracker('Check signature of ' + os.path.basename(obmc_file)):
            Signature.verify(obmc_file)
        obmc_preinstall, obmc_postinstall = FirmwareUpdate\
            ._prepare_customization('obmc.update')
        opfw_file = FirmwareUpdate.TMP_DIR + '/vesnin.pnor'
        with TaskTracker('Check signature of ' + os.path.basename(opfw_file)):
            Signature.verify(opfw_file)
        opfw_preinstall, opfw_postinstall = FirmwareUpdate\
            ._prepare_customization('opfw.update')

        with TaskTracker('Lock PNOR access') as lock_task, PNORLock():
            lock_task.success()

            # Update OpenPOWER firmware
            done = False
            if opfw_preinstall:
                done = self._pre_install(
                    opfw_preinstall, 'OpenPOWER', opfw_file)
            if not done:
                self._update_opfw(opfw_file)
            if opfw_postinstall:
                FirmwareUpdate._post_install(opfw_postinstall, 'OpenPOWER')

            # Update OpenBMC firmware
            done = False
            if obmc_preinstall:
                done = self._pre_install(obmc_preinstall, 'OpenBMC', obmc_file)
            if not done:
                self._update_obmc(obmc_file)

    def _update_obmc(self, fw_file):
        """
        Update OpenBMC firmware (write image).
        :param fw_file: firmware image file
        """
        FirmwareUpdate._execute('Prepare OpenBMC firmware image',
                                'mv -f {} /run/initramfs'.format(fw_file))
        if self._clean_install:
            with TaskTracker('Clear white list'):
                open('/run/initramfs/whitelist', 'w').close()
        FirmwareUpdate._execute('Reboot BMC system', '/sbin/reboot')

    def _update_opfw(self, fw_file):
        """
        Update OpenPOWER firmware (write image).
        :param fw_file: firmware image file
        """
        nvram_image = FirmwareUpdate.TMP_DIR + '/nvram.bin'
        if not self._clean_install:
            FirmwareUpdate._execute(
                'Preserve NVRAM configuration',
                FirmwareUpdate.PFLASH + ' -P NVRAM -r ' + nvram_image
            )

        print('Writing OpenPOWER firmware...')
        try:
            subprocess.call([FirmwareUpdate.PFLASH, '-E',
                             '-f', '-i', '-p', fw_file])
        except subprocess.CalledProcessError as e:
            raise Exception(e.output)

        if not self._clean_install:
            FirmwareUpdate._execute(
                'Recover NVRAM configuration',
                FirmwareUpdate.PFLASH + ' -f -e -P NVRAM -p ' + nvram_image
            )

    def _pre_install(self, cmd, title, fw_file):
        """
        Execute pre-install command.
        :param cmd: command to execute
        :param title: command title (firmware type)
        :param fw_file: firmware image file
        """
        print('Execute ' + title + ' pre-install...')
        rc = subprocess.call([
            cmd, fw_file,
            'clean' if self._clean_install else 'full',
            'interactive' if self._interactive else 'silent'
        ])
        if rc == 0:
            return False
        elif rc == FirmwareUpdate.EALREADY:
            return True
        else:
            raise Exception(
                'Error executing pre-install procedure ({})'.format(rc))

    @staticmethod
    def _post_install(cmd, title):
        """
        Execute post-install command.
        :param cmd: command to execute
        :param title: command title (firmware type)
        """
        print('Execute ' + title + ' post-install...')
        subprocess.call([cmd])

    @staticmethod
    def _prepare_customization(cname):
        """
        Prepare update customization procedures.
        :param cname: customization package name
        :return: tuple with pre- and post-install executable modules
        """
        pre_install = None
        post_install = None
        cust_package = FirmwareUpdate.TMP_DIR + '/' + cname
        if os.path.isfile(cust_package):
            with TaskTracker('Check signature of ' + cname):
                Signature.verify(cust_package)
            cust_path = FirmwareUpdate.TMP_DIR + '/upack_' + cname
            with TaskTracker('Prepare updater ' + cname):
                if not os.path.isdir(cust_path):
                    os.mkdir(cust_path)
            with TaskTracker('Unpack updater ' + cname):
                fw_tar = tarfile.open(cust_package)
                fw_tar.extractall(cust_path)
                fw_tar.close()
            pre_install_test = cust_path + '/preinstall'
            if os.path.isfile(pre_install_test):
                pre_install = pre_install_test
            post_install_test = cust_path + '/postinstall'
            if os.path.isfile(post_install_test):
                post_install = post_install_test
        return pre_install, post_install

    @staticmethod
    def _confirm(title, prompt='Do you want to continue?'):
        """
        Ask user for confirmation.
        :param title: message title
        :param prompt: prompt message
        """
        print('{}**************************************{}\n'
              '{}*             ATTENTION!             *{}\n'
              '{}**************************************{}'.format(
                  CLR_ERROR, CLR_RESET,
                  CLR_ERROR, CLR_RESET,
                  CLR_ERROR, CLR_RESET))
        print(title)
        resp = raw_input(prompt + ' [y/N] ')  # noqa
        if not re.match(r'^(y|yes)$', resp.strip().lower()):
            raise Exception('Aborted by user')

    @staticmethod
    def _execute(title, cmd):
        """
        Execute command.
        :param title: task title
        :param cmd: command to execute
        """
        with TaskTracker(title):
            try:
                subprocess.check_output(cmd.split(), stderr=subprocess.STDOUT)
            except subprocess.CalledProcessError as e:
                raise Exception(e.output)


def main():
    parser = argparse.ArgumentParser(
        description='Update OpenBMC/OpenPOWER firmware.')
    parser.add_argument('-f', '--file', action='store',
                        help='path to the firmware file')
    parser.add_argument('-r', '--reset', action='store_true', help=(
        'reset all settings to manufacturing default, '
        'this option can be combined with -f or used as '
        'standalone command to reset RW partition of '
        'OpenBMC and clean some partitions of the PNOR '
        'flash (such as NVRAM, GUARD, HBEL etc).'
    ))
    parser.add_argument('-l', '--no-lock', action='store_true',
                        help='disable PNOR flash access check/lock')
    parser.add_argument('-s', '--no-sign', action='store_true',
                        help='disable digital signature verification')
    parser.add_argument('-y', '--yes', action='store_true',
                        help='don\'t ask user for confirmation')
    parser.add_argument('-v', '--version', action='store_true',
                        help='print installed firmware version info and exit')
    args = parser.parse_args()
    try:
        PNORLock.USE_LOCK = not args.no_lock
        Signature.USE_VERIFICATION = not args.no_sign
        if args.version:
            VersionInfo().show()
        elif args.file:
            FirmwareUpdate(not args.yes, args.reset).update(args.file)
        elif args.reset:
            FirmwareUpdate.reset(not args.yes)
        else:
            raise Exception(
                'One or both of --file/--reset options must be specified')
        return 0
    except Exception as e:
        sys.stderr.write('{}{}{}\n'.format(CLR_ERROR, str(e), CLR_RESET))
        return -1
    except KeyboardInterrupt:
        sys.stderr.write(
            '\n{}Interrupted by user{}\n'.format(CLR_ERROR, CLR_RESET))
        return -1


if __name__ == '__main__':
    exit(main())
