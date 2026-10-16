#include <gtest/gtest.h>

#include <game_share/dynworld/mapgen/mapgen_params.h>

#include <string>
#include <vector>

namespace
{

void insertInt(NLMISC::CConfigFile& config, const std::string& name, int value)
{
    NLMISC::CConfigFile::CVar var;
    var.forceAsInt(value);
    config.insertVar(name, var);
}

void insertNoiseParams(NLMISC::CConfigFile& config, const std::string& name)
{
    NLMISC::CConfigFile::CVar var;
  const std::vector<double> values{1.5, 2.5, 3.0, 4.0, 5.0, 6.0, 7.0, 0.75};
    var.forceAsDouble(values.front());
    for (size_t i = 1; i < values.size(); ++i)
    {
        var.setAsDouble(values[i], static_cast<int>(i));
    }
    config.insertVar(name, var);
}

void populateConfig(NLMISC::CConfigFile& config)
{
    insertInt(config, "Seed", 1234);
    insertInt(config, "WaterLevel", -3);

    NLMISC::CConfigFile::CVar flags;
   flags.forceAsString("Flat");
    flags.setAsString("Caves", 1);
    config.insertVar("Flags", flags);

    for (const char* name : {
        "NP_terrain_base",
        "NP_terrain_higher",
        "NP_steepness",
        "NP_height_select",
        "NP_soil",
        "NP_beach",
        "NP_cave",
        "NP_humidity",
        "NP_trees",
        "NP_apple_trees"})
    {
        insertNoiseParams(config, name);
    }
}

}

TEST(DynworldMapgenParamsTest, InitializesFlagsAndUnusedBiomeNoise)
{
    CMapgenParams params;

    EXPECT_EQ(params.flags, CMapgenParams::Caves);
    EXPECT_EQ(params.np_biome.offset, 0.0f);
    EXPECT_EQ(params.np_biome.scale, 0.0f);
    EXPECT_EQ(params.np_biome.spread.x, 1.0f);
    EXPECT_EQ(params.np_biome.spread.y, 1.0f);
    EXPECT_EQ(params.np_biome.spread.z, 1.0f);
}

TEST(DynworldMapgenParamsTest, LoadsFlagsAndNoiseParameters)
{
    CMapgenParams params;
  NLMISC::CConfigFile config;
    populateConfig(config);

    params.load(config);

    EXPECT_EQ(params.seed, 1234u);
    EXPECT_EQ(params.water_level, -3);
    EXPECT_EQ(params.flags, CMapgenParams::Flat | CMapgenParams::Caves);
    EXPECT_NE(params.flags & CMapgenParams::Flat, 0u);
    EXPECT_NE(params.flags & CMapgenParams::Caves, 0u);
    EXPECT_FLOAT_EQ(params.np_terrain_base.offset, 1.5f);
    EXPECT_FLOAT_EQ(params.np_terrain_base.scale, 2.5f);
    EXPECT_FLOAT_EQ(params.np_terrain_base.spread.x, 3.0f);
    EXPECT_FLOAT_EQ(params.np_terrain_base.spread.y, 4.0f);
    EXPECT_FLOAT_EQ(params.np_terrain_base.spread.z, 5.0f);
    EXPECT_EQ(params.np_terrain_base.seed, 6);
    EXPECT_EQ(params.np_terrain_base.octaves, 7);
    EXPECT_FLOAT_EQ(params.np_terrain_base.persist, 0.75f);
}
