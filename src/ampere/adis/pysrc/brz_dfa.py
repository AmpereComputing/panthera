#!/usr/bin/env python3
#
# Copyright (c) 2022 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause
#

# Brzozowski derivative of regular expressions

from __future__ import annotations
import collections
import random
import sys

# epsilon string - out of input
# null state: when derivative is nothing

# peel('1', set(['1'])) = set([])  an accepting state, out of input
# peel('1', set(['0'])) = None     an error state

# peel('1', set(['--'])) = set(['-'])  remove one dontcare
# peel('0', set(['--'])) = set(['-'])  remove one dontcare

# we denote "!=00000 as !5:00000
# peel('1', set(['!5:00000'])) = set(['----'])  4 dontcares
# peel('0', set(['!5:00000'])) = set(['!4:0000']) 4 !=0000

# peel('1', set(['!3:110'])) = set(['!2:10'])
# peel('0', set(['!3:110'])) = set(['--'])

def _peel_set(c:int, x:set[str]) -> set([str]):
    """c is the character we are taking the derivative with respect
    to, a 0 or 1.  x is the regular expression, as set of binary-ish
    bit strings. The regular expression can have 1, 0 or
    dontcares("-") and not-equals constraints. The not-equals
    constraints are denoted pwith "!<num>:[01]+" where <num> is a
    number of digits in the not-equals constriant."""
    def rest_are_dont_cares(n,s):
        return s[:n] == '-'*n

    n = set([])
    found = False
    for l in x: # go through all the OR conditions
        peeled = l[0]
        if peeled == c or peeled == '-':
            found = True
            if len(l) > 1:
                n.add(l[1:])
        elif peeled == '!': # not-equals cases... !<num>:[01]+
            nbits = int(l[1]) # <num>
            assert nbits < 10
            first = l[3]  # first bit after colon.
            if first == c: # c matches first char of not-equals pattern
                if nbits > 1: # peel off a bit, continue not-equals sequence (if not all dont-cares)
                    if rest_are_dont_cares(nbits-1,l[4:]):
                        continue # no match... this pattern failed
                    elif l[4] == '-': # push through negation to subsequent bit
                        n.add(f'-!{nbits-2}:{l[5:]}')
                    else:
                        found = True
                        n.add(f'!{nbits-1}:{l[4:]}')
                else:
                    continue # no match
            else: #  c does not match -> convert rest to dontcares or accept
                found = True
                nlen = len(l)
                if nbits > 1:
                    s = '-'*(nbits-1) # dontcares
                    if nlen > nbits+3:
                        s += l[nbits+3:] # rest of string if exissts
                    n.add(s)
                elif nlen > 4: # reached end of notequals, take rest of string if any
                    n.add( l[4:] )

    if found:
        return n
    return None # error state, not-accepting (null state)

class dfa_state_t:
    def __init__(self, name:int):
        """The there is no next state for an input, then that's an error/not-accept/undefined edge. """
        self.name:int = name
        self.final: bool = False
        self.iid: int = -1
        self.next_states: list[int] = [-1,-1]
        self.group_next_state: list[int] = None
        self.renumbered_node: int = 0
    def add_next(self, input: int, next:int):
        self.next_states[input]=next
    def get_next(self, input: int):
        return self.next_states[input]

    def add_next_group(self, input: int, next:int):
        assert next != 0
        if self.group_next_state is None:
            self.group_next_state = [0]*256
        self.group_next_state[input]=next

    def set_final(self, iid:int):
        self.final=True
        self.iid = iid


def _build_dfa(r) -> (dict[frozenset,int], list[tuple]):
    def done(s):
        if len(s) == 0: # handles "fake" stateless testing
            return True
        for x in s:
            if x[0] == '@':
                return True
        return False

    q = collections.deque()
    fr = frozenset(r)
    q.appendleft(fr)
    states = { fr:0 }
    nstates = 1
    edges = []

    while q:
        #print('Q',q)
        #print('STATES', states)
        x = q.pop()
        if done(x):
            edges.append((x,'NOTHING','FINAL'))
        else:
            #print("\tX",x)
            for c in ['0','1']:
                y = _peel_set(c,x)
                #print(f"\t\tPeel {c} -> Y {y}")
                if y is None: # error, null state, not recognized
                    if y not in states:
                        states[None] = nstates
                        nstates += 1
                    edges.append((x,c,None))
                else:
                    yf = frozenset(y)
                    if yf not in states:
                        q.appendleft(yf)
                        states[yf] = nstates
                        nstates += 1
                    edges.append((x,c,yf))

    return states, edges
def _check_dfa(edges: list[tuple]) -> int:
    """Check if there is more than one instruction at every leaf of
    the graph. if more than one instr, decoding failed. That usually
    means we must augment the patterns like we do for HINT and
    MSR_imm    """
    def _check(cs:frozenset):
        if len(cs) != 1:
            print("DFA ERROR:", cs)
            return False
        return True

    errors = 0
    for cs,c,ns in edges:
        if c == 'NOTHING':
            if not _check(cs):
                errors += 1
    return errors


def _build_dfa_postprocess(states:dict[frozenset,int], edges: list[tuple]) -> list[dfa_state_t]:
    """Return a list of dfs_state_t objects that can be used for graph traversal"""
    def _get_instr_state_id(cs:frozenset):
        if len(cs) != 1:
            print(cs)
        assert len(cs) == 1  # check if decoding worked...

        for a in cs:
            if a[0] == '@':
                return int(a[1:])
        assert 0
        return None

    nstates = len(states)
    ostates = [dfa_state_t(i) for i in range(nstates)]
    # recode edges using numeric states
    for cs,c,ns in edges:
        ncs = states[cs] # get a numeric state number from symbolic state
        css = ostates[ncs] # get a state node
        if c == 'NOTHING':
            css.set_final(_get_instr_state_id(cs))
        elif ns is not None:
            nns = states[ns]
            if c in ['0','1']:
                css.add_next(int(c), nns)

    return ostates


def group_transitions(ostates):
    mx = 256
    nbits = 8
    chunks = 4

    node = 0
    next_starting_points = set([])
    next_starting_points.add(node)

    for c in range(chunks):
        if 0:
            print(f"GROUP WALK c={c} starting={len(next_starting_points)}")
        starting_points = next_starting_points.copy()
        next_starting_points = set([])
        while starting_points:
            node = starting_points.pop()
            for v in range(mx): # all the byte values
                # v is the group-of-bits we are simulating as input
                cur_state = ostates[node]
                for n in range(nbits):
                    tbit = (v >> (nbits-n-1)) & 1
                    nxt = cur_state.get_next(tbit)
                    if nxt == -1: # fell off good path, reject
                        break
                    cur_state = ostates[nxt]
                # given starting node-id "node" and input v, we end at node-id "nxt".
                if nxt != -1:
                    next_starting_points.add(nxt)
                    assert node != nxt
                    assert nxt != 0
                    ostates[node].add_next_group(v,nxt)

    print(f"FINAL WALK  starting={len(next_starting_points)}")


def build_dfa(r) -> (list[dfa_state_t], dict[int,frozenset], list[tuple]):
    states,edges = _build_dfa(r)
    errors = _check_dfa(edges)
    if errors:
        print(f"DFA CHECK FAILED: errors={errors}")
        print("Exiting...", file=sys.stderr)
        sys.exit(1)
    print(f"DFA CHECK PASSED: errors={errors}")
    ostates =  _build_dfa_postprocess(states, edges)
    group_transitions(ostates)
    return ostates, states, edges


def print_graph(states, edges):
    print("FINAL")
    print(f"nstates = {len(states)}")
    print(f"nedges = {len(edges)}")
    print("states:", states)

    for x,c,y in edges:
        if y == 'FINAL':
            fs = ''
        else:
            fs = states[y]
        print(f"from: {states[x]}={x} input: {c} to: {fs}={y}")




def create_random_set():
    bs=['0','1']
    def random_bit_string(b):
        return "".join([ bs[random.randrange(2)] for x in range(0,b) ])
    s = set([])
    n = 100
    b = 32
    while len(s) < n:
        s.add(random_bit_string(b))
    return s

def work():
    # r is binary regular expression representing the instruction
    # patterns that the decoder should accept/decode successfully.
    # This example allows 4 instructions out of a possible 16 valid
    # strings with 4 bits.
    r = set(['1111','1100','0110', '1110'])
    #r = create_random_set()
    r = set(['1111','-100','-110', '-101'])
    r = set(['1111!3:000','-100!3:000','-110!3:000', '-101!3:000'])
    r = set(['1111!3:00011','-100!3:00011','-110!3:00011', '-101!3:00010'])
    r = set(['1111!3:00011@2341','-100!3:00011@2342','-110!3:00011@2343', '-101!3:00010@2344'])
    r = set(['11!2:11---@555', '1111---@666']) # ok
    r = set(['11!5:11---@555', '1111---@666']) # overly wide  not-constraint
    #r = set(['!2:1-@555', '1-@666']) # overly wide not-constraint
    r = set(['!3:1-1@555', '1-1@666']) # overly not-constraint with dontcares


    ostate, states, edges = build_dfa(r)
    print_graph(states,edges)


if __name__ == "__main__":
    work()
