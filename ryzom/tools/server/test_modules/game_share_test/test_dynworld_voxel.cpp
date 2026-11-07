#include <gtest/gtest.h>

#include <game_share/dynworld/voxel.h>
#include <nel/misc/mem_stream.h>

TEST(DynworldVoxelTest, StoresRawMaterialBiomeAndDensity)
{
    Voxel voxel;
    voxel.setType(Voxel::Raw);
    voxel.setBiome(5);
    voxel.setMaterial(0xAB);
    voxel.setDensity(0xCD);

    EXPECT_EQ(voxel.getType(), Voxel::Raw);
    EXPECT_EQ(voxel.getBiome(), 5);
    EXPECT_EQ(voxel.getMaterial(), 0xAB);
    EXPECT_EQ(voxel.getMaterialAndBiome(), 0x05AB);
    EXPECT_EQ(voxel.getDensity(), 0xCD);
}

TEST(DynworldVoxelTest, StoresLiquidAndObjectTypes)
{
    Voxel liquid;
    liquid.setType(Voxel::Liquid);
    liquid.setLiquidType(Voxel::Sap);
    liquid.setDensity(37);
    EXPECT_EQ(liquid.getLiquidType(), Voxel::Sap);
    EXPECT_EQ(liquid.getDensity(), 37);

    Voxel object;
    object.setType(Voxel::Object);
    object.setObjectType(Voxel::Dynamic);
    object.setSheetId(0x123456);
    EXPECT_EQ(object.getObjectType(), Voxel::Dynamic);
    EXPECT_EQ(object.getSheetId(), 0x123456);
}

TEST(DynworldVoxelTest, StoresAirColorWithZeroAlpha)
{
    Voxel voxel;
    voxel.setColor(NLMISC::CRGBA(12, 34, 56, 78));

    NLMISC::CRGBA color;
    voxel.getColor(color);
    EXPECT_EQ(color.R, 12);
    EXPECT_EQ(color.G, 34);
    EXPECT_EQ(color.B, 56);
    EXPECT_EQ(color.A, 0);
}

TEST(DynworldVoxelTest, PreservesPackedValueAndSerializedByteOrder)
{
    Voxel voxel(0xD9563412);

    EXPECT_EQ(voxel.getBiome(), 1);
    EXPECT_EQ(voxel.getType(), Voxel::Raw);
    EXPECT_EQ(voxel.getDensity(), 0x12);
    EXPECT_EQ(voxel.getMaterial(), 0x34);
    EXPECT_EQ(voxel.getRawValue(), 0xD9563412);

    NLMISC::CMemStream stream;
    voxel.serial(stream);
    ASSERT_EQ(stream.length(), 4);
    EXPECT_EQ(stream.buffer()[0], 0xD9);
    EXPECT_EQ(stream.buffer()[1], 0x12);
    EXPECT_EQ(stream.buffer()[2], 0x34);
    EXPECT_EQ(stream.buffer()[3], 0x56);

    stream.invert();
    Voxel deserialized;
    deserialized.serial(stream);
    EXPECT_EQ(deserialized.getRawValue(), voxel.getRawValue());
}
