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

struct RValueResolutionTest1 {
    int A, B;
    vector<string> data;
    RValueResolutionTest1(const vector<string>  &in) : data(in), A(1), B(0) {}
};
struct RValueResolutionTest2 {
    int A, B;
    vector<string> data;
    RValueResolutionTest2(const vector<string>  &in) : data(in), A(1), B(0) {}
    RValueResolutionTest2(      vector<string> &&in) : data(in), A(0), B(1) {}
};
TEST(CPlusPlusTest, RValuedResolution) {
    vector<string> foo;
    EXPECT_EQ(1, RValueResolutionTest1(     foo) .A);
    EXPECT_EQ(1, RValueResolutionTest2(     foo) .A);
    EXPECT_EQ(0, RValueResolutionTest1(move(foo)).B);
    EXPECT_EQ(1, RValueResolutionTest2(move(foo)).B);
    EXPECT_EQ(0, RValueResolutionTest1(vector<string>()).B);
    EXPECT_EQ(1, RValueResolutionTest2(vector<string>()).B);
}
    
struct StringMethodResolutionTest {
    int A=0, B=0, C=0, D=0;
    template <class X> void F(const StringPieceT<X> &text) { A++; }
    /**/               void F(const string          &text) { B++; F(StringPiece           (text)); }
    /**/               void F(const String16        &text) { C++; F(String16Piece         (text)); }
    template <class X> void F(const X               *text) { D++; F(StringPiece::Unbounded(text)); }
};
TEST(CPlusPlusTest, MethodResolution) {
    StringMethodResolutionTest t;
    t.F("a");             EXPECT_EQ(1, t.A); EXPECT_EQ(0, t.B); EXPECT_EQ(0, t.C); EXPECT_EQ(1, t.D);
    t.F(string());        EXPECT_EQ(2, t.A); EXPECT_EQ(1, t.B); EXPECT_EQ(0, t.C); EXPECT_EQ(1, t.D);
    t.F(String16());      EXPECT_EQ(3, t.A); EXPECT_EQ(1, t.B); EXPECT_EQ(1, t.C); EXPECT_EQ(1, t.D);
    t.F(StringPiece());   EXPECT_EQ(4, t.A); EXPECT_EQ(1, t.B); EXPECT_EQ(1, t.C); EXPECT_EQ(1, t.D);
    t.F(String16Piece()); EXPECT_EQ(5, t.A); EXPECT_EQ(1, t.B); EXPECT_EQ(1, t.C); EXPECT_EQ(1, t.D);
}

}; // namespace LFL
