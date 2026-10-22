//
// Copyright (c) 2018-2019 Ampere Computing. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//

#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <boost/filesystem/operations.hpp>
#include <sstream>
#include <stdexcept>

#include "ampere/sc/module.h"
#include "ampere/sc/param.h"
#include "ampere/util/enum.h"
#include "ampere/sc/value.h"
#include "ampere/sc/param_registry.h"
#include "ampere/sc/testing/test_module.h"
#include "sysc/utils/sc_report.h"

namespace bfs = boost::filesystem;
using namespace ampere::sc;
using ::testing::HasSubstr;
using ::testing::MatchesRegex;
using ::testing::ThrowsMessage;

class ParameterRegistryTest : public ::testing::Test, public ParameterRegistry
{
protected:
    template<typename T>
    void add_setting(const std::string& s, const T& value)
    {
        ParameterRegistry::add_setting(s, value, "unit test");
    }

    template<typename T>
    T value_as(const ParameterRegistry::Setting* setting)
    {
        return from_json_T<T>(setting->value());
    }

    std::string testfixture(std::string filename) { return (bfs::path(TEST_FIXTURE_DIRECTORY) / filename).string(); }

    bool cfg_settings_non_empty() const
    {
        std::stringstream ss;
        dump_cfg_settings_verbosely(ss);
        return !ss.str().empty();
    }
};

TEST_F(ParameterRegistryTest, FindInEmptyRegistryReturnsNoValue)
{
    EXPECT_FALSE(match_setting("foo"));
}

TEST_F(ParameterRegistryTest, AddThenFindReturnsValue)
{
    add_setting("hello", "world");
    ASSERT_TRUE(match_setting("hello"));
    EXPECT_EQ(match_setting("hello")->value(), "world");
}

TEST_F(ParameterRegistryTest, StringWithDotIsEscaped)
{
    add_setting("foo.bar", "yes");
    ASSERT_TRUE(match_setting("foo.bar"));
    ASSERT_FALSE(match_setting("foo_bar"));
}

TEST_F(ParameterRegistryTest, RegexWithDotActsLikeRegex)
{
    add_setting("~foo.bar", "yes");
    ASSERT_TRUE(match_setting("foo.bar"));
    ASSERT_TRUE(match_setting("foo_bar"));
}

TEST_F(ParameterRegistryTest, FindReturnsLastMatchingValue)
{
    add_setting("abc", "first");
    add_setting("abc", "second");
    ASSERT_TRUE(match_setting("abc"));
    EXPECT_EQ(match_setting("abc")->value(), "second");
}

TEST_F(ParameterRegistryTest, CanGetValueAsInt)
{
    add_setting("foo", 17);
    EXPECT_EQ(value_as<int>(match_setting("foo")), 17);
}

TEST_F(ParameterRegistryTest, CanGetValueAsFloat)
{
    add_setting("foo", 3.14);
    EXPECT_FLOAT_EQ(value_as<float>(match_setting("foo")), 3.14);
}

TEST_F(ParameterRegistryTest, LoadEmptyConfigFile)
{
    load_settings(testfixture("empty.cfg"));
    EXPECT_EQ(settings.size(), 0u);
}

TEST_F(ParameterRegistryTest, LoadSimpleConfigFile)
{
    load_settings(testfixture("simple.cfg"));
    EXPECT_EQ(settings.size(), 2u);
}

TEST_F(ParameterRegistryTest, LoadBadConfigFile)
{
    EXPECT_ANY_THROW(load(testfixture("bad.cfg")));
}

TEST_F(ParameterRegistryTest, LoadMissingConfigFile)
{
    EXPECT_ANY_THROW(load("missing.cfg"));
}

TEST_F(ParameterRegistryTest, WhenceReportsLinesCorrectly)
{
    load(testfixture("simple.cfg"));
    auto p1 = match_setting("param1");
    auto p2 = match_setting("param2");
    ASSERT_TRUE(p1);
    ASSERT_TRUE(p2);
    EXPECT_THAT(p1->whence, MatchesRegex(".*/simple.cfg:6"));
    EXPECT_THAT(p2->whence, MatchesRegex(".*/simple.cfg:7"));
}

TEST_F(ParameterRegistryTest, BogusIntegerReportsAnError)
{
    EXPECT_ANY_THROW(from_json_T<int>("42blah"));
}

TEST_F(ParameterRegistryTest, BogusUnsignedIntegerReportsAnError)
{
    EXPECT_ANY_THROW(from_json_T<unsigned int>("42blah"));
}

TEST_F(ParameterRegistryTest, HexWillConvert)
{
    EXPECT_EQ(from_json_T<unsigned int>("0xdeadbeef"), 0xdeadbeef);
}

TEST_F(ParameterRegistryTest, OctalWillConvert)
{
    EXPECT_EQ(from_json_T<int>("0777"), 0777);
}

template<typename T>
void test_min()
{
    std::string s;
    s = std::to_string(std::numeric_limits<T>::min());
    EXPECT_EQ(from_json_T<T>(s), std::numeric_limits<T>::min());
}

template<typename T>
void test_max()
{
    std::string s;
    s = std::to_string(std::numeric_limits<T>::max());
    EXPECT_EQ(from_json_T<T>(s), std::numeric_limits<T>::max());
}

TEST_F(ParameterRegistryTest, NumericLimitTest)
{
    test_min<int>();
    test_max<int>();
    test_min<long int>();
    test_max<long int>();
    test_min<long long int>();
    test_max<long long int>();
    test_min<unsigned int>();
    test_max<unsigned int>();
    test_min<unsigned long int>();
    test_max<unsigned long int>();
    test_min<unsigned long long int>();
    test_max<unsigned long long int>();
}

TEST_F(ParameterRegistryTest, LoadSimpleJSONValues)
{
    load(testfixture("simple_json.cfg"));
    auto a_string = match_setting("a_string");
    auto an_array = match_setting("an_array");
    auto an_object = match_setting("an_object");

    using objtype = std::map<std::string, int>;

    std::vector<int> expected_array{0, 1, 2, 3};
    objtype expected_object{{"a", 12}, {"b", 23}};

    EXPECT_EQ(a_string->value().get<std::string>(), "hello world");
    EXPECT_EQ(an_array->value().get<std::vector<int>>(), expected_array);
    EXPECT_EQ(an_object->value().get<objtype>(), expected_object);
}

TEST_F(ParameterRegistryTest, LoadMultiLineJSONValues)
{
    load(testfixture("multi_line_json.cfg"));
    auto an_object = match_setting("an_object");
    auto next_value = match_setting("next_value");

    using objtype = std::map<std::string, int>;
    objtype expected_object{{"a", 12}, {"b", 23}};

    EXPECT_EQ(an_object->value().get<objtype>(), expected_object);
    EXPECT_THAT(next_value->whence, MatchesRegex(".*/multi_line_json.cfg:10"));
}

TEST_F(ParameterRegistryTest, OutOfRangeTest)
{
    EXPECT_THROW(from_json_T<int>("2147483648"), std::out_of_range);
    EXPECT_THROW(from_json_T<int>("-2147483649"), std::out_of_range);
    EXPECT_THROW(from_json_T<long int>("9223372036854775808"), std::out_of_range);
    EXPECT_THROW(from_json_T<long int>("-9223372036854775809"), std::out_of_range);
}

TEST_F(ParameterRegistryTest, DuplicateParameterThrowsError)
{
    ParameterRegistry::add_parameter_with_type<int>("dup_param", "a duplicate parameter", "21", "unit test");
    EXPECT_ANY_THROW(
        ParameterRegistry::add_parameter_with_type<int>("dup_param", "a duplicate parameter", "21", "unit test"));
}

TEST_F(ParameterRegistryTest, ConfigFileInclusion)
{
    load(testfixture("include_parent.cfg"));

    auto s1 = match_setting("foo.bar.baz");
    auto s2 = match_setting("foo.bar.frob");

    ASSERT_TRUE(s1);
    ASSERT_TRUE(s2);

    EXPECT_EQ(value_as<int>(s1), 10);
    EXPECT_EQ(value_as<int>(s2), 100);
}

TEST_F(ParameterRegistryTest, ConfigFileInclusionRegexes)
{
    load(testfixture("include_parent.cfg"));

    auto s1 = match_setting("some.baz");
    auto s2 = match_setting("somere.other.re");
    auto s3 = match_setting("foo.bar.another.re");
    auto s4 = match_setting("some.x.y");

    ASSERT_TRUE(s1);
    ASSERT_TRUE(s2);
    ASSERT_TRUE(s3);
    ASSERT_TRUE(s4);

    EXPECT_EQ(value_as<int>(s1), 10);
    EXPECT_EQ(value_as<int>(s2), 19);
    EXPECT_EQ(value_as<int>(s3), 19);
    EXPECT_EQ(value_as<int>(s4), 900);
}

TEST_F(ParameterRegistryTest, ConfigFileInclusionRelativePaths)
{
    load(testfixture("include_parent_dirtest.cfg"));

    auto s1 = match_setting("foo.bar.baz");
    auto s2 = match_setting("foo.bar.frob");

    ASSERT_TRUE(s1);
    ASSERT_TRUE(s2);

    EXPECT_EQ(value_as<int>(s1), 10);
    EXPECT_EQ(value_as<int>(s2), 100);
}

TEST_F(ParameterRegistryTest, ConfigFileMissingOverride)
{
    ASSERT_THROW({ load(testfixture("include_parent_missing_override.cfg")); }, sc_core::sc_report);
}

TEST_F(ParameterRegistryTest, ConfigFileMissingOverrideSinceResursivelyIncluded)
{
    load(testfixture("include_override_recursive_parent.cfg"));

    auto s1 = match_setting("foo.bar.baz");
    auto s2 = match_setting("foo.bar.frob");
    auto s3 = match_setting("foo.bar.x.y");

    ASSERT_TRUE(s1);
    ASSERT_TRUE(s2);
    EXPECT_FALSE(s3);

    EXPECT_EQ(value_as<int>(s1), 20);
    EXPECT_EQ(value_as<int>(s2), 9);

    EXPECT_TRUE(cfg_settings_non_empty());
}

TEST_F(ParameterRegistryTest, ConfigFileOverride)
{
    load(testfixture("include_parent_override.cfg"));

    auto s1 = match_setting("foo.bar.baz");
    auto s2 = match_setting("foo.bar.frob");
    auto s3 = match_setting("foo.bar.x.y");

    ASSERT_TRUE(s1);
    ASSERT_TRUE(s2);
    EXPECT_FALSE(s3);

    EXPECT_EQ(value_as<int>(s1), 20);
    EXPECT_EQ(value_as<int>(s2), 9);

    EXPECT_TRUE(cfg_settings_non_empty());
}

TEST_F(ParameterRegistryTest, ConfigFileInnerOverride)
{
    load(testfixture("include_inner_child_override.cfg"));

    auto s1 = match_setting("baz.foo.bar.completely_different");
    auto s2 = match_setting("baz.foo.bar.frob");

    ASSERT_TRUE(s1);
    EXPECT_FALSE(s2);

    EXPECT_EQ(value_as<int>(s1), 42);

    EXPECT_TRUE(cfg_settings_non_empty());
}

TEST_F(ParameterRegistryTest, ConfigFileAppend)
{
    load(testfixture("include_parent_append.cfg"));

    auto s1 = match_setting("foo.bar.baz");
    auto s2 = match_setting("foo.bar.frob");
    auto s3 = match_setting("foo.bar.x.y");

    ASSERT_TRUE(s1);
    ASSERT_TRUE(s2);
    EXPECT_TRUE(s3);

    EXPECT_EQ(value_as<int>(s1), 20);
    EXPECT_EQ(value_as<int>(s2), 9);
    EXPECT_EQ(value_as<int>(s3), 900);

    EXPECT_TRUE(cfg_settings_non_empty());
}

TEST_F(ParameterRegistryTest, ConfigFileAppendThenOverride)
{
    load(testfixture("include_parent_append_then_override.cfg"));

    auto s1 = match_setting("foo.bar.completely_different");
    auto s2 = match_setting("foo.bar.baz");
    auto s3 = match_setting("foo.bar.x.y");

    ASSERT_TRUE(s1);
    ASSERT_FALSE(s2);
    ASSERT_FALSE(s3);

    EXPECT_EQ(value_as<int>(s1), 42);

    EXPECT_TRUE(cfg_settings_non_empty());
}

TEST_F(ParameterRegistryTest, ConfigFileAppendThenInsertSameRaises)
{
    auto snippet = [this] {
        load(testfixture("include_parent_append_then_override_initial_same.cfg"));
    };
    EXPECT_THAT(snippet,
                ThrowsMessage<sc_core::sc_report>(
                    HasSubstr("cfg is modifying a config-scope, use 'append-cfg' or 'override-cfg' instead")));
}

TEST_F(ParameterRegistryTest, ConfigFileAppendOrder)
{
    load(testfixture("include_parent_append_order.cfg"));

    auto s1 = match_setting("foo.bar.baz");
    auto s2 = match_setting("foo.bar.frob");
    auto s3 = match_setting("foo.bar.x.y");

    ASSERT_TRUE(s1);
    ASSERT_TRUE(s2);
    EXPECT_TRUE(s3);

    EXPECT_EQ(value_as<int>(s1), 50);
    EXPECT_EQ(value_as<int>(s2), 9);
    EXPECT_EQ(value_as<int>(s3), 900);

    EXPECT_TRUE(cfg_settings_non_empty());
}

TEST_F(ParameterRegistryTest, ConfigFileUnevenOverride)
{
    load(testfixture("include_uneven_override.cfg"));

    auto s1 = match_setting("baz.foo.bar.completely_different");
    auto s2 = match_setting("bax.foo.bar.x.y");
    auto s3 = match_setting("baz.foo.bar.frob");
    auto s4 = match_setting("bax.foo.bar.other");

    ASSERT_TRUE(s1);
    ASSERT_TRUE(s2);
    ASSERT_FALSE(s3);
    ASSERT_FALSE(s4);

    EXPECT_EQ(value_as<int>(s1), 42);
    EXPECT_EQ(value_as<int>(s2), 900);

    EXPECT_TRUE(cfg_settings_non_empty());
}

TEST_F(ParameterRegistryTest, ConfigFileOverrideMixWithPlainDoesNotCompletelyOverride)
{
    load(testfixture("mix_override_plain.cfg"));

    auto s1 = match_setting("foo.bar.completely_different");
    auto s2 = match_setting("foo.bar.something_else");
    auto s3 = match_setting("foo.bar.x.y");
    auto s4 = match_setting("foo.bar.baz");

    ASSERT_TRUE(s1);
    ASSERT_TRUE(s2);
    ASSERT_FALSE(s3);
    ASSERT_FALSE(s4);

    EXPECT_EQ(value_as<int>(s1), 42);
    EXPECT_EQ(value_as<int>(s2), 5);

    EXPECT_TRUE(cfg_settings_non_empty());
}

TEST_F(ParameterRegistryTest, ConfigFileRemove)
{
    load(testfixture("include_parent_remove.cfg"));

    auto s1 = match_setting("foo.bar.baz");
    auto s2 = match_setting("foo.bar.frob");
    auto s3 = match_setting("foo.bar.x.y");
    auto s4 = match_setting("foo.bat.baz");
    auto s5 = match_setting("foo.bay.baz");

    ASSERT_FALSE(s1);
    ASSERT_FALSE(s2);
    EXPECT_FALSE(s3);
    EXPECT_TRUE(s4);
    EXPECT_TRUE(s5);

    EXPECT_EQ(value_as<int>(s4), 10);
    EXPECT_EQ(value_as<int>(s5), 10);

    EXPECT_TRUE(cfg_settings_non_empty());
}

TEST_F(ParameterRegistryTest, ConfigFileInclusionNoPrefix)
{
    load(testfixture("include_parent_no_prefix.cfg"));

    auto s1 = match_setting("baz");
    auto s2 = match_setting("frob");
    auto s3 = match_setting("completely_different");

    ASSERT_TRUE(s1);
    ASSERT_TRUE(s2);
    ASSERT_TRUE(s3);

    EXPECT_EQ(value_as<int>(s1), 10);
    EXPECT_EQ(value_as<int>(s2), 100);
    EXPECT_EQ(value_as<int>(s3), 42);
}

TEST_F(ParameterRegistryTest, ConfigFileInclusionNoPrefixNoColon)
{
    load(testfixture("include_parent_no_prefix_no_colon.cfg"));

    auto s1 = match_setting("baz");
    auto s2 = match_setting("frob");
    auto s3 = match_setting("completely_different");

    ASSERT_TRUE(s1);
    ASSERT_TRUE(s2);
    ASSERT_TRUE(s3);

    EXPECT_EQ(value_as<int>(s1), 10);
    EXPECT_EQ(value_as<int>(s2), 100);
    EXPECT_EQ(value_as<int>(s3), 42);
}

TEST_F(ParameterRegistryTest, ConfigFileInclusionNoPrefixNested)
{
    load(testfixture("include_parent_no_prefix_nested.cfg"));

    auto s1 = match_setting("foo.baz");
    auto s2 = match_setting("foo.frob");
    auto s3 = match_setting("foo.completely_different");

    ASSERT_TRUE(s1);
    ASSERT_TRUE(s2);
    ASSERT_TRUE(s3);

    EXPECT_EQ(value_as<int>(s1), 10);
    EXPECT_EQ(value_as<int>(s2), 100);
    EXPECT_EQ(value_as<int>(s3), 42);
}

TEST_F(ParameterRegistryTest, ConfigFileInclusionNoPrefixNestedNoColon)
{
    load(testfixture("include_parent_no_prefix_nested_no_colon.cfg"));

    auto s1 = match_setting("foo.baz");
    auto s2 = match_setting("foo.frob");
    auto s3 = match_setting("foo.completely_different");

    ASSERT_TRUE(s1);
    ASSERT_TRUE(s2);
    ASSERT_TRUE(s3);

    EXPECT_EQ(value_as<int>(s1), 10);
    EXPECT_EQ(value_as<int>(s2), 100);
    EXPECT_EQ(value_as<int>(s3), 42);
}

TEST_F(ParameterRegistryTest, SCTimeConversionTest)
{
    EXPECT_EQ(from_json_T<sc_core::sc_time>("1fs"), (sc_core::sc_time{1, sc_core::SC_FS}));
    EXPECT_EQ(from_json_T<sc_core::sc_time>("4 ps"), (sc_core::sc_time{4, sc_core::SC_PS}));
    EXPECT_EQ(from_json_T<sc_core::sc_time>("21ns"), (sc_core::sc_time{21, sc_core::SC_NS}));
    EXPECT_EQ(from_json_T<sc_core::sc_time>("9.8us"), (sc_core::sc_time{9.8, sc_core::SC_US}));
    EXPECT_EQ(from_json_T<sc_core::sc_time>("100ms"), (sc_core::sc_time{100, sc_core::SC_MS}));
    EXPECT_EQ(from_json_T<sc_core::sc_time>("3e2 s"), (sc_core::sc_time{3e2, sc_core::SC_SEC}));
}

TEST_F(ParameterRegistryTest, SCTimeConversionReportsErrors)
{
    EXPECT_ANY_THROW(from_json_T<sc_core::sc_time>("1 ds"));
    EXPECT_ANY_THROW(from_json_T<sc_core::sc_time>("garble"));
    EXPECT_ANY_THROW(from_json_T<sc_core::sc_time>("1e417ns"));
}

TEST_F(ParameterRegistryTest, AliasReturnsUnderlyingValue)
{
    ParameterRegistry::add_alias("hello", "hola", "unit test");
    add_setting("hola", "world");
    ASSERT_TRUE(match_setting("hello"));
    EXPECT_EQ(match_setting("hello")->value(), "world");
}

TEST_F(ParameterRegistryTest, RecursiveAliasIsDetected)
{
    ParameterRegistry::add_alias("hello", "hola", "unit test");
    ParameterRegistry::add_alias("hola", "hello", "unit test");
    EXPECT_ANY_THROW(match_setting("hello"));
}

TEST_F(ParameterRegistryTest, LoadAlias)
{
    load(testfixture("alias.cfg"));
    EXPECT_EQ(match_setting("hello")->value(), "world");
}

TEST_F(ParameterRegistryTest, LoadAliasFromSubRegistry)
{
    load(testfixture("alias_parent.cfg"));
    ASSERT_TRUE(match_setting("core.hello"));
    EXPECT_EQ(match_setting("core.hello")->value(), "world");
}

TEST_F(ParameterRegistryTest, NonexistantAliasWillThrow)
{
    ParameterRegistry::add_alias("foo", "bar", "unit test");
    ParameterRegistry::add_alias("one", "two", "unit test");
    ParameterRegistry::add_alias("two", "three", "unit test");
    EXPECT_ANY_THROW(match_setting("foo"));
    EXPECT_ANY_THROW(match_setting("one"));
}

TEST_F(ParameterRegistryTest, AliasAtParentLevel)
{
    load(testfixture("alias_in_parent.cfg"));
    auto setting = match_setting("a.param");
    ASSERT_TRUE(setting);
    auto value = setting->value().get<int>();
    EXPECT_EQ(value, 20);
}

TEST_F(ParameterRegistryTest, AliasAtParentLevelWithOverride)
{
    load(testfixture("alias_in_parent.cfg"));
    add_setting("b.param", 42);
    auto setting = match_setting("a.param");
    ASSERT_TRUE(setting);
    auto value = setting->value().get<int>();
    EXPECT_EQ(value, 42);
}

TEST_F(ParameterRegistryTest, AliasAtGrandparentLevelWithOverride)
{
    load(testfixture("alias_grandparent.cfg"));
    add_setting("x.b.param", 42);
    auto setting = match_setting("x.a.param");
    ASSERT_TRUE(setting);
    auto value = setting->value().get<int>();
    EXPECT_EQ(value, 42);
}

TEST_F(ParameterRegistryTest, CanMergeJSONArrays)
{
    add_setting("foo", "[1]"_json);
    add_setting("+foo", "[2]"_json);
    std::vector<int> expected{1, 2};
    auto setting = match_setting("foo");
    ASSERT_TRUE(setting);
    EXPECT_EQ(setting->value(), expected);
}

TEST_F(ParameterRegistryTest, CanMergeJSONObjects)
{
    json j_document = R"({
      "a": "b",
      "x": [1],
      "y" : {
        "z": 2
      },
      "c": {
        "d": "e",
        "f": "g"
     }})"_json;

    json j_patch = R"({
      "a":"z",
      "x": [2],
      "y": {},
      "c": {
        "f": null
      }})"_json;

    add_setting("foo", j_document);
    add_setting("+foo", j_patch);
    auto setting = match_setting("foo");
    ASSERT_TRUE(setting);
    EXPECT_EQ(setting->value()["a"].get<std::string>(), "z");
    auto expected_array = std::vector<int>{2};
    EXPECT_EQ(setting->value()["x"].get<std::vector<int>>(), expected_array);
    EXPECT_EQ(setting->value()["y"].count("z"), 1u);
    EXPECT_EQ(setting->value()["c"]["d"].get<std::string>(), "e");
    EXPECT_EQ(setting->value()["c"].count("f"), 0u);
}

TEST_F(ParameterRegistryTest, CanMergeJSONAcrossConfigs)
{
    add_setting("+foo.bar.param", R"([1])"_json);
    load(testfixture("inner_append_json.cfg"), "foo");
    add_setting("+foo.bar.param", R"([4])"_json);

    auto setting = match_setting("foo.bar.param");
    ASSERT_TRUE(setting);
    EXPECT_EQ(setting->value(), std::vector<int>({1, 2, 3, 4}));
}

TEST_F(ParameterRegistryTest, CanLoadJSONFromFileWithinArgs)
{
    auto path = "@" + testfixture("values.json");
    const char* argv[] = {"", "-foo", path.c_str()};
    load_from_args(3, const_cast<char**>(argv));

    auto setting = match_setting("foo");
    ASSERT_TRUE(setting);
    EXPECT_EQ(setting->value()["x"], 2);
    EXPECT_EQ(setting->value()["y"], 1);
}

TEST_F(ParameterRegistryTest, CanLoadJSONFromFileWithinConfig)
{
    load(testfixture("include_json.cfg"));
    auto setting = match_setting("foo");
    ASSERT_TRUE(setting);
    EXPECT_EQ(setting->value()["x"], 2);
    EXPECT_EQ(setting->value()["y"], 1);
}

TEST_F(ParameterRegistryTest, TypedBasic)
{
    need_help = true;
    auto& param = add_parameter_with_type<unsigned int>("something", "something", 1, "default");
    EXPECT_EQ(param.type.name, "unsigned integer");
}

TEST_F(ParameterRegistryTest, TypedSpecification)
{
    need_help = true;
    auto& param = add_parameter_with_type<uint8_t>("something", "something", 1, "default");
    EXPECT_EQ(param.type.name, "unsigned integer");
    EXPECT_EQ(param.type.specification, "0 - 255");
}

namespace ampere::sc {
extern void collect_nonspace(std::istream& is,
                             std::ostringstream& result,
                             const std::map<std::string, std::string>& vars);
void collect_string(std::istream& is,
                    std::ostringstream& result,
                    int& lineno,
                    const std::map<std::string, std::string>& vars);
std::string collect_var(std::istream& is, const std::map<std::string, std::string>& vars);
} // namespace ampere::sc

TEST(JSONCollection, CollectNonSpaceWillQuoteNonNumbers)
{
    std::istringstream iss1{"this/should/be/quoted"};
    std::istringstream iss2{"333ps"};

    std::ostringstream oss1;
    std::ostringstream oss2;

    collect_nonspace(iss1, oss1, {});
    collect_nonspace(iss2, oss2, {});

    EXPECT_EQ(oss1.str(), "\"this/should/be/quoted\"");
    EXPECT_EQ(oss2.str(), "\"333ps\"");
}

TEST(JSONCollection, CollectNonSpaceWillNotQuoteKeywords)
{
    std::istringstream iss_true{"true"};
    std::istringstream iss_false{"false"};
    std::istringstream iss_null{"null"};

    std::ostringstream oss_true;
    std::ostringstream oss_false;
    std::ostringstream oss_null;

    collect_nonspace(iss_true, oss_true, {});
    collect_nonspace(iss_false, oss_false, {});
    collect_nonspace(iss_null, oss_null, {});

    EXPECT_EQ(oss_true.str(), "true");
    EXPECT_EQ(oss_false.str(), "false");
    EXPECT_EQ(oss_null.str(), "null");
}

TEST(JSONCollection, CollectNonSpaceWillNotQuoteNumbers)
{
    std::istringstream iss1{"42"};
    std::istringstream iss2{"1.123"};
    std::istringstream iss3{"0.314e1"};
    std::istringstream iss4{"0.00314E+3 "};
    std::istringstream iss5{"1111E-100"};

    std::ostringstream oss1;
    std::ostringstream oss2;
    std::ostringstream oss3;
    std::ostringstream oss4;
    std::ostringstream oss5;

    collect_nonspace(iss1, oss1, {});
    collect_nonspace(iss2, oss2, {});
    collect_nonspace(iss3, oss3, {});
    collect_nonspace(iss4, oss4, {});
    collect_nonspace(iss5, oss5, {});

    EXPECT_EQ(oss1.str(), "42");
    EXPECT_EQ(oss2.str(), "1.123");
    EXPECT_EQ(oss3.str(), "0.314e1");
    EXPECT_EQ(oss4.str(), "0.00314E+3");
    EXPECT_EQ(oss5.str(), "1111E-100");
}

TEST(JSONCollection, CollectVar)
{
    std::istringstream iss{"$(foo)"};
    auto result = collect_var(iss, {{"foo", "bar"}});
    EXPECT_EQ(result, "bar");
}

TEST(JSONCollection, CollectVarNoMatch)
{
    std::istringstream iss{"$(foo)"};
    auto result = collect_var(iss, {});
    EXPECT_EQ(result, "");
}

TEST(JSONCollection, CollectVarEmptyInput)
{
    std::istringstream iss{};
    auto result = collect_var(iss, {});
    EXPECT_EQ(result, "");
}

TEST(JSONCollection, CollectVarEarlyEndOfInput)
{
    std::istringstream iss{"$(foo"};
    EXPECT_ANY_THROW(collect_var(iss, {}));
}

TEST(JSONCollection, CollectStringExpandsVariable)
{
    int lineno = 1;

    std::istringstream iss1{"\"$(foo)\""};
    std::istringstream iss2{"\"em$(foo)bed\""};

    std::ostringstream oss1;
    std::ostringstream oss2;

    collect_string(iss1, oss1, lineno, {{"foo", "bar"}});
    collect_string(iss2, oss2, lineno, {{"foo", "bar"}});

    EXPECT_EQ(oss1.str(), "\"bar\"");
    EXPECT_EQ(oss2.str(), "\"embarbed\"");
}

TEST(JSONCollection, CollectNonSpaceExpandsVariable)
{
    std::istringstream iss1{"$(foo)"};
    std::istringstream iss2{"em$(foo)bed"};

    std::ostringstream oss1;
    std::ostringstream oss2;

    collect_nonspace(iss1, oss1, {{"foo", "bar"}});
    collect_nonspace(iss2, oss2, {{"foo", "bar"}});

    EXPECT_EQ(oss1.str(), "\"bar\"");
    EXPECT_EQ(oss2.str(), "\"embarbed\"");
}

TEST(JSONCollection, CollectStringNotAVariableAfterAll)
{
    int lineno = 1;
    std::istringstream iss{"\"${notavar}\""};
    std::ostringstream oss;
    collect_string(iss, oss, lineno, {{"notavar", "redherring"}});
    EXPECT_EQ(oss.str(), "\"${notavar}\"");
}

make_enum(SOMEENUM, ONE, TWO, THREE);

struct ParamTest : TestModule, Module
{
    using TestModule::get_clock;
    using TestModule::get_environment;

    ParamTest() : Module("top", get_clock(), get_environment()) {}
};

TEST_F(ParamTest, Boolean)
{
    get_environment()->parameters.add_setting("param", "true", "unit test");
    Param<bool> param(*get_environment(), "param", "something", false);

    EXPECT_EQ(param, true);
    EXPECT_EQ(param.val(), true);
    EXPECT_EQ(*param, true);
}

TEST_F(ParamTest, Integer)
{
    get_environment()->parameters.add_setting("param", "8", "unit test");
    Param<int> param(*get_environment(), "param", "something", 3);

    EXPECT_EQ(param, 8);
    EXPECT_EQ(param.val(), 8);
    EXPECT_EQ(*param, 8);
}

TEST_F(ParamTest, Enum)
{
    get_environment()->parameters.add_setting("param", "TWO", "unit test");
    get_environment()->parameters.add_setting("other_param", "2", "unit test");
    Param<SOMEENUM> param(*get_environment(), "param", "something", SOMEENUM::ONE);
    Param<SOMEENUM> other_param(*get_environment(), "other_param", "something", SOMEENUM::ONE);

    EXPECT_EQ(param.val(), SOMEENUM::TWO);
    EXPECT_EQ(*param, SOMEENUM::TWO);
    EXPECT_EQ(other_param.val(), SOMEENUM::THREE);
    EXPECT_EQ(*other_param, SOMEENUM::THREE);
}

TEST_F(ParamTest, FrozenEnum)
{
    auto e = ampere::util::FrozenEnumeration<unsigned>::create();
    e.add("RED");
    e.add("BLUE");
    e.add("GREEN");
    auto my_enum = e.freeze();

    get_environment()->parameters.add_setting("param", "BLUE", "unit test");
    get_environment()->parameters.add_setting("other_param", "2", "unit test");
    get_environment()->parameters.add_setting("other_params", R"(["BLUE", "GREEN"])"_json, "unit test");
    Param<decltype(my_enum)> param(*get_environment(), "param", "something", "RED", my_enum);
    Param<decltype(my_enum)> other_param(*get_environment(), "other_param", "something", "RED", my_enum);
    Param<std::vector<decltype(my_enum)>> other_params(*get_environment(), "other_params", "something", {}, my_enum);

    EXPECT_EQ(param->value(), my_enum.at("BLUE"));
    EXPECT_EQ(other_param->value(), my_enum.at("GREEN"));
    EXPECT_THAT(other_params.val(), ::testing::ElementsAre(my_enum.at("BLUE"), my_enum.at("GREEN")));
}

TEST_F(ParamTest, String)
{
    get_environment()->parameters.add_setting("param", "hey", "unit test");
    Param<std::string> param(*get_environment(), "param", "something", "yo");

    EXPECT_EQ(static_cast<std::string>(param), "hey");
    EXPECT_EQ(param.val(), "hey");
    EXPECT_EQ(*param, "hey");
    EXPECT_EQ(param->size(), 3U);
}

TEST_F(ParamTest, Optional)
{
    get_environment()->parameters.add_setting("param", "8", "unit test");
    get_environment()->parameters.add_setting("other_param", json::parse("null"), "unit test");
    Param<std::optional<int>> param(*get_environment(), "param", "something", {});
    Param<std::optional<int>> other_param(*get_environment(), "other_param", "something", {});

    EXPECT_TRUE(static_cast<bool>(param));
    EXPECT_TRUE(param.has_value());
    EXPECT_EQ(param.val(), 8);
    EXPECT_EQ(*param, 8);
    EXPECT_EQ(param, 8);

    EXPECT_FALSE(static_cast<bool>(other_param));
    EXPECT_FALSE(other_param.has_value());
}

TEST_F(ParamTest, UniquePtr)
{
    get_environment()->parameters.add_setting("param", "blah", "unit test");
    get_environment()->parameters.add_setting("other_param", json::parse("null"), "unit test");
    Param<std::unique_ptr<std::string>> param(*get_environment(), "param", "something", {});
    Param<std::unique_ptr<std::string>> other_param(*get_environment(), "other_param", "something", {});

    EXPECT_TRUE(static_cast<bool>(param));
    EXPECT_EQ(*param, "blah");
    EXPECT_EQ(param->size(), 4U);

    EXPECT_FALSE(static_cast<bool>(other_param));
}

TEST_F(ParamTest, PowerOfTwo)
{
    get_environment()->parameters.add_setting("param", "16", "unit test");
    get_environment()->parameters.add_setting("other_param", "15", "unit test");
    Param<PowerOfTwo<unsigned>> param(*get_environment(), "param", "something", 4);
    EXPECT_EQ(*param, 16);

    EXPECT_THROW(
        { Param<PowerOfTwo<unsigned>> other_param(*get_environment(), "other_param", "something", 2); },
        std::invalid_argument);
}

TEST_F(ParamTest, WithinRange)
{
    using WithinRangeParam = Param<WithinRange<unsigned, 2, 8>>;
    get_environment()->parameters.add_setting("param", "7", "unit test");
    get_environment()->parameters.add_setting("lower_param", "1", "unit test");
    get_environment()->parameters.add_setting("upper_param", "15", "unit test");
    WithinRangeParam param(*get_environment(), "param", "something", 4);
    EXPECT_EQ(*param, 7);

    EXPECT_THROW(
        { WithinRangeParam lower_param(*get_environment(), "lower_param", "something", 2); }, std::invalid_argument);

    EXPECT_THROW(
        { WithinRangeParam upper_param(*get_environment(), "upper_param", "something", 2); }, std::invalid_argument);
}

TEST_F(ParamTest, NonZero)
{
    get_environment()->parameters.add_setting("param", "16", "unit test");
    get_environment()->parameters.add_setting("other_param", "0", "unit test");
    Param<NonZero<unsigned>> param(*get_environment(), "param", "something", 4);
    EXPECT_EQ(*param, 16);

    EXPECT_THROW(
        { Param<NonZero<unsigned>> other_param(*get_environment(), "other_param", "something", 2); },
        std::invalid_argument);
}

TEST_F(ParamTest, OneOf)
{
    using OneOfSome = OneOf<unsigned, 1, 4, 8>;
    get_environment()->parameters.add_setting("param", "4", "unit test");
    get_environment()->parameters.add_setting("other_param", "6", "unit test");
    Param<OneOfSome> param(*get_environment(), "param", "something", 1);
    EXPECT_EQ(*param, 4);

    EXPECT_THROW(
        { Param<OneOfSome> other_param(*get_environment(), "other_param", "something", 1); }, std::invalid_argument);
}

/*
 * vim: sw=4:ts=4:et:si:cindent:cino=\:0,(0,g0,t0:
 */
