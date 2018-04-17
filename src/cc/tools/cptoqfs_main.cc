//---------------------------------------------------------- -*- Mode: C++ -*-
// $Id$
//
// Created 2006/06/23
// Author: Sriram Rao
//         Mike Ovsiannikov
//
// Copyright 2008-2012,2016 Quantcast Corporation. All rights reserved.
// Copyright 2006-2008 Kosmix Corp.
//
// This file is part of Kosmos File System (KFS).
//
// Licensed under the Apache License, Version 2.0
// (the "License"); you may not use this file except in compliance with
// the License. You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or
// implied. See the License for the specific language governing
// permissions and limitations under the License.
//
// \brief Tool that copies a file/directory from a local file system to
// KFS.  This tool is analogous to dump---backup a directory from a
// file system into KFS.
//
//----------------------------------------------------------------------------

#include "libclient/KfsClient.h"
#include "common/MsgLogger.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <dirent.h>

#include <iostream>
#include <cerrno>

namespace KFS
{
using std::cout;
using std::string;

class CpToKfs
{
public:
    CpToKfs()
        : mKfsClient(0),
          mTestNumReWrites(-1),
          mNumReplicas(3),
          mDryRunFlag(false),
          mIgnoreSrcErrorsFlag(false),
          mAppendMode(false),
          mBufSize(8 << 20),
          mKfsBufSize(4 << 20),
          mTruncateFlag(false),
          mDeleteFlag(false),
          mCreateExclusiveFlag(false),
          mReadBuf(0),
          mStriperType(KFS_STRIPED_FILE_TYPE_NONE),
          mStripeSize(0),
          mNumStripes(0),
          mNumRecoveryStripes(0),
          mMinSTier(kKfsSTierMax),
          mMaxSTier(kKfsSTierMax),
          mStartPos(0)
    {}
    ~CpToKfs()
    {
        delete mKfsClient;
        delete [] mReadBuf;
    }

    int Run(int argc, char **argv);

private:
    KfsClient* mKfsClient;
    int        mTestNumReWrites;
    int        mNumReplicas;
    bool       mDryRunFlag;
    bool       mIgnoreSrcErrorsFlag;
    bool       mAppendMode;
    int        mBufSize;
    int        mKfsBufSize;
    bool       mTruncateFlag;
    bool       mDeleteFlag;
    bool       mCreateExclusiveFlag;
    char*      mReadBuf;
    int        mStriperType;
    int        mStripeSize;
    int        mNumStripes;
    int        mNumRecoveryStripes;
    kfsSTier_t mMinSTier;
    kfsSTier_t mMaxSTier;
    int64_t    mStartPos;

    bool Mkdirs(string path);

    //
    // For the purpose of the cp -r, take the leaf from sourcePath and
    // make that directory in kfsPath.
    //
    bool MakeKfsLeafDir(string sourcePath, string kfsPath);

    //
    // Given a file defined by sourcePath, copy it to KFS as defined by
    // kfsPath
    //
    int BackupFile(string sourcePath, string kfsPath);

    // Given a dirname, backit up it to dirname.  Dirname will be created
    // if it doesn't exist.
    int BackupDir(string dirname, string kfsdirname);

    // Guts of the work
    int BackupFile2(string srcfilename, string kfsfilename);

    void ReportError(const char* what, string fname, int err)
    {
        cout <<
            (what ? what : "") <<
            " " << fname <<
            " " << ErrorCodeToStr(err) << "\n";
    }
};

int
CpToKfs::Run(int argc, char **argv)  //运行时候的主要函数
{
    string              kfsPath;
    string              serverHost;
    string              sourcePath;
    int                 port       = -1;
    bool                help       = false;
    MsgLogger::LogLevel logLevel   = MsgLogger::kLogLevelINFO;
    int                 maxRetry   = -1;
    int                 retryDelay = -1;
    int                 opTimeout  = -1;
    const char*         config     = 0;
    int                 optchar;

    while ((optchar = getopt(argc, argv,
            "d:hk:p:s:W:r:vniatxXb:w:u:y:z:R:D:T:Sm:l:B:f:F:")) != -1) {
        switch (optchar) {
            case 'd':
                sourcePath = optarg; //-d 之后输入的文件源路径
                break;
            case 'k':
                kfsPath = optarg; //-k 之后输入的QFS中的存储路径
                break;
            case 'p':
                port = atoi(optarg);  //MetaServer的端口
                break;
            case 's':
                serverHost = optarg;  //MetaServer的HostIP地址
                break;
            case 'h':     // 打印帮助
                help = true;
                break;
            case 'v':
                logLevel = MsgLogger::kLogLevelDEBUG;
                break;
            case 'r':
                mNumReplicas = atoi(optarg);
                break;
            case 'W':
                mTestNumReWrites = atoi(optarg);
                break;
            case 'n':
                mDryRunFlag = true;
                break;
            case 'i':
                mIgnoreSrcErrorsFlag = true;
                break;
            case 'a':
                mAppendMode = true;
                break;
            case 'b':
                mBufSize = (int)atof(optarg);
                break;
            case 'w':
                mKfsBufSize = (int)atof(optarg);
                break;
            case 't':
                mTruncateFlag = true;
                break;
            case 'x':
                mDeleteFlag = true;
                break;
            case 'u':
                mStripeSize = atol(optarg);
                break;
            case 'y':
                mNumStripes = atol(optarg);
                break;
            case 'z':
                mNumRecoveryStripes = atol(optarg);
                break;
            case 'S':                 //使用RS Encoding时候的参数设定
                mStripeSize         = 64 << 10; //stripe的大小为64KB
                mNumStripes         = 6;
                mNumRecoveryStripes = 3;
                mNumReplicas        = 1;
                break;
            case 'R':
                maxRetry = (int)atof(optarg);
                break;
            case 'D':
                retryDelay = (int)atof(optarg);
                break;
            case 'T':
                opTimeout = (int)atof(optarg);
                break;
            case 'X':
                mCreateExclusiveFlag = true;
                break;
            case 'm':
                mMinSTier = (kfsSTier_t)atol(optarg);
                break;
            case 'l':
                mMaxSTier = (kfsSTier_t)atol(optarg);
                break;
            case 'B':
                mStartPos = (int64_t)strtoll(optarg, 0, 0);
                break;
            case 'F':
                mStriperType = atoi(optarg);
                break;
            case 'f':
                config = optarg;
                break;
            default:
                help = true;
                break;
        }
    }

    if (help || sourcePath.empty() || kfsPath.empty() || serverHost.empty() ||
            port <= 0 || mBufSize < 1 ||
                (mAppendMode && mBufSize > (64 << 20))) {
        cout << "Usage: " << argv[0] << "\n"
            " -s   -- meta server name or ip\n"
            " -p   -- meta server port\n"
            " -d   -- source path; \"-\" means stdin\n"
            " -k   -- destination (qfs) path\n"
            " [-v] -- verbose debug trace\n"
            " [-r] -- replication factor; default 3\n"
            " [-W] -- testing -- number test rewrites\n"
            " [-n] -- dry run\n"
            " [-a] -- append\n"
            " [-b] -- input buffer size in bytes; default is 8MB\n"
            " [-w] -- qfs write buffer size in bytes; default is 4MB,"
                " or 1MB per stripe\n"
            " [-t] -- truncate destination files if exist\n"
            " [-x] -- delete destination files if exist\n"
            " [-u] -- stripe size\n"
            " [-y] -- data stripes count\n"
            " [-z] -- recovery stripes count (0 or 3 with file type 2)\n"
            " [-S] -- 6+3 RS 64KB stripes 1 replica\n"
            " [-R] -- op retry count, default -1 -- qfs client default\n"
            " [-D] -- op retry delay, default -1 -- qfs client default\n"
            " [-T] -- op timeout, default -1 -- qfs client default\n"
            " [-X] -- create exclusive\n"
            " [-m] -- min storage tier\n"
            " [-l] -- max storage tier\n"
            " [-B] -- write from this position\n"
            " [-f] -- configuration file name\n"
            " [-F] -- file type -- default 1 or 2 if stripe count not 0\n"
        ;
        return(-1);
    }

    if (KFS_STRIPED_FILE_TYPE_NONE == mStriperType &&
            (mStripeSize > 0 || mNumStripes > 0 || mNumRecoveryStripes > 0)) {
        mStriperType = KFS_STRIPED_FILE_TYPE_RS;
    }  // 判断是否使用RS编码

    string valErrorMsg;
    int valErrCode = KfsClient::ValidateCreateParams(
        mNumReplicas, mNumStripes, mNumRecoveryStripes, mStripeSize, mStriperType,
        mMinSTier, mMaxSTier, &valErrorMsg);
    if (valErrCode) {
        cout << valErrorMsg << "\n";
        return(-1);
    } //判断输入的参数是否存在不合理

    MsgLogger::Init(0, logLevel);
    mKfsClient = KfsClient::Connect(serverHost, port, config); //客户端连接MetaServer
    if (!mKfsClient) {
        cout << "qfs client failed to initialize\n";
        return(-1);
    }
    if (maxRetry > 0) {
        mKfsClient->SetMaxRetryPerOp(maxRetry);
    }
    if (retryDelay > 0) {
        mKfsClient->SetRetryDelay(retryDelay);
    }
    if (opTimeout > 0) {
        mKfsClient->SetDefaultIOTimeout(opTimeout);
    }
    if (mKfsBufSize >= 0) {   //控制Buffer的大小
        mKfsClient->SetDefaultIoBufferSize(mKfsBufSize);
    }

    struct stat statInfo;  // 判断文件路径异常？
    statInfo.st_mode = S_IFREG;
    if (sourcePath != "-" && stat(sourcePath.c_str(), &statInfo)) {
        ReportError("stat", sourcePath, -errno);
        return(-1);
    }

    mReadBuf = new char[mBufSize];

    if (!S_ISDIR(statInfo.st_mode)) {
        return BackupFile(sourcePath, kfsPath);
    }

    DIR* const dirp = opendir(sourcePath.c_str());
    if (! dirp) {
        ReportError("opendir", sourcePath, -errno);
        return(-1);
    }

    // when doing cp -r a/b kfs://c, we need to create c/b in KFS.
    const bool ok = MakeKfsLeafDir(sourcePath, kfsPath); //建立相应的文件目录
    closedir(dirp);
    return (ok ? BackupDir(sourcePath, kfsPath) : -1);
}

bool
CpToKfs::MakeKfsLeafDir(string sourcePath, string kfsPath)  // 在QFS中建立相应的路径
{
    string leaf;
    const string::size_type slash = sourcePath.rfind('/');

    // met everything after the last slash
    if (slash != string::npos) {
        leaf.assign(sourcePath, slash+1, string::npos);
    } else {
        leaf = sourcePath;
    }
    if (kfsPath.empty() || kfsPath[kfsPath.size()-1] != '/') {
        kfsPath += "/";
    }

    kfsPath += leaf;
    return Mkdirs(kfsPath);
}

int
CpToKfs::BackupFile(string sourcePath, string kfsPath)     //针对输入是文件的情况
{
    string filename;
    const string::size_type slash = sourcePath.rfind('/');

    // get everything after the last slash
    if (slash != string::npos) {
        filename.assign(sourcePath, slash+1, string::npos); //得到存储文件的指定名称
    } else {
        filename = sourcePath;
    }

    // for the dest side: if kfsPath is a dir, we are copying to
    // kfsPath with srcFilename; otherwise, kfsPath is a file (that
    // potentially exists) and we are ovewriting/creating it
    //判断所指定的QFS目录是目录还是文件名字？

    if (mKfsClient->IsDirectory(kfsPath.c_str())) {  //对于输入的KfsPath是目录的情况
        string dst = kfsPath;
        if (dst[kfsPath.size() - 1] != '/') {
            dst += "/";
        }
        return BackupFile2(sourcePath, dst + filename);
    }

    // kfsPath is the filename that is being specified for the cp
    // target.  try to copy to there...
    return BackupFile2(sourcePath, kfsPath);  //对于输入的KfsPath是文件名的情况直接overwrite相应的文件
}

int
CpToKfs::BackupDir(string dirname, string kfsdirname)  //针对输入是整个目录的情况
{
    string subdir, kfssubdir;
    DIR *dirp;
    struct dirent *fileInfo;

    if ((dirp = opendir(dirname.c_str())) == NULL) {
        ReportError("opendir", dirname, -errno);
        if (mIgnoreSrcErrorsFlag) {
            return 0;
        }
        return(-1);
    }
    if (!Mkdirs(kfsdirname)) {
        closedir(dirp);
        return (-1);
    }

    int ret = 0;
    while ((fileInfo = readdir(dirp)) != NULL) {
        if (strcmp(fileInfo->d_name, ".") == 0) {
            continue;
        }
        if (strcmp(fileInfo->d_name, "..") == 0) {
            continue;
        }
        string name = dirname + "/" + fileInfo->d_name;
        struct stat buf;
        if (stat(name.c_str(), &buf)) {
            ret = -errno;
            ReportError("stat", name, ret);
            break;
        }
        if (S_ISDIR(buf.st_mode)) {
            subdir = dirname + "/" + fileInfo->d_name;
            kfssubdir = kfsdirname + "/" + fileInfo->d_name;
            BackupDir(subdir, kfssubdir);
        } else if (S_ISREG(buf.st_mode)) {
            ret = BackupFile2(dirname + "/" + fileInfo->d_name, kfsdirname + "/" + fileInfo->d_name);
            if (ret) {
                break;
            }
        }
    }
    closedir(dirp);
    return ret;
}

//
// Guts of the work to copy the file.
//
int
CpToKfs::BackupFile2(string srcfilename, string kfsfilename)  //主要的文件拷贝函数
{
    const int srcFd = srcfilename == "-" ?
        dup(0) : open(srcfilename.c_str(), O_RDONLY);
    if (srcFd  < 0) {
        ReportError("open", srcfilename, -errno);
        if (mIgnoreSrcErrorsFlag) {
            return 0;
        }
        return(-1);
    }
    if (mDryRunFlag) {
        close(srcFd);
        return 0;
    }

    if (mDeleteFlag && mAppendMode) {  						//对文件进行删除的操作
        const int res = mKfsClient->Remove(kfsfilename.c_str());
        if (res < 0 && res != -ENOENT) {
            close(srcFd);
            ReportError("remove", kfsfilename, res);
            return(-1);
        }
    }
    const bool kForceTypeFlag = true;
    const int kfsfd = (mCreateExclusiveFlag || (mDeleteFlag && ! mAppendMode)) ?
        mKfsClient->Create(  			//根据输入的路径创建相应的文件
            kfsfilename.c_str(),
            mNumReplicas,
            mCreateExclusiveFlag,
            mNumStripes,
            mNumRecoveryStripes,
            mStripeSize,
            mStriperType,
            kForceTypeFlag,
            0666,
            mMinSTier,
            mMaxSTier
        )
        :
        mKfsClient->Open( 				  //根据输入的路径打开相应的文件
            kfsfilename.c_str(),
            (O_CREAT | O_WRONLY) |
                (mAppendMode ? O_APPEND : 0) |
                (mTruncateFlag ? O_TRUNC : 0),
            mNumReplicas,
            mNumStripes,
            mNumRecoveryStripes,
            mStripeSize,
            mStriperType,
            0666,
            mMinSTier,
            mMaxSTier
        );
    if (kfsfd < 0) {					//kfsfd为处理结果的返回值
        ReportError("open", kfsfilename, kfsfd);
        close(srcFd);
        return(-1);
    }
    KFS_LOG_STREAM_DEBUG <<
        kfsfilename << ": write behind: " << mKfsBufSize <<
        " => " << mKfsClient->GetIoBufferSize(kfsfd) <<
    KFS_LOG_EOM;

    if (0 < mStartPos) { 		//mStartPos是chunk offset
        const int64_t pos = mKfsClient->Seek(kfsfd, mStartPos);
        if (pos != mStartPos) {
            ReportError("seek", kfsfilename, (int)pos);
            close(srcFd);
            return(-1);
        }
    }

    ssize_t nRead; 		//读入的字节数目

    //////////////////////*将文件写入buffer的过程*/////////////////////////////
    while ((nRead = read(srcFd, mReadBuf, mBufSize)) > 0) {
        for (char* p = mReadBuf, * const e = p + nRead; p < e; ) {
            for (int i = 0; ;) {
                const int res = mKfsClient->Write(kfsfd, p, e - p);

                if (res <= 0 || (mAppendMode && p + res != e)) {
                    ReportError(mAppendMode ? "append" : "write",
                        kfsfilename, res);
                    close(srcFd);
                    mKfsClient->Close(kfsfd);
                    return(-1);
                }

                if (++i > mTestNumReWrites || mAppendMode) {
                    p += res;
                    break;
                }

                mKfsClient->Sync(kfsfd);
                const int nw = i <= 1 ? 0 : res / i;
                p += nw;
                mKfsClient->Seek(kfsfd, nw - res, SEEK_CUR);
            }
        }
    }
    if (nRead < 0) {  		//判断读入的字节数是否存在异常
        ReportError("read", srcfilename, -errno);
        if (mIgnoreSrcErrorsFlag) {
            nRead = 0;
        }
    }
    close(srcFd);  			// 关闭当前的源文件
    const int res = mKfsClient->Close(kfsfd);  //关闭在qfs端的文件
    if (res != 0) {
        ReportError("close", kfsfilename, res);
        return(-1);
    }
    return (nRead < 0 ? -1 : 0);
}

bool
CpToKfs::Mkdirs(string path)
{
    if (mDryRunFlag) {
        return true;
    }
    const int res = mKfsClient->Mkdirs(path.c_str());
    if (res < 0 && res != -EEXIST) {
        ReportError("mkdir", path, res);
        return false;
    }
    return true;
}

} // namespace KFS

int
main(int argc, char **argv)
{
    KFS::CpToKfs cpToKfs;
    return cpToKfs.Run(argc, argv);
}
