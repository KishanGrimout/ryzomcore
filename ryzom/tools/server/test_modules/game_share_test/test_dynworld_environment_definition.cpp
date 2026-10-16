#include <gtest/gtest.h>

#include <game_share/dynworld/environment_definition.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <limits>
#include <string>

TEST(DynworldEnvironmentDefinitionTest, LoadsEnvironmentConfig)
{
    const auto fileName = std::string("ryzom_environment_")
        + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())
        + ".cfg";
    const auto filePath = std::filesystem::temp_directory_path() / fileName;
    std::ofstream configFile(filePath);
    ASSERT_TRUE(configFile.is_open());
    configFile << "Name = \"Test Environment\";\n"
        << "Id = 17;\n"
        << "DefaultSoil = \"Sand\";\n"
        << "Seed = 42;\n"
        << "WaterLevel = 3;\n"
        << "Flags = { \"Flat\" };\n"
        << "BigFlora = { 101, 202 };\n"
        << "SmallFlora = { 303 };\n";
    configFile.close();

    const std::string configPath = filePath.string();
    CEnvironmentDefinition environment;
    environment.initialize(configPath.c_str());
    std::filesystem::remove(filePath);

    EXPECT_STREQ(environment.getName(), "Test Environment");
    EXPECT_EQ(environment.getId(), 17u);
    EXPECT_STREQ(environment.getDefaultSoilName(), "Sand");
    EXPECT_EQ(environment.getDefaultSoilId(), std::numeric_limits<uint>::max());
    EXPECT_EQ(environment.getMapgenParams().seed, 42u);
    EXPECT_EQ(environment.getMapgenParams().water_level, 3);
    EXPECT_EQ(environment.getMapgenParams().flags, CMapgenParams::Flat);
    EXPECT_FLOAT_EQ(environment.getMinGroundHeight(), -42.0f);
    EXPECT_FLOAT_EQ(environment.getMaxGroundHeight(), 58.0f);
    ASSERT_EQ(environment.getBigFloraSheetIds().size(), 2u);
    EXPECT_EQ(environment.getBigFloraSheetIds()[0], 101u);
    EXPECT_EQ(environment.getBigFloraSheetIds()[1], 202u);
    ASSERT_EQ(environment.getSmallFloraSheetIds().size(), 1u);
    EXPECT_EQ(environment.getSmallFloraSheetIds()[0], 303u);

    environment.setDefaultSoilId(55);
    EXPECT_EQ(environment.getDefaultSoilId(), 55u);
}
