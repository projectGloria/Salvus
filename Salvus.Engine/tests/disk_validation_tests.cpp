#include "disk/disk_validation.hpp"

#include <gtest/gtest.h>

#include <limits>

namespace {

using namespace salvus::disk;
using salvus::common::StatusCode;

TEST(DiskValidationTests, ValidatesSectorSizes) {
    EXPECT_TRUE(IsValidSectorSize(512));
    EXPECT_TRUE(IsValidSectorSize(4096));
    EXPECT_TRUE(IsValidSectorSize(65536));
    EXPECT_FALSE(IsValidSectorSize(0));
    EXPECT_FALSE(IsValidSectorSize(256));
    EXPECT_FALSE(IsValidSectorSize(1000));
    EXPECT_FALSE(IsValidSectorSize(131072));
}

TEST(DiskValidationTests, AcceptsValidAlignedReadRange) {
    const DiskGeometry geom{1024ULL * 1024ULL, 512U};
    const ReadRange range{4096ULL, 8192ULL};
    const auto res = ValidateReadRange(geom, range, true);
    EXPECT_TRUE(res.ok());
}

TEST(DiskValidationTests, RejectsUnalignedOffsetOrLengthWhenRequired) {
    const DiskGeometry geom{1024ULL * 1024ULL, 512U};

    const auto badOffset = ValidateReadRange(geom, ReadRange{100ULL, 512ULL}, true);
    EXPECT_FALSE(badOffset.ok());
    EXPECT_EQ(badOffset.error().code, StatusCode::InvalidArgument);

    const auto badLen = ValidateReadRange(geom, ReadRange{512ULL, 100ULL}, true);
    EXPECT_FALSE(badLen.ok());
    EXPECT_EQ(badLen.error().code, StatusCode::InvalidArgument);
}

TEST(DiskValidationTests, RejectsOutOfBoundsAndOverflowRanges) {
    const DiskGeometry geom{4096ULL, 512U};

    const auto pastEnd = ValidateReadRange(geom, ReadRange{4096ULL, 512ULL}, true);
    EXPECT_FALSE(pastEnd.ok());
    EXPECT_EQ(pastEnd.error().code, StatusCode::OutOfBounds);

    const auto crossesEnd = ValidateReadRange(geom, ReadRange{3584ULL, 1024ULL}, true);
    EXPECT_FALSE(crossesEnd.ok());
    EXPECT_EQ(crossesEnd.error().code, StatusCode::OutOfBounds);

    const auto overflow = ValidateReadRange(
        geom,
        ReadRange{512ULL, (std::numeric_limits<std::uint64_t>::max)() - 511ULL},
        false);
    EXPECT_FALSE(overflow.ok());
    EXPECT_EQ(overflow.error().code, StatusCode::OutOfBounds);
}

TEST(DiskValidationTests, AlignUpDetectsOverflow) {
    const auto overflow = AlignUpToSector((std::numeric_limits<std::uint64_t>::max)(), 512U);
    EXPECT_FALSE(overflow.ok());
    EXPECT_EQ(overflow.error().code, StatusCode::OutOfBounds);

    const auto aligned = AlignUpToSector(513ULL, 512U);
    ASSERT_TRUE(aligned.ok());
    EXPECT_EQ(aligned.value(), 1024ULL);
}

} // namespace
