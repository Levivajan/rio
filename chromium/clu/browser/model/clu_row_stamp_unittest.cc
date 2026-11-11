// Copyright 2026 The Rio Authors. All rights reserved.

#include "clu/browser/model/clu_row_stamp.h"

#include <string>

#include "testing/gtest/include/gtest/gtest.h"

// The stamp read back from a session or tab restore file: only a row id gives a restored page its row. The round
// trip through real entries runs in clu_browsertests (CluRestoreAPageCarriesItsRowIdIntoChromiumsRestoreFiles).
namespace clu {
namespace {

TEST(CluRowStampTest, OnlyARowIdComesBack) {
  EXPECT_EQ(ParseRowStamp("0a1b2c3d-4e5f-4a6b-9c7d-8e9fa0b1c2d3"),
            "0a1b2c3d-4e5f-4a6b-9c7d-8e9fa0b1c2d3");
  // Either case, lowercased like every row id.
  EXPECT_EQ(ParseRowStamp("0A1B2C3D-4E5F-4A6B-9C7D-8E9FA0B1C2D3"),
            "0a1b2c3d-4e5f-4a6b-9c7d-8e9fa0b1c2d3");
  EXPECT_FALSE(ParseRowStamp(""));
  EXPECT_FALSE(ParseRowStamp("not-a-row"));
  EXPECT_FALSE(ParseRowStamp("0a1b2c3d-4e5f-1a6b-9c7d-8e9fa0b1c2d3"));  // v1
  EXPECT_FALSE(ParseRowStamp("0a1b2c3d-4e5f-4a6b-9c7d-8e9fa0b1c2d3 "));
  EXPECT_FALSE(ParseRowStamp(std::string(4096, 'a')));
  EXPECT_FALSE(ParseRowStamp(std::string("0a1b2c3d-4e5f-4a6b-9c7d-8e9fa0b1c2\0d", 36)));
}

}  // namespace
}  // namespace clu
