//
// Copyright (c) 2023 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#pragma once

#include "ampere/sys/test/integration_test.h"

class CoreIntegrationTest : public ampere::SysIntegrationTest
{
protected:
    CoreIntegrationTest() = default;
};

// Core tests

class FuriosaCoreIntegrationTest : public CoreIntegrationTest
{
protected:
    void SetUp() override
    {
        get_environment()->parameters.load(ampere::sc::TestModule::testconfig("sys/furiosa-core-test.cfg"));
    }
};

class RipleyCoreIntegrationTest : public CoreIntegrationTest
{
protected:
    void SetUp() override
    {
        get_environment()->parameters.load(ampere::sc::TestModule::testconfig("sys/ripley-core-test.cfg"));
    }
};

class TrinityCoreIntegrationTest : public CoreIntegrationTest
{
protected:
    void SetUp() override
    {
        get_environment()->parameters.load(ampere::sc::TestModule::testconfig("sys/trinity-core-test.cfg"));
    }
};

class ConnorCoreIntegrationTest : public CoreIntegrationTest
{
protected:
    void SetUp() override
    {
        get_environment()->parameters.load(ampere::sc::TestModule::testconfig("sys/connor-core-test.cfg"));
    }
};

class ConnorAICoreIntegrationTest : public CoreIntegrationTest
{
protected:
    void SetUp() override
    {
        get_environment()->parameters.load(ampere::sc::TestModule::testconfig("sys/connorai-core-test.cfg"));
    }
};

class AlitaCoreIntegrationTest : public CoreIntegrationTest
{
protected:
    void SetUp() override
    {
        get_environment()->parameters.load(ampere::sc::TestModule::testconfig("sys/alita-core-test.cfg"));
    }
};

// SoC tests

class SirynCoreIntegrationTest : public CoreIntegrationTest
{
protected:
    void SetUp() override { get_environment()->parameters.load(TestModule::testconfig("sys/siryn.cfg")); }
};

class BansheeCoreIntegrationTest : public CoreIntegrationTest
{
protected:
    void SetUp() override { get_environment()->parameters.load(ampere::sc::TestModule::testconfig("sys/banshee.cfg")); }
};

class RogueCoreIntegrationTest : public CoreIntegrationTest
{
protected:
    void SetUp() override { get_environment()->parameters.load(ampere::sc::TestModule::testconfig("sys/rogue.cfg")); }
};

class DominoCoreIntegrationTest : public CoreIntegrationTest
{
protected:
    void SetUp() override { get_environment()->parameters.load(ampere::sc::TestModule::testconfig("sys/domino.cfg")); }
};

class NebulaCoreIntegrationTest : public CoreIntegrationTest
{
protected:
    void SetUp() override { get_environment()->parameters.load(ampere::sc::TestModule::testconfig("sys/nebula.cfg")); }
};

class NebulaRateNCoreIntegrationTest : public CoreIntegrationTest
{
protected:
    void SetUp() override
    {
        get_environment()->parameters.load(ampere::sc::TestModule::testconfig("sys/nebula-rateN.cfg"));
    }
};

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
