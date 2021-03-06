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

#ifndef __LFL_LFAPP_FILE_H__
#define __LFL_LFAPP_FILE_H__
namespace LFL {

struct MIMEType {
    static bool Jpg(const string &mt) { return mt == "image/jpg" || mt == "image/jpeg"; }
    static bool Png(const string &mt) { return mt == "image/png"; }
};

struct FileSuffix {
    static bool HTML (const string &url) { return SuffixMatch(url, ".html", 0) || SuffixMatch(url, ".txt", 0); }
    static bool Image(const string &url) { return Jpg(url) || Png(url) || Gif(url) || Bmp(url); }
    static bool Jpg  (const string &url) { return SuffixMatch(url, ".jpg", 0) || SuffixMatch(url, ".jpeg", 0); }
    static bool Png  (const string &url) { return SuffixMatch(url, ".png", 0); }
    static bool Gif  (const string &url) { return SuffixMatch(url, ".gif", 0); }
    static bool Bmp  (const string &url) { return SuffixMatch(url, ".bmp", 0); }
};

struct File {
    virtual ~File() {}
    virtual bool Opened() = 0;
    virtual void Close() = 0;
    virtual bool Open(const string &path, const string &mode, bool pre_create=0) = 0;
    virtual const char *Filename() const = 0;
    virtual int Size() = 0;
    virtual void Reset() = 0;

    struct Whence { enum { SET=SEEK_SET, CUR=SEEK_CUR, END=SEEK_END }; int x; };
    virtual long long Seek(long long offset, int whence) = 0;
    virtual int Read(void *buf, size_t size) = 0;
    virtual int Write(const void *buf, size_t size=-1) = 0;
    virtual bool Flush() { return false; }

    struct NextRecord { 
        string buf;
        bool buf_dirty;
        int buf_offset, file_offset, record_offset, record_len;
        NextRecord() { Reset(); }
        void Reset() { buf.clear(); buf_dirty = 0; buf_offset = file_offset = record_offset = record_len = 0; }
        void SetFileOffset(int v) { file_offset = v; buf_dirty = 1; }
        typedef const char* (*NextRecordCB)(const StringPiece&, bool, int *);
        const char *GetNextRecord(File *f, int *offset, int *nextoffset, NextRecordCB cb); 
    } nr;

    string Contents();
    const char *NextLine   (int *offset=0, int *nextoffset=0);
    const char *NextLineRaw(int *offset=0, int *nextoffset=0);
    const char *NextChunk  (int *offset=0, int *nextoffset=0);
    const char *NextProto  (int *offset=0, int *nextoffset=0, ProtoHeader *phout=0);

    int Write(const string &b) { return Write(b.c_str(), b.size()); }
    int WriteProto(ProtoHeader *hdr, const Proto *msg, bool flush=0);
    int WriteProto(const ProtoHeader *hdr, const Proto *msg, bool flush=0);
    int WriteProtoFlag(const ProtoHeader *hdr, bool flush=0);

    static bool ReadSuccess(File *f, void *out, int len) { return f->Read(out, len) == len; }
    static bool SeekSuccess(File *f, long long pos) { return f->Seek(pos, Whence::SET) == pos; }
    static bool SeekReadSuccess(File *f, long long pos, void *out, int len) { return SeekSuccess(f, pos) ? ReadSuccess(f, out, len) : false; }
};

struct BufferFile : public File {
    string buf, fn;
    int rdo, wro;
    BufferFile(const string &s, const char *FN=0) : buf(s), fn(FN?FN:""), rdo(0), wro(0) {}
    ~BufferFile() { Close(); }

    bool Opened() { return true; }
    bool Open(const string &path, const string &mode, bool pre_create=0) { return false; }
    const char *Filename() const { return fn.c_str(); }
    int Size() { return buf.size(); }
    void Reset() { rdo=wro=0; nr.Reset(); }
    void Close() { buf.clear(); Reset(); }

    long long Seek(long long pos, int whence);
    int Read(void *out, size_t size);
    int Write(const void *in, size_t size=-1);
};

struct LocalFile : public File {
    void *impl;
    string fn;
    bool writable;
    virtual ~LocalFile() { Close(); }
    LocalFile() : impl(0), writable(0) {}
    LocalFile(const string &path, const string &mode, bool pre_create=0) : impl(0) { Open(path, mode, pre_create); }
    static int WhenceMap(int n);

    static const char Slash;
    static bool mkdir(const string &dir, int mode);
    static int IsDirectory(const string &localfilename);
    static string CurrentDirectory(int max_size=1024);
    static string JoinPath(const string &x, const string &y);
    static string FileContents(const string &localfilename) { return LocalFile(localfilename, "r").Contents(); }
    static int WriteFile(const string &path, const StringPiece &sp) {
        LocalFile file(path, "w");
        return file.Opened() ? file.Write(sp.data(), sp.size()) : -1;
    }

    bool Opened() { return impl; }
    bool Open(const string &path, const string &mode, bool pre_create=0);
    const char *Filename() const { return fn.c_str(); }
    int Size();
    void Reset();
    void Close();

    long long Seek(long long pos, int whence);
    int Read(void *buf, size_t size);
    int Write(const void *buf, size_t size=-1);
    bool Flush();
};

struct FileLineIter : public StringIter {
    File *f;
    FileLineIter(File *F) : f(F) {}
    const char *Next() { return f->NextLine(); }
    void Reset() { f->Reset(); }
    bool Done() const { return f->nr.buf_offset < 0; }
    const char *Begin() const { return 0; }
    const char *Current() const { return f->nr.buf.c_str() + f->nr.record_offset; }
    int CurrentOffset() const { return f->nr.file_offset; }
    int CurrentLength() const { return f->nr.record_len; }
    int TotalLength() const { return 0; }
};

struct LocalFileLineIter : public StringIter {
    LocalFile f;
    LocalFileLineIter(const string &path) : f(path, "r") {};
    const char *Next() { return f.NextLine(); }
    void Reset() { f.Reset(); }
    bool Done() const { return f.nr.buf_offset < 0; }
    const char *Begin() const { return 0; }
    const char *Current() const { return f.nr.buf.c_str() + f.nr.record_offset; }
    int CurrentOffset() const { return f.nr.file_offset; }
    int CurrentLength() const { return f.nr.record_len; }
    int TotalLength() const { return 0; }
};
   
struct BufferFileLineIter : public StringIter {
    BufferFile f;
    BufferFileLineIter(const string &s) : f(s) {};
    const char *Next() { return f.NextLine(); }
    void Reset() { f.Reset(); }
    bool Done() const { return f.nr.buf_offset < 0; }
    const char *Begin() const { return 0; }
    const char *Current() const { return f.nr.buf.c_str() + f.nr.record_offset; }
    int CurrentOffset() const { return f.nr.file_offset; }
    int CurrentLength() const { return f.nr.record_len; }
    int TotalLength() const { return 0; }
};

struct DirectoryIter {
    typedef map<string, int> Map;
    string pathname;
    Map filemap;
    Map::iterator iter;
    const char *P, *S;
    bool init;
    DirectoryIter() : P(0), S(0), init(0) {}
    DirectoryIter(const string &path, int dirs=0, const char *FilePrefix=0, const char *FileSuffix=0);
    const char *Next();
    static void Add(void *self, const char *k, int v) { ((DirectoryIter*)self)->filemap[k] = v; }
};

struct ArchiveIter {
    void *impl, *entry, *dat;
    ArchiveIter(const char *path);
    ArchiveIter(){};
    ~ArchiveIter();
    void Skip();
    const char *Next();
    long long Size();
    const void *Data();
};

struct VersionedFileName {
    const char *dir, *_class, *var;
    VersionedFileName(const char *D=0, const char *C=0, const char *V=0) : dir(D), _class(C), var(V) {}
};

struct ProtoHeader {
    int flag, len; 
    ProtoHeader() : len(0) { SetFlag(0); }
    ProtoHeader(int f) : len(0) { SetFlag(f); }
    ProtoHeader(const char *text) {
        memcpy(&flag, text, sizeof(int));
        memcpy(&len, text+sizeof(int), sizeof(int));
        Validate();
    }
    void Validate() const { if (((flag>>16)&0xffff) != magic) FATAL("magic check"); }
    void SetLength(int v) { Validate(); len = v; }
    void SetFlag(unsigned short v) { flag = (magic<<16) | v; }
    unsigned short GetFlag() const { return flag & 0xffff; }
    static const int size = sizeof(int)*2, magic = 0xfefe;
};

struct ProtoFile {
    File *file; int read_offset, write_offset; bool done;
    ProtoFile(const char *fn=0) : file(0) { Open(fn); }
    ~ProtoFile() { delete file; }
    bool Opened() { return file && file->Opened(); }
    void Open(const char *fn);
    int Add(const Proto *msg, int status);
    bool Update(int offset, const ProtoHeader *ph, const Proto *msg);
    bool Update(int offset, int status);
    bool Get(Proto *out, int offset, int status=-1);
    bool Next(Proto *out, int *offsetOut=0, int status=-1);
    bool Next(ProtoHeader *hdr, Proto *out, int *offsetOut=0, int status=-1);
};

struct StringFile {
    vector<string> *F;
    string H;
    StringFile() { Clear(); }
    StringFile(vector<string> *f, const string &h=string()) : F(f), H(h) {}
    ~StringFile() { delete F; }

    void Clear() { F=0; H.clear(); }
    void Print(const string &name, bool nl=1);
    int Lines() const { return F ? F->size() : 0; }
    string Line(int i) const { return (F && i < F->size()) ? (*F)[i] : ""; }
    void AssignTo(vector<string> **Fo, string *Ho) { if (Fo) *Fo=F; if (Ho) *Ho=H; Clear(); }

    int ReadVersioned (const VersionedFileName &fn, int iter=-1);
    int WriteVersioned(const VersionedFileName &fn, int iter, const string &hdr=string());
    int WriteVersioned(const char *D, const char *C, const char *V, int iter, const string &hdr=string())
    { return WriteVersioned(VersionedFileName(D, C, V), iter, hdr); }

    int Read(const string &path, int header=1);
    int Read(IterWordIter *word, int header);

    int Write(File         *file, const string &name);
    int Write(const string &path, const string &name);
    static int WriteRow(File *file, const string &rowval);

    static int Read(const string &fn, vector<string> **F, string *H)
    { StringFile f; int ret=f.Read(fn); f.AssignTo(F, H); return ret; }
    static int ReadVersioned(const VersionedFileName &fn, vector<string> **F, string *H, int iter=-1)
    { StringFile f; int ret=f.ReadVersioned(fn); f.AssignTo(F, H); return ret; }
    static int ReadVersioned(const char *D, const char *C, const char *V, vector<string> **F, string *H, int iter=-1)
    { return ReadVersioned(VersionedFileName(D, C, V), F, H, iter); }
};

struct SettingsFile {
    static const char *VarType() { return "string"; }
    static const char *VarName() { return "settings"; }
    static const char *Separator() { return " = "; }

    static int Read(const string &dir, const string &name);
    static int Write(const vector<string> &fields, const string &dir, const string &name);
};

struct MatrixFile {
    struct Header { enum { NONE=0, DIM_PLUS=1, DIM=2 }; };
    struct BinaryHeader{ int magic, M, N, name, transcript, data, unused1, unused2; };

    Matrix *F; string H;
    MatrixFile() { Clear(); }
    MatrixFile(Matrix *f, const string &h=string()) : F(f), H(h) {}
    ~MatrixFile() { delete F; }

    void Clear() { F=0; H.clear(); }
    const char *Text() { return H.c_str(); }
    void AssignTo(Matrix **Fo, string *Ho) { if (Fo) *Fo=F; if (Ho) *Ho=H; Clear(); }

    int ReadVersioned       (const VersionedFileName &fn, int iteration=-1);
    int WriteVersioned      (const VersionedFileName &fn, int iteration);
    int WriteVersionedBinary(const VersionedFileName &fn, int iteration);
    int WriteVersioned(const char *D, const char *C, const char *V, int iter)
    { return WriteVersioned(VersionedFileName(D, C, V), iter); }
    int WriteVersionedBinary(const char *D, const char *C, const char *V, int iter)
    { return WriteVersionedBinary(VersionedFileName(D, C, V), iter); }

    int Read(IterWordIter *word, int header=1);
    int Read(const string &path, int header=1, int (*IsSpace)(int)=0);
    int ReadBinary(const string &path);

    int Write      (File         *file, const string &name);
    int Write      (const string &path, const string &name);
    int WriteBinary(File         *file, const string &name);
    int WriteBinary(const string &path, const string &name);

    static string Filename(const VersionedFileName &fn, const string &suf, int iter) { return Filename(fn._class, fn.var, suf, iter); }
    static string Filename(const string &_class, const string &var, const string &suffix, int iteration);
    static int FindHighestIteration(const VersionedFileName &fn, const string &suffix);
    static int FindHighestIteration(const VersionedFileName &fn, const string &suffix1, const string &suffix2);
    static int ReadHeader    (IterWordIter *word, string *hdrout);
    static int ReadDimensions(IterWordIter *word, int *M, int *N);
    static int WriteHeader      (File *file, const string &name, const string &hdr, int M, int N);
    static int WriteBinaryHeader(File *file, const string &name, const string &hdr, int M, int N);
    static int WriteRow         (File *file, const double *row, int N, bool lastrow=0);

    static int Read(IterWordIter *fd, Matrix **F, string *H)
    { MatrixFile f; int ret=f.Read(fd); f.AssignTo(F, H); return ret; }
    static int Read(const string &fn, Matrix **F, string *H)
    { MatrixFile f; int ret=f.Read(fn); f.AssignTo(F, H); return ret; }
    static int ReadVersioned(const VersionedFileName &fn, Matrix **F, string *H, int iter=-1)
    { MatrixFile f; int ret=f.ReadVersioned(fn); f.AssignTo(F, H); return ret; }
    static int ReadVersioned(const char *D, const char *C, const char *V, Matrix **F, string *H=0, int iter=-1)
    { return ReadVersioned(VersionedFileName(D, C, V), F, H, iter); }
};

struct MatrixArchiveOut {
    File *file;
    MatrixArchiveOut(const string &name=string());
    ~MatrixArchiveOut();

    void Close();
    int Open(const string &name);
    int Write(Matrix*, const string &hdr, const string &name);
    int Write(const MatrixFile *f, const string &name) { return Write(f->F, f->H, name); }
};

struct MatrixArchiveIn {
    IterWordIter *file; int index;
    MatrixArchiveIn(const string &name=string());
    ~MatrixArchiveIn();

    void Close();
    int Open(const string &name);
    int Read(Matrix **out, string *hdrout);
    int Read(MatrixFile *f) { return Read(&f->F, &f->H); }
    int Skip();
    string Filename();
    static int Count(const string &name);
};

template <class X, void (*Assign)(double *, X), bool (*Equals)(const double*, X)>
struct HashMatrixT {
    Matrix *map;
    int VPE;
    HashMatrixT(Matrix *M=0, int vpe=0) : map(M), VPE(vpe) {}

    const double *Get(X hash) const {
        const double *hashrow = map->row(hash % map->M);
        for (int k=0, l=map->N/VPE; k<l; k++) if (Equals(&hashrow[k*VPE], hash)) return &hashrow[k*VPE];
        return 0;
    }
    double *Get(X hash) {
        double *hashrow = map->row(hash % map->M);
        for (int k=0, l=map->N/VPE; k<l; k++) if (Equals(&hashrow[k*VPE], hash)) return &hashrow[k*VPE];
        return 0;
    }
    double *Set(X hash) {
        long long ind = hash % map->M;
        double *hashrow = map->row(ind);
        for (int k=0; k<map->N/VPE; k++) {
            int hri = k*VPE;
            if (hashrow[hri]) {
                if (Equals(&hashrow[hri], hash)) { ERROR("hash collision or duplicate insert ", hash); break; }
                continue;
            }
            Assign(&hashrow[hri], hash);
            return &hashrow[hri];
        }
        return 0;
    }
};

template <class X, void (*Assign)(double *, X), bool (*Equals)(const double*, X)>
struct HashMatrixFileT {
    File *lf;
    int M, N, hdr_size, VPE;
    HashMatrixFileT(File *F=0, int m=0, int n=0, int hs=0, int vpe=0) : lf(F), M(m), N(n), hdr_size(hs), VPE(vpe) {}

    double *SetBinary(X hash, double *hashrow) {
        long long ind = hash % M, row_size = N * sizeof(double), offset = hdr_size + ind * row_size, ret;
        if ((ret = lf->Seek(offset, File::Whence::SET)) != offset) { ERROR("seek: ", offset,   " != ", ret); return 0; } 
        if ((ret = lf->Read(hashrow, row_size))       != row_size) { ERROR("read: ", row_size, " != ", ret); return 0; }

        for (int k=0; k<N/VPE; k++) {
            int hri = k*VPE;
            if (hashrow[hri]) {
                if (Equals(&hashrow[hri], hash)) { ERROR("hash collision or duplicate insert ", hash); break; }
                continue;
            }
            int hri_offset = offset + hri * sizeof(double);
            if ((ret = lf->Seek(hri_offset, File::Whence::SET)) != hri_offset) { ERROR("seek: ", hri_offset, " != ", ret); return 0; } 
            Assign(&hashrow[hri], hash);
            return &hashrow[hri];
        }
        return 0;
    }
    void SetBinaryFlush(const double *hashrow) {
        int write_size = VPE * sizeof(double);
        if (lf->Write(hashrow, write_size) != write_size) ERROR("read: ", write_size);
    }
};

struct HashMatrixF {
    static void Assign(/**/  double *hashrow, unsigned hash) { if (1) hashrow[0] =  hash; }
    static bool Equals(const double *hashrow, unsigned hash) { return hashrow[0] == hash; }
};
struct HashMatrix     : public HashMatrixT    <unsigned, &HashMatrixF::Assign, &HashMatrixF::Equals> { HashMatrix(Matrix *M=0, int vpe=0) : HashMatrixT(M,vpe) {} };
struct HashMatrixFile : public HashMatrixFileT<unsigned, &HashMatrixF::Assign, &HashMatrixF::Equals> {};

struct HashMatrix64F {
    static void Assign(double *hashrow, unsigned long long hash) {
        hashrow[0] = static_cast<unsigned>(hash>>32);
        hashrow[1] = static_cast<unsigned>(hash&0xffffffff);
    }
    static bool Equals(const double *hashrow, unsigned long long hash) {
        return hashrow[0] == static_cast<unsigned>(hash>>32) &&
               hashrow[1] == static_cast<unsigned>(hash&0xffffffff);
    }
};
struct HashMatrix64     : public HashMatrixT    <unsigned long long, &HashMatrix64F::Assign, &HashMatrix64F::Equals> {};
struct HashMatrix64File : public HashMatrixFileT<unsigned long long, &HashMatrix64F::Assign, &HashMatrix64F::Equals> {};

struct GraphVizFile {
    static string DigraphHeader(const string &name);
    static string NodeColor(const string &s);
    static string NodeShape(const string &s);
    static string NodeStyle(const string &s);
    static string Footer();
    static void AppendNode(string *out, const string &n1, const string &label=string());
    static void AppendEdge(string *out, const string &n1, const string &n2, const string &label=string());
};

}; // namespace LFL
#endif // __LFL_LFAPP_FILE_H__
