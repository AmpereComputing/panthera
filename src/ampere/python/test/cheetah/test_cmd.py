# Copyright (c) 2025 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

import pickle
import unittest

from panthera.cheetah import Cmd, CmdPool, CmdSeq


class TestCmd(unittest.TestCase):
    def setUp(self):
        self.cmd = Cmd(cmd='echo "{greeting}"', greeting="Hello, world!")

    def test_contains(self):
        self.assertIn("greeting", self.cmd)
        self.assertNotIn("missing", self.cmd)

    def test_to_string(self):
        self.assertEqual(str(self.cmd), 'echo "Hello, world!"')


class TestCmdComposition(unittest.TestCase):
    def test_basic_rendering(self):
        c = Cmd(cmd='echo "{greeting}"', greeting="Hello, world!")
        self.assertEqual(c.render(), 'echo "Hello, world!"')

    def test_apply_dict(self):
        """
        Applying a list of dictionaries converts a Cmd to a CmdPool, containing
        one Cmd per dictionary.
        """
        c = Cmd(cmd='{exe} "{greeting}"')
        pool = c.apply([{"exe": "echo", "greeting": "Hello, world!"}, {"exe": "foo", "greeting": "bar"}])
        self.assertIsInstance(pool, CmdPool)
        renders = sorted([c.render() for c in pool.cmds])
        self.assertEqual(renders[0], 'echo "Hello, world!"')
        self.assertEqual(renders[1], 'foo "bar"')

    def test_if_set_present(self):
        c = Cmd(cmd=["something", Cmd.if_set("here", "present", "gone")], here="xxx")
        self.assertEqual(c.render(), "something present")

    def test_if_set_missing(self):
        c = Cmd(cmd=["something", Cmd.if_set("missing", "here", "gone")])
        self.assertEqual(c.render(), "something gone")

    def test_if_set_multiple_present(self):
        c = Cmd(cmd=["something", Cmd.if_set(["here", "also_here"], "present", "gone")], here="xxx", also_here="yyy")
        self.assertEqual(c.render(), "something present")

    def test_if_set_multiple_one_missing(self):
        c = Cmd(cmd=["something", Cmd.if_set(["here", "missing"], "present", "gone")], here="xxx")
        self.assertEqual(c.render(), "something gone")

    def test_pick_first(self):
        c = Cmd(
            cmd=[
                "something",
                Cmd.pick_first(
                    {
                        "here": "{here}",
                        "gone": "{gone}",
                    },
                    default_value="missing",
                ),
            ],
            here="present",
        )
        self.assertEqual(c.render(), "something present")

    def test_pick_first_other(self):
        c = Cmd(
            cmd=[
                "something",
                Cmd.pick_first(
                    {
                        "here": "{here}",
                        "gone": "{gone}",
                    },
                    default_value="missing",
                ),
            ],
            gone="else",
        )
        self.assertEqual(c.render(), "something else")

    def test_pick_first_missing(self):
        c = Cmd(
            cmd=[
                "something",
                Cmd.pick_first(
                    {
                        "here": "{here}",
                        "gone": "{gone}",
                    },
                    default_value="missing",
                ),
            ]
        )
        self.assertEqual(c.render(), "something missing")

    def test_join_paths(self):
        c = Cmd(cmd=["something", Cmd.join_paths("one", "{two}", "three", prefix="@")], two="red")
        self.assertEqual(c.render(), "something @one/red/three")

    def test_join_paths_absolute(self):
        c = Cmd(cmd=["something", Cmd.join_paths("one", "two", "/three")])
        self.assertEqual(c.render(), "something /three")

    def test_min_value(self):
        c = Cmd(cmd=["something", Cmd.min_value("one", "two", "three")], one=3, two=2, three=5)
        self.assertEqual(c.render(), "something 2")

    def test_min_value_default(self):
        c = Cmd(cmd=["something", Cmd.min_value("one", "two", "three", default_value=4)])
        self.assertEqual(c.render(), "something 4")

    def test_max_value(self):
        c = Cmd(cmd=["something", Cmd.max_value("one", "two", "three")], one=1, two=6, three=3)
        self.assertEqual(c.render(), "something 6")

    def test_append_prepend_cmd(self):
        cmd1 = Cmd(cmd='{exe} "{greeting}"', exe="echo", greeting="Hello, world!")
        cmd2 = Cmd(cmd="foo{bar}", bar="lish")

        for seq in [cmd1 + cmd2, cmd1.append(cmd2), cmd2.prepend(cmd1)]:
            self.assertIsInstance(seq, CmdSeq)
            renders = [c.render() for c in seq.cmds]
            self.assertEqual(renders[0], 'echo "Hello, world!"')
            self.assertEqual(renders[1], "foolish")

    def test_append_prepend_cmdseq(self):
        cmd1 = Cmd(cmd='{exe} "{greeting}"', exe="echo", greeting="Hello, world!")
        cmd2 = Cmd(cmd="foo{bar}", bar="lish")
        cmd3 = Cmd(cmd="whois {name}", name="me")

        seq12 = cmd1 + cmd2
        seq23 = cmd2 + cmd3
        self.assertIsInstance(seq12, CmdSeq)
        self.assertIsInstance(seq23, CmdSeq)

        for seq in [
            seq12 + cmd3,
            cmd1 + seq23,
            cmd1.append(seq23),
            seq12.append(cmd3),
            cmd3.prepend(seq12),
            seq23.prepend(cmd1),
        ]:
            self.assertIsInstance(seq, CmdSeq)
            renders = [c.render() for c in seq.cmds]
            self.assertEqual(renders[0], 'echo "Hello, world!"')
            self.assertEqual(renders[1], "foolish")
            self.assertEqual(renders[2], "whois me")

    def test_append_prepend_cmdpool(self):
        in_cmd = Cmd(cmd="{exe} {args}", exe="gunzip", args="input")
        cmd = Cmd(cmd='echo "{greeting}"')
        out_cmd = Cmd(cmd="gzip {f}", f="output")
        config = [{"greeting": "Hello, world!"}, {"greeting": "Adios"}]

        for pool in [
            cmd.apply(config).prepend(in_cmd).append(out_cmd),
            (in_cmd + cmd).apply(config).append(out_cmd),
            (in_cmd + cmd + out_cmd).apply(config),
            cmd.apply(config).append(out_cmd).prepend(in_cmd),
        ]:
            self.assertIsInstance(pool, CmdPool)
            renders = [[c.render() for c in seq] for seq in pool]

            self.assertEqual(renders[0][0], "gunzip input")
            self.assertEqual(renders[0][1], 'echo "Hello, world!"')
            self.assertEqual(renders[0][2], "gzip output")
            self.assertEqual(renders[1][0], "gunzip input")
            self.assertEqual(renders[1][1], 'echo "Adios"')
            self.assertEqual(renders[1][2], "gzip output")


class TestCmdCopy(unittest.TestCase):
    def setUp(self):
        self.cmd = Cmd(
            cmd=[
                "-cfg",
                Cmd.join_paths("one", "{two}", "three"),
                "-x",
                Cmd.if_set("blue", "{two}", "missing"),
            ],
            two="red",
        )
        self.copied = None

    def tearDown(self) -> None:
        self.assertEqual(self.cmd.render(), self.copied.render())

    def test_can_copy(self):
        self.copied = self.cmd.copy()

    def test_can_pickle(self):
        self.copied = pickle.loads(pickle.dumps(self.cmd))


class TestCmdSeq(unittest.TestCase):
    def setUp(self):
        def not_one(cmd):
            return cmd["number"] != 1

        self.cmdseq = CmdSeq([Cmd(cmd="echo {number}", number=i, only_if=not_one) for i in range(3)])

    def test_trim(self):
        trimmed = self.cmdseq.trim()
        self.assertEqual(len(trimmed), 2)
        self.assertEqual(list(map(str, trimmed)), ["echo 0", "echo 2"])
