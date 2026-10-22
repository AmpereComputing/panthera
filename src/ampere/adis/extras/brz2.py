#!/usr/bin/env python3
#
# Copyright (c) 2022 Ampere Computing. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause
#
from __future__ import annotations
import collections
import random

# epsilon string - out of input
# null state: when derivative is nothing

# peel('1', set(['1'])) = set([])  an accepting state, out of input
# peel('1', set(['0'])) = None     an error state 


def peel_set(c:int, x:set[str]) -> set([str]):
    n = set([])
    found = False
    for l in x:
        if l[0] == c:
            found = True
            if len(l) > 1:
                n.add(l[1:])
    if found:
        return n
    return None

def build_dfa(r) -> (dict[frozenset,int], list[tuple]):
    q = collections.deque()
    fr = frozenset(r)
    q.appendleft(fr)
    states = { 1:fr }
    nstates = 1
    edges = []
    
    while q:
        #print('Q',q)
        #print('STATES', states)
        x = q.pop()
        if len(x) == 0:
            edges.append((x,'NOTHING','FINAL'))
        else:
            #print("\tX",x)            
            for c in ['0','1']:
                y = peel_set(c,x)
                #print(f"\t\tPeel {c} -> Y {y}")
                if y is None:
                    if y not in states:
                        states[None] = nstates
                        nstates += 1
                    edges.append((x,c,None))
                else:
                    yf = frozenset(y)

                    # Experiment to remember *all* states.
                    #
                    # In a real test, dontcare splatters share a
                    # common final state so they can re-converge.
                    
                    q.appendleft(yf)
                    states[nstates] = yf
                    nstates += 1
                    edges.append((x,c,yf))
    return states, edges

def print_graph(states, edges):
    print("FINAL")
    print(f"nstates = {len(states)}")
    print(f"nedges = {len(edges)}")
    print("states:", states)

    for x,c,y in edges:
        #print(f"from: {states[x]}={x} input: {c} to: {fs}={y}")
        print(f"from: {x} input: {c} to: {y}")


def group_transitions(states, edges, nbits):
    pass 

def create_random_set():
    bs=['0','1']
    def random_bit_string(b):
        return "".join([ bs[random.randrange(2)] for x in range(0,b) ])
    s = set([])
    n = 8000
    b = 32
    while len(s) < n:
        s.add(random_bit_string(b))
    return s

def work():
    # r is binary regular expression representing the instruction
    # patterns that the decoder should accept/decode successfully.
    # This example allows 4 instructions out of a possible 16 valid
    # strings with 4 bits.
    #r = set(['1111','1100','0110', '1110'])
    r = create_random_set()
    states, edges = build_dfa(r)
    print_graph(states,edges)
    
    # current graph has 0,1,2 output edges per node
    # we want to create a new graph with 2^n output edges per node.
    
    # walk graph n inputs at a time and build a new graph
    #n = 2
    #gstates,gedges = group_transitions(states, edges, n)

if __name__ == "__main__":
    work()
