/*
 * $Id: corpus.h 1306 2014-09-04 07:13:16Z justin $
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

#ifndef __LFL_ML_CORPUS_H__
#define __LFL_ML_CORPUS_H__
namespace LFL {

struct Corpus {
    Callback start_cb, finish_cb;
    virtual ~Corpus() {}
    virtual void RunFile(const string &filename) {}
    virtual void Run(const string &file_or_dir) {
        if (start_cb) start_cb();
        if (!file_or_dir.empty() && !LocalFile::IsDirectory(file_or_dir)) RunFile(file_or_dir);
        else {
            DirectoryIter iter(file_or_dir, -1);
            for (const char *fn = iter.Next(); Running() && fn; fn = iter.Next()) Run(StrCat(file_or_dir, fn));
        }
        if (finish_cb) finish_cb();
    }  
};  

}; // namespace LFL
#endif // __LFL_ML_CORPUS_H__
