#include "firebolt/json_types.h"
#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

using namespace Firebolt::JSON;

TEST(JsonTypesTest, StringBasicType)
{
    String str;
    nlohmann::json json = "test string";
    str.fromJson(json);
    EXPECT_EQ(str.value(), "test string");
}

TEST(JsonTypesTest, BooleanBasicType)
{
    Boolean boolean;
    nlohmann::json json = true;
    boolean.fromJson(json);
    EXPECT_TRUE(boolean.value());
}

TEST(JsonTypesTest, FloatBasicType)
{
    Float floatVal;
    nlohmann::json json = 3.14f;
    floatVal.fromJson(json);
    EXPECT_FLOAT_EQ(floatVal.value(), 3.14f);
}

TEST(JsonTypesTest, UnsignedBasicType)
{
    Unsigned unsignedVal;
    nlohmann::json json = 42u;
    unsignedVal.fromJson(json);
    EXPECT_EQ(unsignedVal.value(), 42u);
}

TEST(JsonTypesTest, IntegerBasicType)
{
    Integer intVal;
    nlohmann::json json = -42;
    intVal.fromJson(json);
    EXPECT_EQ(intVal.value(), -42);
}

TEST(JsonTypesTest, StringArrayType)
{
    NL_Json_Array<String, std::string> stringArray;
    nlohmann::json json = {"first", "second", "third"};
    stringArray.fromJson(json);

    std::vector<std::string> result = stringArray.value();
    ASSERT_EQ(result.size(), 3);
    EXPECT_EQ(result[0], "first");
    EXPECT_EQ(result[1], "second");
    EXPECT_EQ(result[2], "third");
}

TEST(JsonTypesTest, IntegerArrayType)
{
    NL_Json_Array<Integer, int32_t> intArray;
    nlohmann::json json = {1, 2, 3, 4, 5};
    intArray.fromJson(json);

    std::vector<int32_t> result = intArray.value();
    ASSERT_EQ(result.size(), 5);
    EXPECT_EQ(result[0], 1);
    EXPECT_EQ(result[4], 5);
}

TEST(JsonTypesTest, BooleanArrayType)
{
    NL_Json_Array<Boolean, bool> boolArray;
    nlohmann::json json = {true, false, true};
    boolArray.fromJson(json);

    std::vector<bool> result = boolArray.value();
    ASSERT_EQ(result.size(), 3);
    EXPECT_TRUE(result[0]);
    EXPECT_FALSE(result[1]);
    EXPECT_TRUE(result[2]);
}

TEST(JsonTypesTest, EmptyArray)
{
    NL_Json_Array<String, std::string> stringArray;
    nlohmann::json json = nlohmann::json::array();
    stringArray.fromJson(json);

    std::vector<std::string> result = stringArray.value();
    EXPECT_TRUE(result.empty());
}

TEST(JsonTypesTest, EnumTypeToString)
{
    // clang-format off
    enum class Color { Red, Green, Blue };
    EnumType<Color> colorMap = {
        {"red", Color::Red},
        {"green", Color::Green},
        {"blue", Color::Blue}
    };
    // clang-format on

    EXPECT_EQ(toString(colorMap, Color::Red), "red");
    EXPECT_EQ(toString(colorMap, Color::Green), "green");
    EXPECT_EQ(toString(colorMap, Color::Blue), "blue");
}

TEST(JsonTypesTest, EnumTypeToStringNotFound)
{
    // clang-format off
    enum class Status { Active, Inactive };
    EnumType<Status> statusMap = {
        {"active", Status::Active}
    };
    // clang-format on

    std::string result = toString(statusMap, Status::Inactive);
    EXPECT_TRUE(result.empty());
}

TEST(JsonTypesTest, FloatArrayType)
{
    NL_Json_Array<Float, float> floatArray;
    nlohmann::json json = {1.1, 2.2, 3.3};
    floatArray.fromJson(json);

    std::vector<float> result = floatArray.value();
    ASSERT_EQ(result.size(), 3);
    EXPECT_FLOAT_EQ(result[0], 1.1);
    EXPECT_FLOAT_EQ(result[1], 2.2);
    EXPECT_FLOAT_EQ(result[2], 3.3);
}

TEST(JsonTypesTest, UnsignedArrayType)
{
    NL_Json_Array<Unsigned, uint32_t> unsignedArray;
    nlohmann::json json = {10u, 20u, 30u};
    unsignedArray.fromJson(json);

    std::vector<uint32_t> result = unsignedArray.value();
    ASSERT_EQ(result.size(), 3);
    EXPECT_EQ(result[0], 10u);
    EXPECT_EQ(result[1], 20u);
    EXPECT_EQ(result[2], 30u);
}

TEST(JsonTypesTest, BasicTypeIncorrectPayload)
{
    String str;
    nlohmann::json jsonInt = 123;
    EXPECT_THROW(str.fromJson(jsonInt), nlohmann::json::type_error);

    Integer integer;
    nlohmann::json jsonStr = "not a number";
    EXPECT_THROW(integer.fromJson(jsonStr), nlohmann::json::type_error);
}

TEST(JsonTypesTest, ArrayWithNonArrayPayload)
{
    NL_Json_Array<String, std::string> stringArray;
    nlohmann::json json = {{"key", "value"}};
    EXPECT_THROW(stringArray.fromJson(json), nlohmann::json::type_error);
}

TEST(JsonTypesTest, ArrayWithMixedTypes)
{
    NL_Json_Array<Integer, int32_t> intArray;
    nlohmann::json json = {1, "two", 3};
    EXPECT_THROW(intArray.fromJson(json), nlohmann::json::type_error);
}
