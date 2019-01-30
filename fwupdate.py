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
import sys
import subprocess
import tarfile

# Terminal colors
if sys.stdout.isatty():
    CLR_ERROR = '\033[31;1m'  # red
    CLR_SUCCESS = '\033[32m'  # green
    CLR_RESET = '\033[0m'
else:
    CLR_ERROR = ''
    CLR_SUCCESS = ''
    CLR_RESET = ''


class VersionInfo(object):
    """
    Firmware version.
    """

    # File used to store OpenPOWER firmware version information (cache file)
    OPFW_CACHE_FILE = '/var/lib/obmc/opfw.version'

    @staticmethod
    def show():
        """
        Print installed firmware version info.
        """
        # Get and print OpenBMC firmware version
        print('OpenBMC firmware: ' + VersionInfo.obmc())
        # Get and print OpenPOWER firmware version
        version = VersionInfo.opfw()
        title = 'OpenPOWER firmware: '
        print(title + version[0].strip())
        for v in version[1:]:
            print(' ' * len(title) + v.strip())

    @staticmethod
    def obmc():
        """
        Get OpenBMC firmware version.
        :return: OpenBMC firmware version
        """
        version = 'N/A'
        try:
            rx = re.compile(r'VERSION_ID="([^"]+)"')
            with open('/etc/os-release', 'r') as f:
                for s in f.readlines():
                    match = rx.search(s)
                    if match:
                        version = match.group(1)
                        break
        except:
            pass  # Ignore all errors
        return version

    @staticmethod
    def opfw():
        """
        Get OpenPOWER firmware version.
        :return: array with OpenPOWER firmware components versions
        """
        version = VersionInfo._read_from_file()
        if not version:
            version = VersionInfo._read_from_pnor()
            if version:
                VersionInfo._save_to_file(version)
            else:
                version = ['N/A']
        return version

    @staticmethod
    def _read_from_file():
        """
        Read OpenPOWER firmware version info from cache file.
        :return: array with OpenPOWER firmware components versions
        """
        if os.path.isfile(VersionInfo.OPFW_CACHE_FILE):
            try:
                with open(VersionInfo.OPFW_CACHE_FILE, 'r') as f:
                    version = f.readlines()
                return version
            except:
                pass  # Ignore all errors

    @staticmethod
    def _save_to_file(version):
        """
        Save OpenPOWER firmware version info to the cache file.
        :param version: array with OpenPOWER firmware components versions
        """
        try:
            with open(VersionInfo.OPFW_CACHE_FILE, 'w') as f:
                for v in version:
                    f.write(v.strip() + '\n')
        except:
            pass  # Ignore all errors

    @staticmethod
    def _read_from_pnor():
        """
        Read OpenPOWER firmware version info from PNOR flash partition.
        :return: array with OpenPOWER firmware components versions
        """
        try:
            with PNORLock():
                dump = '/tmp/fwupdate.version.dump'
                subprocess.check_output([FirmwareUpdate.PFLASH, '-P', 'VERSION', '-r', dump])
                with open(dump, 'r') as f:
                    raw = f.read()
                os.remove(dump)
                return filter(None, [i.strip() for i in raw.split('\n')])
        except:
            pass  # Ignore all errors


class PNORLock(object):
    """
    PNOR flash access lock.
    """

    # Enable/disable locking
    USE_LOCK = True

    # Lock file path
    LOCK_FILE_PATH = '/var/lock/fwupdate.lock'

    def __init__(self):
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
            self._lock_file = open(PNORLock.LOCK_FILE_PATH, 'w')
            fcntl.lockf(self._lock_file, fcntl.LOCK_EX | fcntl.LOCK_NB)
            PNORLock._check_pflash()
            PNORLock._check_host_state()
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
                os.remove(PNORLock.LOCK_FILE_PATH)
            except:
                pass  # Ignore all errors

    @staticmethod
    def _check_pflash():
        """
        Check for running pflash utility.
        """
        if subprocess.call(['/bin/pidof', 'pflash']) == 0:
            raise Exception('pflash is running')

    @staticmethod
    def _check_host_state():
        """
        Check for host state.
        """
        try:
            dbus_out = subprocess.check_output(['/usr/bin/busctl', '--no-pager', 'call',
                                                'xyz.openbmc_project.State.Host',
                                                '/xyz/openbmc_project/state/host0',
                                                'org.freedesktop.DBus.Properties',
                                                'Get', 'ss', 'xyz.openbmc_project.State.Host',
                                                'CurrentHostState'], stderr=subprocess.STDOUT)
        except subprocess.CalledProcessError as e:
            raise Exception(e.output)
        match = re.compile(r'.*Host\.HostState\.([^"]+)').search(dbus_out)
        if not match:
            raise Exception('Host state is undefined')
        state = match.group(1)
        if state != 'Off':
            raise Exception('Host is not OFF (' + state + ')')


class Signature(object):
    """
    Digital signature verification.
    """

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
        if not self._done:
            print('[{}OK{}]'.format(CLR_SUCCESS, CLR_RESET))
            self._done = True

    def fail(self):
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

    # Size of BMC RW partition (4 MiB)
    BMC_RW_SIZE = 4 * 1024 * 1024

    @staticmethod
    def reset(interactive):
        """
        Reset all settings to manufacturing default.
        :param interactive: flag to use interactive mode (ask for user confirmation)
        """
        if interactive:
            title = 'All settings will be restored to manufacture default values.\n' + \
                    'OpenBMC system will be rebooted automatically to apply changes.'
            FirmwareUpdate._confirm(title)

        # OpenPOWER firmware reset
        FirmwareUpdate._execute('Clear NVRAM partition', FirmwareUpdate.PFLASH + ' -f -e -P NVRAM')
        FirmwareUpdate._execute('Clear GUARD partition', FirmwareUpdate.PFLASH + ' -f -c -P GUARD')
        FirmwareUpdate._execute('Clear DJVPD partition', FirmwareUpdate.PFLASH + ' -f -c -P DJVPD')
        FirmwareUpdate._execute('Clear HBEL partition', FirmwareUpdate.PFLASH + ' -f -c -P HBEL')

        # OpenBMC firmware reset
        with TaskTracker('Prepare temporary directory'):
            if not os.path.isdir(FirmwareUpdate.TMP_DIR):
                os.mkdir(FirmwareUpdate.TMP_DIR)
        with TaskTracker('Create empty RW image'):
            with open('/run/initramfs/image-rwfs', 'w') as f:
                f.write('\xff' * FirmwareUpdate.BMC_RW_SIZE)
        with TaskTracker('Clear white list'):
            open('/run/initramfs/whitelist', 'w').close()
        FirmwareUpdate._execute('Reboot BMC system', '/sbin/reboot')

    @staticmethod
    def update(fw_file, interactive, check_sign, clean_install):
        """
        Update firmware.
        :param fw_file: firmware package file
        :param interactive: flag to use interactive mode (ask for user confirmation)
        :param check_sign: flag to use signature verification
        :param clean_install: flag to perform clean installation (reset all settings to manufacturing default)
        """
        if not os.path.isfile(fw_file):
            raise Exception('Firmware package file not found: ' + fw_file)

        if interactive:
            title = 'OpenBMC and OpenPOWER firmwares will be updated.\n'
            if clean_install:
                title += 'All settings will be restored to manufacture default values.\n'
            title += 'OpenBMC system will be rebooted automatically to apply changes.\n'
            title += 'Please do not turn off the server during update!'
            FirmwareUpdate._confirm(title)

        with TaskTracker('Prepare temporary directory'):
            if not os.path.isdir(FirmwareUpdate.TMP_DIR):
                os.mkdir(FirmwareUpdate.TMP_DIR)

        with TaskTracker('Unpack firmware package'):
            fw_tar = tarfile.open(fw_file)
            fw_tar.extractall(FirmwareUpdate.TMP_DIR)
            fw_tar.close()

        obmc_image = 'image-bmc'
        opfw_image = 'vesnin.pnor'
        obmc_file = FirmwareUpdate.TMP_DIR + '/' + obmc_image
        opfw_file = FirmwareUpdate.TMP_DIR + '/' + opfw_image

        if check_sign:
            with TaskTracker('Check signature of ' + obmc_image):
                Signature.verify(obmc_file)
            with TaskTracker('Check signature of ' + opfw_image):
                Signature.verify(opfw_file)

        with TaskTracker('Lock PNOR access') as lock_task, PNORLock():
            lock_task.success()
            FirmwareUpdate._update_opfw(opfw_file, clean_install)
            FirmwareUpdate._update_obmc(obmc_file, clean_install)

    @staticmethod
    def _update_obmc(fw_file, clean_install):
        """
        Update OpenBMC firmware.
        :param fw_file: firmware file
        :param clean_install: flag to perform clean installation (reset all settings to manufacturing default)
        """
        FirmwareUpdate._execute('Prepare OpenBMC firmware image', 'mv -f {} /run/initramfs'.format(fw_file))
        if clean_install:
            with TaskTracker('Clear white list'):
                open('/run/initramfs/whitelist', 'w').close()
        FirmwareUpdate._execute('Reboot BMC system', '/sbin/reboot')

    @staticmethod
    def _update_opfw(fw_file, clean_install):
        """
        Update OpenPOWER firmware.
        :param fw_file: firmware file
        :param clean_install: flag to perform clean installation (reset all settings to manufacturing default)
        """
        nvram_image = FirmwareUpdate.TMP_DIR + '/nvram.bin'
        if not clean_install:
            FirmwareUpdate._execute('Preserve NVRAM configuration',
                                    FirmwareUpdate.PFLASH + ' -P NVRAM -r ' + nvram_image)

        print('Writing OpenPOWER firmware...')
        try:
            subprocess.call([FirmwareUpdate.PFLASH, '-E', '-f', '-i', '-p', fw_file])
        except subprocess.CalledProcessError as e:
            raise Exception(e.output)

        if not clean_install:
            FirmwareUpdate._execute('Recover NVRAM configuration',
                                    FirmwareUpdate.PFLASH + ' -f -e -P NVRAM -p ' + nvram_image)

    @staticmethod
    def _confirm(title, prompt='Do you want to continue?'):
        """
        Ask user for confirmation.
        :param title: message title
        :param prompt: prompt message
        """
        print('{}**************************************{}'.format(CLR_ERROR, CLR_RESET))
        print('{}*             ATTENTION!             *{}'.format(CLR_ERROR, CLR_RESET))
        print('{}**************************************{}'.format(CLR_ERROR, CLR_RESET))
        print(title)
        resp = raw_input(prompt + ' [y/N] ')
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
    parser = argparse.ArgumentParser(description='Update OpenBMC/OpenPOWER firmware.')
    parser.add_argument('-f', '--file', action='store', help='path to the firmware file')
    parser.add_argument('-r', '--reset', action='store_true', help='reset all settings to manufacturing default, ' +
                                                                   'this option can be combined with -f or used as ' +
                                                                   'standalone command to reset RW partition of ' +
                                                                   'OpenBMC and clean some partitions of the PNOR ' +
                                                                   'flash (such as NVRAM, GUARD, HBEL etc).')
    parser.add_argument('-l', '--no-lock', action='store_true', help='disable PNOR flash access check/lock')
    parser.add_argument('-s', '--no-sign', action='store_true', help='disable digital signature verification')
    parser.add_argument('-y', '--yes', action='store_true', help='don\'t ask user for confirmation')
    parser.add_argument('-v', '--version', action='store_true', help='print installed firmware version info and exit')
    args = parser.parse_args()
    try:
        PNORLock.USE_LOCK = not args.no_lock
        if args.version:
            VersionInfo.show()
        elif args.file:
            FirmwareUpdate.update(args.file, not args.no_sign, not args.yes, args.reset)
        elif args.reset:
            FirmwareUpdate.reset(not args.yes)
        else:
            raise Exception('One or both of --file/--reset options must be specified')
        return 0
    except Exception as e:
        sys.stderr.write('{}{}{}\n'.format(CLR_ERROR, str(e), CLR_RESET))
        return -1
    except KeyboardInterrupt:
        sys.stderr.write('\n{}Interrupted by user{}\n'.format(CLR_ERROR, CLR_RESET))
        return -1


if __name__ == '__main__':
    exit(main())
