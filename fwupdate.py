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

    SYSTEMD_BUS = 'org.freedesktop.systemd1'
    SYSTEMD_PATH = '/org/freedesktop/systemd1'
    SYSTEMD_IFACE = 'org.freedesktop.systemd1.Manager'

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

    def start_unit(self, unit_name):
        """
        Start systemd unit
        :params unit_name: Systemd unit name
        """
        self._bus.call_blocking(self.SYSTEMD_BUS, self.SYSTEMD_PATH,
                                self.SYSTEMD_IFACE, 'StartUnit', 'ss',
                                [unit_name, 'replace'])


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


class FirmwareLock(DbusClient):
    """
    Firmware flash access lock.
    """

    # Enable/disable locking
    USE_PNOR_LOCK = True

    # Lock file path
    LOCK_FILE_PATH = '/var/lock/fwupdate.lock'

    CHASSIS_PATH = '/xyz/openbmc_project/state/chassis0'
    CHASSIS_IFACE = 'xyz.openbmc_project.State.Chassis'

    HIOMAPD_PATH = '/xyz/openbmc_project/Hiomapd'
    HIOMAPD_IFACE = 'xyz.openbmc_project.Hiomapd.Control'

    def __init__(self, bus=None):
        DbusClient.__init__(self, bus)
        self._lock_file = None
        self._hiomapd = dbus.Interface(
            self._bus.get_object(
                self.get_object(self.HIOMAPD_PATH,
                                [self.HIOMAPD_IFACE]).keys()[0],
                self.HIOMAPD_PATH),
            self.HIOMAPD_IFACE)

    def __enter__(self):
        self.lock()
        return self

    def __exit__(self, exc_type, exc_val, exc_tb):
        self.unlock()

    def lock(self):
        """
        Enable firmware drives guard
        """
        try:
            self._hiomapd_suspend()
            self._pnor_lock()
            self._check_chassis_state()
        except Exception as err:
            self.unlock()
            raise Exception('Lock firmware flash access failed: ' + str(err))

    def unlock(self):
        """
        Disable firmware drives guard
        """
        self._pnor_unlock()
        self._hiomapd_resume()
        self._reboot_unlock()

    def _reboot_lock(self):
        """
        Prevents the BMC reboot.
        """
        self.start_unit('reboot-guard-enable.service')

    def _reboot_unlock(self):
        """
        Resumes the BMC reboot possibility.
        """
        try:
            self.start_unit('reboot-guard-disable.service')
        except Exception:
            pass

    def _hiomapd_state(self):
        """
        Returns current state of the hiomapd.
        """
        return self.get_property(self._hiomapd.bus_name,
                                 self._hiomapd.object_path,
                                 self.HIOMAPD_IFACE, 'DaemonState')

    def _hiomapd_suspend(self):
        """
        Suspends hiomapd work
        """
        assert(self._hiomapd_state() == 0)
        self._hiomapd.Suspend()

    def _hiomapd_resume(self):
        """
        Resumes the hiomapd work
        """
        try:
            if self._hiomapd_state() != 0:
                self._hiomapd.Resume(True)
        except dbus.exceptions.DBusException:
            pass

    def _pnor_lock(self):
        """
        Check and lock access to the PNOR flash.
        """
        assert(self._lock_file is None)
        if self.USE_PNOR_LOCK:
            self._lock_file = open(self.LOCK_FILE_PATH, 'w')
            fcntl.lockf(self._lock_file, fcntl.LOCK_EX | fcntl.LOCK_NB)
            self._check_pflash()

    def _pnor_unlock(self):
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

    SYSTEM_KEYS_DIRECTORY = '/etc/activationdata'

    def __init__(self):
        self._folder = None
        self._keyfile = None
        self._hashfunc = None

    @staticmethod
    def _get_value(filename, tag):
        """
        Read the specified tag value from the file.
        """
        with open(filename, 'r') as stream:
            for line in stream.readlines():
                key, val = line.split('=')
                if key == tag:
                    return val.strip()
            raise Exception('{} not found in {}!'.format(tag, filename))

    @staticmethod
    def _make_filename(*args):
        """
        Build a path to the file using os.path.join()
        and ensure that the file exists.
        """
        filename = os.path.join(*args)
        if not os.path.isfile(filename):
            raise Exception('File {} not found!'.format(filename))
        return filename

    @staticmethod
    def _verify_file(filename, keyname, hashfunc):
        """
        Verify signature of specified file.
        :param filename: path to the file for verification
        :param keyname: path to the public key
        :param hashfunc: signature hash function
        """
        try:
            subprocess.check_output(['/usr/bin/openssl', 'dgst',
                                     '-' + hashfunc,
                                     '-verify', keyname,
                                     '-signature', filename + '.sig',
                                     filename], stderr=subprocess.STDOUT)
        except subprocess.CalledProcessError as err:
            raise Exception('Verification of {} failed: {}'.format(filename,
                                                                   err.output))

    def system_level_verify(self, folder):
        """
        Function used for system level file signature validation of image
        specific publickey file and manifest file using the available public
        keys and hash functions in the system.
        :param manifest: path to the manifest file.
        :param publickey: path to the image specific public key file.
        """
        manifest = self._make_filename(folder, 'MANIFEST')
        publickey = self._make_filename(folder, 'publickey')
        valid = False

        for keytype in os.listdir(Signature.SYSTEM_KEYS_DIRECTORY):
            try:
                keyfile = Signature._make_filename(
                    Signature.SYSTEM_KEYS_DIRECTORY, keytype, 'publickey')
                hashfn = Signature._make_filename(
                    Signature.SYSTEM_KEYS_DIRECTORY, keytype, 'hashfunc')
                hashfunc = Signature._get_value(hashfn, 'HashType')

                Signature._verify_file(manifest, keyfile, hashfunc)
                Signature._verify_file(publickey, keyfile, hashfunc)
                valid = True
                break
            except Exception:
                pass

        if valid:
            self._folder = folder
            self._keyfile = publickey
            self._hashfunc = self._get_value(manifest, 'HashType')
        else:
            raise Exception('System level verification failed!')

    def verify(self, filename):
        assert(self._folder)
        assert(self._keyfile)
        assert(self._hashfunc)

        self._verify_file(self._make_filename(filename), self._keyfile,
                          self._hashfunc)


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

    def __init__(self, interactive, clean_install=False):
        """
        Constructor.
        :param interactive: flag to use interactive mode
                            (ask for user confirmation)
        :param clean_install: flag to perform clean installation
                              (reset all settings to manufacturing default)
        """
        self._interactive = interactive
        self._clean_install = clean_install
        self._validator = Signature()

    def _get_parts_to_clean(self):
        PARTS = re.compile(r'^ID=\d+\s+(\w+)\s.*\[([^\]]+)\]$')
        parts = dict()
        try:
            output = subprocess.check_output([self.PFLASH, '-i'],
                                             stderr=subprocess.STDOUT)
            for ln in output.splitlines():
                match = PARTS.match(ln)
                if not match or 'F' not in match.group(2):
                    continue

                ecc = 'C' in match.group(2) or 'E' in match.group(2)
                parts.update({match.group(1): '-c' if ecc else '-e'})

        except subprocess.CalledProcessError:
            raise Exception('Unable to get list of OpenPOWER partitions')

        return parts

    def reset(self):
        """
        Reset all settings to manufacturing default.
        :param interactive: flag to use interactive mode
                            (ask for user confirmation)
        """

        if self._interactive:
            FirmwareUpdate._confirm(
                'All settings will be restored to manufacture default values.'
                '\nOpenBMC system will be rebooted automatically to apply '
                'changes.'
            )

        # Single D-bus connection
        bus = dbus.SystemBus()

        with TaskTracker('Lock PNOR access') as lock_task, FirmwareLock(bus):
            lock_task.success()

            # OpenPOWER firmware reset
            for part, ecc_opt in self._get_parts_to_clean().items():
                self._execute('Clear {} partition'.format(part),
                              self.PFLASH + ' -f ' + ecc_opt + ' -P  ' + part)

            # OpenBMC firmware reset
            with TaskTracker('Enable the BMC clean'):
                DbusClient(bus).start_unit(
                    'obmc-flash-bmc-setenv@'
                    'openbmconce\\x3dfactory\\x2dreset'
                    '.service')

        self._execute('Reboot BMC system', '/sbin/reboot')

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
            self._confirm(title)

        with TaskTracker('Prepare temporary directory'):
            if os.path.isdir(self.TMP_DIR):
                shutil.rmtree(self.TMP_DIR)
            os.mkdir(self.TMP_DIR)

        with TaskTracker('Unpack firmware package'):
            fw_tar = tarfile.open(fw_file)
            fw_tar.extractall(self.TMP_DIR)
            fw_tar.close()

        if self._validator.USE_VERIFICATION:
            with TaskTracker('Check signature of firmware package'):
                self._validator.system_level_verify(self.TMP_DIR)

            # Prepare for update
            obmc_file = os.path.join(self.TMP_DIR, 'image-bmc')
            self._verify(obmc_file)
            obmc_preinstall, obmc_postinstall = self\
                ._prepare_customization('obmc.update')

            opfw_file = os.path.join(self.TMP_DIR, 'vesnin.pnor')
            self._verify(opfw_file)
            opfw_preinstall, opfw_postinstall = self\
                ._prepare_customization('opfw.update')

        with TaskTracker('Lock PNOR access') as lock_task, FirmwareLock():
            lock_task.success()

            # Update OpenPOWER firmware
            done = False
            if opfw_preinstall:
                done = self._pre_install(
                    opfw_preinstall, 'OpenPOWER', opfw_file)
            if not done:
                self._update_opfw(opfw_file)
            if opfw_postinstall:
                self._post_install(opfw_postinstall, 'OpenPOWER')

            # Update OpenBMC firmware
            done = False
            if obmc_preinstall:
                done = self._pre_install(obmc_preinstall, 'OpenBMC', obmc_file)
            if not done:
                self._update_obmc(obmc_file)

    def _verify(self, fname):
        if self._validator.USE_VERIFICATION:
            with TaskTracker('Check signature of ' + os.path.basename(fname)):
                self._validator.verify(fname)

    def _update_obmc(self, fw_file):
        """
        Update OpenBMC firmware (write image).
        :param fw_file: firmware image file
        """
        self._execute('Prepare OpenBMC firmware image',
                      'mv -f {} /run/initramfs'.format(fw_file))
        if self._clean_install:
            with TaskTracker('Clear white list'):
                open('/run/initramfs/whitelist', 'w').close()
        self._execute('Reboot BMC system', '/sbin/reboot')

    def _update_opfw(self, fw_file):
        """
        Update OpenPOWER firmware (write image).
        :param fw_file: firmware image file
        """
        nvram_image = os.path.join(self.TMP_DIR, 'nvram.bin')
        if not self._clean_install:
            self._execute('Preserve NVRAM configuration',
                          self.PFLASH + ' -P NVRAM -r ' + nvram_image)

        print('Writing OpenPOWER firmware...')
        try:
            subprocess.call([self.PFLASH, '-E', '-f', '-i', '-p', fw_file])
        except subprocess.CalledProcessError as e:
            raise Exception(e.output)

        if not self._clean_install:
            self._execute('Recover NVRAM configuration',
                          self.PFLASH + ' -f -e -P NVRAM -p ' + nvram_image)

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
        elif rc == self.EALREADY:
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

    def _prepare_customization(self, cname):
        """
        Prepare update customization procedures.
        :param cname: customization package name
        :return: tuple with pre- and post-install executable modules
        """
        pre_install = None
        post_install = None
        cust_package = os.path.join(self.TMP_DIR, cname)
        if os.path.isfile(cust_package):
            self._verify(cust_package)
            cust_path = os.path.join(self.TMP_DIR, 'upack_' + cname)
            with TaskTracker('Prepare updater ' + cname):
                if not os.path.isdir(cust_path):
                    os.mkdir(cust_path)
            with TaskTracker('Unpack updater ' + cname):
                fw_tar = tarfile.open(cust_package)
                fw_tar.extractall(cust_path)
                fw_tar.close()
            pre_install_test = os.path.join(cust_path, 'preinstall')
            if os.path.isfile(pre_install_test):
                pre_install = pre_install_test
            post_install_test = os.path.join(cust_path, 'postinstall')
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
        FirmwareLock.USE_PNOR_LOCK = not args.no_lock
        Signature.USE_VERIFICATION = not args.no_sign
        if args.version:
            VersionInfo().show()
        elif args.file:
            FirmwareUpdate(not args.yes, args.reset).update(args.file)
        elif args.reset:
            FirmwareUpdate(not args.yes).reset()
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
