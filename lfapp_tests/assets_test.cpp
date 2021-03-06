/*
 * $Id$
 * Copyright (C) 2009 Lucid Fusion Labs

 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.

 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.

 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "gtest/gtest.h"
#include "lfapp/lfapp.h"

namespace LFL {
#ifdef LFL_PNG
TEST(ImageFormatTest, PNG) {
    Texture tex(256, 256), tex_in;
    tex.RenewBuffer();
    for (int i=0; i<tex.height; i++)
        for (int j=0; j<tex.width; j++)
            for (int k=0; k<tex.PixelSize(); k++)
                tex.buf[tex.LineSize() * i + j * tex.PixelSize() + k] = i;
    {
        LocalFile lf("/tmp/lfapp_test.png", "w");
        CHECK_EQ(0, PngWriter::Write(&lf, tex));
    }
    {
        LocalFile lf("/tmp/lfapp_test.png", "r");
        CHECK_EQ(0, PngReader::Read(&lf, &tex_in));
    }
    CHECK_EQ(tex_in.width,      tex.width);
    CHECK_EQ(tex_in.height,     tex.height);
    CHECK_EQ(tex_in.pf,         tex.pf);
    CHECK_EQ(tex_in.LineSize(), tex.LineSize());
    CHECK_EQ(0, memcmp(tex.buf, tex_in.buf, tex.LineSize() * tex.height));
}
#endif

TEST(AssetTest, Tiles) {
    int tile_test_a=0, tile_test_b=0, tile_test_c=0, tile_test_d=0, tile_test_e=0, tile_test_f=0, tile_test_g=0;
    int tile_test_h=0, tile_test_i=0, tile_test_j=0, tile_test_k=0, tile_test_l=0, tile_test_m=0;
    Tiles tiles, *T = &tiles;
    T->Run();
    T->ContextOpen();
    TilesPreAdd (T, [&](){ tile_test_a++; });
    TilesPostAdd(T, [&](){ tile_test_i++; });
    T->ContextOpen();
    TilesPreAdd (T, [&](){ tile_test_b++; });
    TilesPostAdd(T, [&](){ tile_test_j++; });

    T->ContextOpen();
    TilesPreAdd (T, [&](){ tile_test_e++; });
    TilesPostAdd(T, [&](){ tile_test_k++; });
    Box b(10, 10, 400, 400);
    TilesAdd(T, &b, [&](){ tile_test_c++; });
    T->ContextClose();

    T->ContextOpen();
    TilesPreAdd (T, [&](){ tile_test_f++; });
    TilesPostAdd(T, [&](){ tile_test_l++; });
    b = Box(10, 520, 400, 400);
    TilesAdd(T, &b, [&](){ tile_test_d++; });
    T->ContextClose();

    T->ContextOpen();
    TilesPreAdd (T, [&](){ tile_test_g++; });
    TilesPostAdd(T, [&](){ tile_test_m++; });
    b = Box(10, 260, 400, 400);
    TilesAdd(T, &b, [&](){ tile_test_h++; });
    T->ContextClose();

    T->ContextClose();
    T->ContextClose();
    T->Run();
    EXPECT_EQ(4, tile_test_c);
    EXPECT_EQ(4, tile_test_d);
    EXPECT_EQ(4, tile_test_e);
    EXPECT_EQ(4, tile_test_k);
    EXPECT_EQ(4, tile_test_f);
    EXPECT_EQ(4, tile_test_l);
    EXPECT_EQ(4, tile_test_g);
    EXPECT_EQ(4, tile_test_m);
    EXPECT_EQ(4, tile_test_h);
    EXPECT_EQ(8, tile_test_a);
    EXPECT_EQ(8, tile_test_i);
    EXPECT_EQ(8, tile_test_b);
    EXPECT_EQ(8, tile_test_j);
}
}; // namespace LFL
