# QEMU library
#
# Copyright (C) 2015-2016 Red Hat Inc.
# Copyright (C) 2012 IBM Corp.
#
# Authors:
#  Fam Zheng <famz@redhat.com>
#
# This work is licensed under the terms of the GNU GPL, version 2.  See
# the COPYING file in the top-level directory.
#
# Based on qmp.py.
#

import errno
import string
import os
import sys
import subprocess
import qmp.qmp


class QEMUMachine(object):
    '''A QEMU VM'''

    def __init__(self, binary, args=[], wrapper=[], name=None,
                 test_dir="/var/tmp", monitor_address=None,
                 socket_scm_helper=None, debug=False):
        '''
        Create a QEMUMachine object

        @param binary: path to the qemu binary (str)
        @param args: initial list of extra arguments
        @param wrapper: list of arguments used as prefix to qemu binary
        @param name: name of this object (used for log/monitor/... file names)
        @param test_dir: base location to put log/monitor/... files in
        @param monitor_address: custom address for QMP monitor
        @param socket_scm_helper: path to scm_helper binary (to forward fds)
        @param debug: enable debug mode (forwarded to QMP helper and such)
        @note: Qemu process is not started until launch() is used.
        '''
        if name is None:
            name = "qemu-%d" % os.getpid()
        if monitor_address is None:
            monitor_address = os.path.join(test_dir, name + "-monitor.sock")
        self._monitor_address = monitor_address
        self._qemu_log_path = os.path.join(test_dir, name + ".log")
        self._popen = None
        self._binary = binary
        self._args = list(args)     # Force copy args in case we modify them
        self._wrapper = wrapper
        self._events = []
        self._iolog = None
        self._socket_scm_helper = socket_scm_helper
        self._debug = debug
        self._qmp = None

    # This can be used to add an unused monitor instance.
    def add_monitor_telnet(self, ip, port):
        args = 'tcp:%s:%d,server,nowait,telnet' % (ip, port)
        self._args.append('-monitor')
        self._args.append(args)

    def add_fd(self, fd, fdset, opaque, opts=''):
        '''Pass a file descriptor to the VM'''
        options = ['fd=%d' % fd,
                   'set=%d' % fdset,
                   'opaque=%s' % opaque]
        if opts:
            options.append(opts)

        self._args.append('-add-fd')
        self._args.append(','.join(options))
        return self

    def send_fd_scm(self, fd_file_path):
        # In iotest.py, the qmp should always use unix socket.
        assert self._qmp.is_scm_available()
        if self._socket_scm_helper is None:
            print >>sys.stderr, "No path to socket_scm_helper set"
            return -1
        if os.path.exists(self._socket_scm_helper) is False:
            print >>sys.stderr, "%s does not exist" % self._socket_scm_helper
            return -1
        fd_param = ["%s" % self._socket_scm_helper,
                    "%d" % self._qmp.get_sock_fd(),
                    "%s" % fd_file_path]
        devnull = open('/dev/null', 'rb')
        proc = subprocess.Popen(fd_param, stdin=devnull, stdout=sys.stdout,
                                stderr=sys.stderr)
        return proc.wait()

    @staticmethod
    def _remove_if_exists(path):
        '''Remove file object at path if it exists'''
        try:
            os.remove(path)
        except OSError as exception:
            if exception.errno == errno.ENOENT:
                return
            raise

    def is_running(self):
        return self._popen and (self._popen.returncode is None)

    def exitcode(self):
        if self._popen is None:
            return None
        return self._popen.returncode

    def get_pid(self):
        if not self.is_running():
            return None
        return self._popen.pid

    def _load_io_log(self):
        with open(self._qemu_log_path, "r") as iolog:
            self._iolog = iolog.read()

    def _base_args(self):
        if isinstance(self._monitor_address, tuple):
            moncdev = "socket,id=mon,host=%s,port=%s" % (
                self._monitor_address[0],
                self._monitor_address[1])
        else:
            moncdev = 'socket,id=mon,path=%s' % self._monitor_address
        return ['-chardev', moncdev,
                '-mon', 'chardev=mon,mode=control',
                '-display', 'none', '-vga', 'none']

    def _pre_launch(self):
        self._qmp = qmp.qmp.QEMUMonitorProtocol(self._monitor_address,
                                                server=True, debug=self._debug)

    def _post_launch(self):
        self._qmp.accept()

    def _post_shutdown(self):
        if not isinstance(self._monitor_address, tuple):
            self._remove_if_exists(self._monitor_address)
        self._remove_if_exists(self._qemu_log_path)

    def launch(self):
        '''Launch the VM and establish a QMP connection'''
        devnull = open('/dev/null', 'rb')
        qemulog = open(self._qemu_log_path, 'wb')
        try:
            self._pre_launch()
            args = (self._wrapper + [self._binary] + self._base_args() +
                    self._args)
            self._popen = subprocess.Popen(args, stdin=devnull, stdout=qemulog,
                                           stderr=subprocess.STDOUT,
                                           shell=False)
            self._post_launch()
        except:
            if self.is_running():
                self._popen.kill()
                self._popen.wait()
            self._load_io_log()
            self._post_shutdown()
            raise

    def shutdown(self):
        '''Terminate the VM and clean up'''
        if self.is_running():
            try:
                self._qmp.cmd('quit')
                self._qmp.close()
            except:     # kill VM on any failure pylint: disable=W0702
                self._popen.kill()

            exitcode = self._popen.wait()
            if exitcode < 0:
                sys.stderr.write('qemu received signal %i: %s\n'
                                 % (-exitcode, ' '.join(self._args)))
            self._load_io_log()
            self._post_shutdown()

    underscore_to_dash = string.maketrans('_', '-')

    def qmp(self, cmd, conv_keys=True, **args):
        '''Invoke a QMP command and return the result dict'''
        qmp_args = dict()
        for key in args.keys():
            if conv_keys:
                qmp_args[key.translate(self.underscore_to_dash)] = args[key]
            else:
                qmp_args[key] = args[key]

        return self._qmp.cmd(cmd, args=qmp_args)

    def command(self, cmd, conv_keys=True, **args):
        '''
        Invoke a QMP command and on success report result dict or on failure
        raise exception with details.
        '''
        reply = self.qmp(cmd, conv_keys, **args)
        if reply is None:
            raise Exception("Monitor is closed")
        if "error" in reply:
            raise Exception(reply["error"]["desc"])
        return reply["return"]

    def get_qmp_event(self, wait=False):
        '''Poll for one queued QMP events and return it'''
        if len(self._events) > 0:
            return self._events.pop(0)
        return self._qmp.pull_event(wait=wait)

    def get_qmp_events(self, wait=False):
        '''Poll for queued QMP events and return a list of dicts'''
        events = self._qmp.get_events(wait=wait)
        events.extend(self._events)
        del self._events[:]
        self._qmp.clear_events()
        return events

    def event_wait(self, name, timeout=60.0, match=None):
        ''' Wait for event in QMP, optionally filter results by match. '''
        # Test if 'match' is a recursive subset of 'event'; skips branch
        # processing on match's value `None`
        #    {"foo": {"bar": 1} matches {"foo": None}
        def _event_match(event, match=None):
            if match is None:
                return True

            for key in match:
                if key in event:
                    if isinstance(event[key], dict):
                        if not _event_match(event[key], match[key]):
                            return False
                    elif event[key] != match[key]:
                        return False
                else:
                    return False

            return True

        # Search cached events
        for event in self._events:
            if (event['event'] == name) and _event_match(event, match):
                self._events.remove(event)
                return event

        # Poll for new events
        while True:
            event = self._qmp.pull_event(wait=timeout)
            if (event['event'] == name) and _event_match(event, match):
                return event
            self._events.append(event)

        return None

    def get_log(self):
        '''
        After self.shutdown or failed qemu execution this returns the output
        of the qemu process.
        '''
        return self._iolog
