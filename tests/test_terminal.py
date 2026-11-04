#!/usr/bin/env python3
"""Exercise the real game through a POSIX pseudoterminal; no pip packages."""
import errno
import fcntl
import os
from pathlib import Path
import pty
import re
import select
import signal
import struct
import subprocess
import sys
import tempfile
import termios
import time
import unittest
import unicodedata

BINARY = str(Path(sys.argv.pop(1) if len(sys.argv) > 1 else './burger').resolve())
ANSI = re.compile(rb'\x1b\[[0-?]*[ -/]*[@-~]')


class Session:
    def __init__(self, *args, size=(80, 24), color=False):
        self.master, self.slave = pty.openpty()
        self.resize(*size)
        self.original = termios.tcgetattr(self.slave)
        env = dict(os.environ)
        if color:
            env.pop('NO_COLOR', None)
        self.process = subprocess.Popen(
            [BINARY, *([] if color else ['--no-color']), '--seed', '42', *args],
            stdin=self.slave, stdout=self.slave, stderr=self.slave,
            start_new_session=True,
            env=env,
        )
        self.buffer = b''

    def resize(self, width, height):
        fcntl.ioctl(self.slave, termios.TIOCSWINSZ, struct.pack('HHHH', height, width, 0, 0))

    def send(self, text):
        os.write(self.master, text.encode() if isinstance(text, str) else text)

    def read(self, timeout=0.05):
        if select.select([self.master], [], [], timeout)[0]:
            try:
                chunk = os.read(self.master, 65536)
                self.buffer = (self.buffer + chunk)[-200000:]
            except OSError as error:
                if error.errno != errno.EIO:
                    raise
        return ANSI.sub(b'', self.buffer).decode('utf-8', errors='replace')

    def expect(self, text, timeout=3):
        until = time.monotonic() + timeout
        while time.monotonic() < until:
            output = self.read()
            if text in output:
                return output
            if self.process.poll() is not None:
                break
        raise AssertionError(f'Missing {text!r}: {self.read()[-4000:]}')

    def clear(self):
        self.read(0)
        self.buffer = b''

    def close(self):
        if self.process.poll() is None:
            self.process.send_signal(signal.SIGINT)
        until = time.monotonic() + 3
        while self.process.poll() is None and time.monotonic() < until:
            self.read()
        if self.process.poll() is None:
            self.process.kill()
        self.process.wait(timeout=3)
        restored = termios.tcgetattr(self.slave)
        os.close(self.master)
        os.close(self.slave)
        if restored != self.original:
            raise AssertionError('Terminal settings were not restored')
        if self.process.returncode != 0:
            raise AssertionError(f'Game exited with {self.process.returncode}: {self.buffer[-3000:]!r}')


class TerminalTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory(prefix='burger-pty-')
        self.ranking = Path(self.directory.name) / 'ranking.txt'
        self.sessions = []

    def tearDown(self):
        for session in self.sessions:
            session.close()
        self.directory.cleanup()

    def open(self, *args, **kwargs):
        session = Session('--ranking', str(self.ranking), *args, **kwargs)
        self.sessions.append(session)
        session.expect('H A M B U R G E R')
        return session

    def test_cli_and_demo(self):
        self.assertEqual(subprocess.run([BINARY, '--help'], capture_output=True).returncode, 0)
        for args in (['--duration', '0'], ['--duration', '-1'], ['--duration', '3601'],
                     ['--seed', '4294967296'], ['--duration'], ['--wat']):
            self.assertNotEqual(subprocess.run([BINARY, *args], capture_output=True).returncode, 0)
        self.assertNotEqual(subprocess.run([BINARY], stdin=subprocess.DEVNULL,
                                          capture_output=True).returncode, 0)
        demo = subprocess.check_output([BINARY, '--demo'])
        lines = ANSI.sub(b'', demo).decode().splitlines()
        self.assertEqual(len(lines), 24)
        self.assertIn('MY BURGER', ''.join(lines))

    def test_correct_order_undo_and_korean_ranking(self):
        session = self.open()
        session.clear()
        session.send('1')
        output = session.expect('주문 #001')
        digits = re.findall(r'\d+\. \[([1-7])\]', output)
        self.assertEqual(digits[:4], ['1', '2', '3', '7'])
        session.send('16u237s')
        session.expect('서빙 성공!')
        session.clear()
        session.send('q')
        session.expect('FINAL SCORE')
        session.send('김셰프X\x7f\r')
        session.expect('점수를 저장했습니다!')
        session.expect('김셰프')
        data = self.ranking.read_text()
        self.assertRegex(data, r'^\d+\t김셰프\n$')
        self.assertGreater(int(data.split('\t')[0]), 0)

    def test_wrong_orders_end_round_and_skip_save(self):
        session = self.open()
        session.send('1')
        session.expect('ORDER QUEUE')
        session.send('sss')
        session.expect('목숨을 모두 사용했습니다.')
        session.clear()
        session.send('\x1b')
        session.expect('H A M B U R G E R')
        self.assertFalse(self.ranking.exists())

    def test_timer_runs_without_input(self):
        session = self.open('--duration', '1')
        session.send('1')
        session.expect('ORDER QUEUE')
        session.expect('영업시간이 끝났습니다.', timeout=3)

    def test_resize_pauses_the_game(self):
        session = self.open('--duration', '2')
        session.send('1')
        session.expect('ORDER QUEUE')
        session.resize(60, 20)
        session.expect('크기 조절 중에는 시간이 멈춥니다.')
        until = time.monotonic() + 2.2
        while time.monotonic() < until:
            session.read()
        session.clear()
        session.resize(80, 24)
        session.expect('ORDER QUEUE')
        self.assertNotIn('SHIFT COMPLETE', session.read())
        session.expect('영업시간이 끝났습니다.', timeout=3)

    def test_save_failure_keeps_result(self):
        session = self.open('--ranking', str(Path(self.directory.name) / 'missing' / 'rank.txt'))
        session.send('1')
        session.expect('ORDER QUEUE')
        session.send('q')
        session.expect('FINAL SCORE')
        session.send('\r')
        session.expect('저장 실패:')
        self.assertFalse(self.ranking.exists())

    def test_arrows_help_and_terminal_cleanup(self):
        session = self.open()
        session.send('\x1b[B\x1b[B\r')
        session.expect('HOW TO PLAY')
        session.clear()
        session.send('\r')
        session.expect('H A M B U R G E R')
        session.send('2')
        session.expect('아직 기록이 없어요.')

    def test_pause_preserves_round_and_ingredients(self):
        session = self.open('--duration', '2')
        session.send('1')
        session.expect('ORDER QUEUE')
        session.send('12p')
        session.expect('PAUSED')
        until = time.monotonic() + 2.2
        while time.monotonic() < until:
            session.read()
        session.clear()
        session.send('p')
        output = session.expect('2 LAYERS')
        self.assertNotIn('SHIFT COMPLETE', output)
        session.expect('영업시간이 끝났습니다.', timeout=3)

    def test_feedback_survives_starting_the_next_burger(self):
        session = self.open()
        session.send('1')
        session.expect('ORDER QUEUE')
        session.send('1237s')
        session.expect('ORDER UP!')
        session.clear()
        session.send('1')
        session.expect('1 LAYERS')
        session.expect('서빙 성공!')

    def test_rich_layout_resize_and_full_stack(self):
        session = self.open(size=(96, 32), color=True)
        session.send('1')
        session.expect('ORDER QUEUE')
        session.send('123456712347')
        session.expect('12 LAYERS')
        self.assertIn(b'\x1b[48;2;', session.buffer)
        self.assertIn('▀'.encode(), session.buffer)
        session.clear()
        session.resize(80, 24)
        session.expect('12 LAYERS')
        # Entire frames must fit the terminal; CJK characters occupy two columns.
        frame = session.buffer.split(b'\x1b[H')[-1]
        text = ANSI.sub(b'', frame).decode('utf-8', errors='replace')
        lines = text.replace('\r', '').split('\n')
        for line in lines:
            columns = sum(2 if unicodedata.east_asian_width(c) in ('W', 'F') else 1 for c in line)
            self.assertLessEqual(columns, 80, line)
        session.send('u')
        session.expect('11 LAYERS')


if __name__ == '__main__':
    unittest.main(verbosity=2)
