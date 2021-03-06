//
// C++ Implementation: srcFileParser
//
//  \copyright 2013 DCBlaha.  Distributed under the GPL.

#include "srcFileParser.h"
#include "Project.h"
#include "FilePath.h"
#include "OovProcess.h"
#include "ComponentFinder.h"
#include <stdio.h>
#include <algorithm>

bool srcFileParser::analyzeSrcFiles(OovStringRef const srcRootDir,
        OovStringRef const analysisDir)
    {
    mSrcRootDir = srcRootDir;
    mAnalysisDir = analysisDir;

#define MULTIPLE_THREADS 1
#if(MULTIPLE_THREADS)
    // This requires that the oovaide-incdeps file can be updated by multiple processes.
    setupQueue(getNumHardwareThreads());
#else
    setupQueue(1);
#endif
    mExcludeDirs = mComponentFinder.getProjectBuildArgs().getProjectExcludeDirs();
    OovStatus status = recurseDirs(srcRootDir);
    waitForCompletion();
    return status.ok();
    }

VerboseDumper sVerboseDump;

void VerboseDumper::open(OovStringRef const outPath)
    {
    mFp = stdout;
    /*
    std::string path = outPath;
    ensureLastPathSep(path);
    path += "OovBuilder.txt";
    DebugFile::open(path);
    */
    }

void VerboseDumper::logProcess(OovStringRef const srcFile,  char const * const *argv, int argc)
    {
    if(mFp)
        {
        fprintf(mFp, "\nOovBuilder: %s", srcFile.getStr());
        if(argc > 0)
            {
            for(int i=0; i<argc; i++)
                fprintf(mFp, " %s", argv[i]);
            fprintf(mFp, "\n");
            }
        }
    }

void VerboseDumper::logProgress(OovStringRef const progress)
    {
    if(mFp)
        {
        fprintf(mFp, "\nOovBuilder: %s", progress.getStr());
        }
    }

void VerboseDumper::logOutputOld(OovStringRef const fn)
    {
    if(mFp)
        {
        fprintf(mFp, "OovBuilder: Output older than %s\n", fn.getStr());
        }
    }

static void appendPathArgSep(OovString &str)
    {
#ifdef __linux__
    str += ":";
#else
    str += ";";
#endif
    }

static void getAnalysisToolCommand(FilePath const &filePath,
    ProjectBuildArgs const &projBuildInfo, CppChildArgs &args)
    {
    OovString command;
    if(isJavaSource(filePath))
        {
        args.addArg(projBuildInfo.getJavaCompilerPath());

        // Arguments for the parser must not be given to java, and instead
        // given later to the parser outside of this function.
        CompoundValue javaArgs;
        OovString javaArgsStr = projBuildInfo.getJavaArgs();     // This should be filtered for analyze mode
        javaArgs.parseString(javaArgsStr);
        if(javaArgsStr.length() > 0)
            {
            javaArgs.erase(remove_if(javaArgs.begin(), javaArgs.end(),
                [](OovString const &arg){ return(arg.find("-dups") != std::string::npos);}
                ));
            }
        ComponentFinder::appendArgs(true, javaArgs.getAsString(), args);

        // Add the classpath
        args.addArg("-cp");
        // Add the compiler jar to the classpath.
        OovString classPathArg;
        FilePath javaParserPath(Project::getBinDirectory(), FP_Dir);
        OovString javaAnalyzerTool = projBuildInfo.getJavaAnalyzerPath();
        OovString anaToolJar = javaAnalyzerTool;
        anaToolJar += ".jar";
        javaParserPath.appendFile(anaToolJar);
        classPathArg += javaParserPath;

        // Add tools.jar from the Java SDK to the classpath.
        appendPathArgSep(classPathArg);
        OovString toolsJar = projBuildInfo.getJavaJdkPath();
        if(toolsJar.back() == ';')
            {
            toolsJar.pop_back();
            }
        FilePath toolsJarFile(toolsJar, FP_Dir);
        toolsJarFile.appendDir("lib");
        toolsJarFile.appendFile("tools.jar");
        classPathArg += toolsJarFile;

        OovString classPathStr = projBuildInfo.getJavaClassPath();
        if(classPathStr.length() > 0)
            {
            appendPathArgSep(classPathArg);
            classPathArg += classPathStr;
            }
        FilePathQuoteCommandLinePath(classPathArg);
        args.addArg(classPathArg);
        args.addArg(javaAnalyzerTool);
        }
    else
        {
        OovString command;
        FilePath path(Project::getBinDirectory(), FP_Dir);
        path.appendFile("oovCppParser");
        command = FilePathMakeExeFilename(path);
        args.addArg(command);
        }
    }


bool srcFileParser::processFile(OovStringRef const srcFile)
    {
    bool success = true;
    if(!ComponentFinder::excludesMatch(srcFile, mExcludeDirs))
        {
        FilePath ext(srcFile, FP_File);
        bool cppSource = isCppHeader(ext) || isCppSource(ext);
        if(cppSource || isJavaSource(ext))
            {
            struct OovStat32 srcFileStat;
            success = (OovStatFunc(srcFile, &srcFileStat) == 0);
            if(success)
                {
                OovString srcRoot = mSrcRootDir;
                FilePathEnsureLastPathSep(srcRoot);
                OovString outFileName = Project::makeAnalysisFileName(srcFile,
                        srcRoot, mAnalysisDir);
                OovStatus status(true, SC_File);
                if(FileStat::isOutputOld(outFileName, srcFile, status))
                    {
                    OovString ownerComp = mComponentFinder.getComponentTypesFile().getComponentNameOwner(srcFile);
                    mComponentFinder.setCompConfig(ownerComp);
                    CppChildArgs ca;
                    getAnalysisToolCommand(ext, mComponentFinder.getProjectBuildArgs(), ca);
                    ca.addArg(srcFile);
                    ca.addArg(mSrcRootDir);
                    ca.addArg(mAnalysisDir);

                    if(cppSource)
                        {
                        OovStringVec incDirs = mComponentFinder.getFileIncludeDirs(srcFile);
                        ca.addCompileArgList(mComponentFinder, incDirs);
                        }
                    else
                        {
                        CompoundValue javaArgs;
                        javaArgs.parseString(mComponentFinder.getProjectBuildArgs().getJavaArgs());
                        if(javaArgs.find("-dups") != std::string::npos)
                            {
                            ca.addArg("-dups");
                            }
                        ComponentFinder::appendArgs(false, javaArgs.getAsString(), ca);
                        }
                    sVerboseDump.logProcess(srcFile, ca.getArgv(), static_cast<int>(ca.getArgc()));
                    addTask(ca);
    /*
                    sLog.logProcess(srcFile, ca.getArgv(), ca.getArgc());
                    printf("\noovBuilder Analyzing: %s\n", srcFile);
                    fflush(stdout);
                    OovProcessStdListener listener;
                    int exitCode;
                    OovPipeProcess pipeProc;
                    success = pipeProc.spawn(procPath, ca.getArgv(), listener, exitCode);
                    if(!success)
                        fprintf(stderr, "OovBuilder: Unable to execute process %s\n", procPath.getStr());
                    if(!success || exitCode != 0)
                        {
                        fprintf(stderr, "oovBuilder: Errors from %s\nArguments were: ", procPath.getStr());
                        ca.printArgs(stderr);
                        }
                    fflush(stdout);
                    fflush(stderr);
    */
                    }
                /// @todo - notify oovaide when files are ready to parse?
                }
            }
        }
    return success;
    }

bool srcFileParser::processItem(CppChildArgs const &item)
    {
    OovProcessBufferedStdListener listener(mListenerStdMutex);
    int exitCode;
    OovPipeProcess pipeProc;
    OovString processStr = "\noovBuilder Analyzing: ";
    /// @todo - this is a cheat. Should use something else to indicate
    /// which arg is the filename that is being analyzed.
    if(item.getArgv()[1][0] == '-')
        { processStr += item.getArgv()[4]; }
    else
        { processStr += item.getArgv()[1]; }
    processStr += "\n";
    printf("%s", processStr.getStr());
    fflush(stdout);
    listener.setProcessIdStr(processStr);
    bool success = pipeProc.spawn(item.getArgv()[0], item.getArgv(),
            listener, exitCode);
    if(!success || exitCode != 0)
        {
        OovString tempStr;
        if(!success)
            {
            tempStr = "OovBuilder: Unable to execute process ";
            }
        else
            {
            tempStr = "OovBuilder: Process returned error ";
            }
        tempStr += item.getArgv()[0];
        if(success)
            {
            tempStr += " ";
            tempStr.appendInt(exitCode);
            }
        if(!pipeProc.isArgLengthOk(static_cast<int>(tempStr.length() + item.getArgsAsStr().length())))
            {
            tempStr += "\nToo long of command arguments.";
            }
        tempStr += "\nArguments were: ";
        tempStr += item.getArgsAsStr();
        tempStr += "\n";
        listener.onStdErr(tempStr, tempStr.length());
        }
    return true;
    }
