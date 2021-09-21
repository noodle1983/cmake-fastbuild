/*============================================================================
    CMake - Cross Platform Makefile Generator
    Copyright 2000-2009 Kitware, Inc., Insight Software Consortium

    Distributed under the OSI-approved BSD License (the "License");
    see accompanying file Copyright.txt for details.

    This software is distributed WITHOUT ANY WARRANTY; without even the
    implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
    See the License for more information.
============================================================================*/

#include "cmGlobalFastbuildGenerator.h"

#include <algorithm>
#include <future>

#ifdef _WIN32
#  include <windows.h>

#  include <objbase.h>
#  include <shellapi.h>
#endif

#include "cmComputeLinkInformation.h"
#include "cmCustomCommandGenerator.h"
#include "cmDocumentationEntry.h"
#include "cmFastbuildNormalTargetGenerator.h"
#include "cmFastbuildUtilityTargetGenerator.h"
#include "cmGeneratedFileStream.h"
#include "cmGeneratorTarget.h"
#include "cmGlobalGeneratorFactory.h"
#include "cmLocalFastbuildGenerator.h"
#include "cmLocalGenerator.h"
#include "cmMakefile.h"
#include "cmSourceFile.h"
#include "cmState.h"
#include "cmSystemTools.h"
#include "cmTarget.h"
#include "cmVersion.h"
#include "cmake.h"

const char* cmGlobalFastbuildGenerator::FASTBUILD_BUILD_FILE = "fbuild.bff";

const char* cmGlobalFastbuildGenerator::INDENT = "  ";

cmGlobalFastbuildGenerator::cmGlobalFastbuildGenerator(cmake* cm)
  : cmGlobalCommonGenerator(cm)
  , BuildFileStream(nullptr)
{
#ifdef _WIN32
  cm->GetState()->SetWindowsShell(true);
#endif
  this->FindMakeProgramFile = "CMakeFastbuildFindMake.cmake";
  cm->GetState()->SetFastbuildMake(true);
}

std::unique_ptr<cmGlobalGeneratorFactory>
cmGlobalFastbuildGenerator::NewFactory()
{
  return std::unique_ptr<cmGlobalGeneratorFactory>(
    new cmGlobalGeneratorSimpleFactory<cmGlobalFastbuildGenerator>());
}

bool cmGlobalFastbuildGenerator::FindMakeProgram(cmMakefile* mf)
{
  if (!cmGlobalGenerator::FindMakeProgram(mf)) {
    return false;
  }
  if (auto const* const fastbuildCommand =
        mf->GetDefinition("CMAKE_MAKE_PROGRAM")) {
    this->FastbuildCommand = *fastbuildCommand;
    std::vector<std::string> command;
    command.push_back(this->FastbuildCommand);
    command.emplace_back("-version");
    std::string version;
    std::string error;
    if (!cmSystemTools::RunSingleCommand(command, &version, &error, nullptr,
                                         nullptr,
                                         cmSystemTools::OUTPUT_NONE)) {
      mf->IssueMessage(MessageType::FATAL_ERROR,
                       "Running\n '" + cmJoin(command, "' '") +
                         "'\n"
                         "failed with:\n " +
                         error);
      cmSystemTools::SetFatalErrorOccured();
      return false;
    }
    cmsys::RegularExpression versionRegex(R"(^FASTBuild v([0-9]+\.[0-9]+))");
    versionRegex.find(version);
    this->FastbuildVersion = versionRegex.match(1);
  }
  return true;
}

std::unique_ptr<cmLocalGenerator>
cmGlobalFastbuildGenerator::CreateLocalGenerator(cmMakefile* makefile)
{
  return std::unique_ptr<cmLocalGenerator>(
    cm::make_unique<cmLocalFastbuildGenerator>(this, makefile));
}

std::vector<cmGlobalGenerator::GeneratedMakeCommand>
cmGlobalFastbuildGenerator::GenerateBuildCommand(
  const std::string& makeProgram, const std::string& projectName,
  const std::string& projectDir, std::vector<std::string> const& targetNames,
  const std::string& config, bool fast, int jobs, bool verbose,
  std::vector<std::string> const& makeOptions)
{
  GeneratedMakeCommand makeCommand;
  makeCommand.Add(this->SelectMakeProgram(makeProgram));
  // A build command for fastbuild looks like this:
  // fbuild.exe [make-options] [-config projectName.bff] <target>

  // Hunt the fbuild.bff file in the directory above
  std::string configFile;
  if (!cmSystemTools::FileExists(projectDir + "fbuild.bff")) {
    configFile = cmSystemTools::FileExistsInParentDirectories(
      "fbuild.bff", projectDir.c_str(), "");
  }

  // Push in the make options
  makeCommand.Add(makeOptions.begin(), makeOptions.end());

  // makeCommand.Add("-showcmds");
  // makeCommand.Add("-quiet");
  makeCommand.Add("-monitor");
  makeCommand.Add("-ide");
  makeCommand.Add("-cache");
  makeCommand.Add("-wait");

  if (!configFile.empty()) {
    makeCommand.Add("-config", configFile);
  }

  // Add the target-config to the command
  for (const auto& tname : targetNames) {
    if (!tname.empty()) {
      if (tname == "clean") {
        makeCommand.Add("-clean");
      } else {
        makeCommand.Add(tname);
      }
    }
  }

  return { std::move(makeCommand) };
}

void cmGlobalFastbuildGenerator::ComputeTargetObjectDirectory(
  cmGeneratorTarget* gt) const
{
  // Compute full path to object file directory for this target.
  std::string dir;
  dir += gt->Makefile->GetCurrentBinaryDirectory();
  dir += "/";
  dir += gt->LocalGenerator->GetTargetDirectory(gt);
  dir += "/";
  gt->ObjectDirectory = dir;
}

void cmGlobalFastbuildGenerator::GetDocumentation(cmDocumentationEntry& entry)
{
  entry.Name = cmGlobalFastbuildGenerator::GetActualName();
  entry.Brief = "Generates build.bff files.";
}

void cmGlobalFastbuildGenerator::Generate()
{
  // Check minimum Fastbuild version.
  if (cmSystemTools::VersionCompare(cmSystemTools::OP_LESS,
                                    this->FastbuildVersion.c_str(),
                                    RequiredFastbuildVersion().c_str())) {
    std::ostringstream msg;
    msg << "The detected version of Fastbuild (" << this->FastbuildVersion;
    msg << ") is less than the version of Fastbuild required by CMake (";
    msg << this->RequiredFastbuildVersion() << ").";
    this->GetCMakeInstance()->IssueMessage(MessageType::FATAL_ERROR,
                                           msg.str());
    return;
  }

  this->OpenBuildFileStream();

  this->WriteBuildFileTop(*this->BuildFileStream);

  // Execute the standard generate process
  cmGlobalGenerator::Generate();

  // Write compilers
  this->WriteCompilers(*this->BuildFileStream);

  this->WriteTargets(*this->BuildFileStream);

  if (cmSystemTools::GetErrorOccuredFlag()) {
    this->BuildFileStream->setstate(std::ios::failbit);
  }

  this->CloseBuildFileStream();

#ifdef _WIN32
  std::vector<std::string> command;
  command.push_back(this->FastbuildCommand);
  command.emplace_back("VSSolution-all");
  std::string output;
  std::string error;
  std::string workingDir = LocalGenerators[0]->GetCurrentBinaryDirectory();
  if (!cmSystemTools::RunSingleCommand(command, &output, &error, nullptr,
                                       workingDir.c_str(),
                                       cmSystemTools::OUTPUT_NONE)) {
    LocalGenerators[0]->GetMakefile()->IssueMessage(
      MessageType::FATAL_ERROR,
      cmStrCat("Running\n '", cmJoin(command, "' '"),
               "'\n"
               "failed with:\n ",
               error, "\n", output));
    cmSystemTools::SetFatalErrorOccured();
  }
#endif
}

void cmGlobalFastbuildGenerator::WriteBuildFileTop(std::ostream& os)
{
  // Define some placeholder
  WriteDivider(os);
  os << "// Helper variables\n\n";

  WriteVariable(os, "FB_INPUT_1_PLACEHOLDER", Quote("\"%1\""));
  WriteVariable(os, "FB_INPUT_2_PLACEHOLDER", Quote("\"%2\""));
  WriteVariable(os, "FB_INPUT_3_PLACEHOLDER", Quote("\"%3\""));

  // Write settings
  auto root = LocalGenerators[0].get();

  std::string cacheDir =
    GetCMakeInstance()->GetHomeOutputDirectory() + "/fbuild.cache";
  if (root->GetMakefile()->IsDefinitionSet("CMAKE_FASTBUILD_CACHE_PATH"))
    cacheDir =
      root->GetMakefile()->GetSafeDefinition("CMAKE_FASTBUILD_CACHE_PATH");
  cmSystemTools::ConvertToOutputSlashes(cacheDir);

  WriteDivider(os);
  os << "// Settings\n\n";

  WriteCommand(os, "Settings");
  os << "{\n";
  WriteArray(*this->BuildFileStream, "Environment",
             Wrap(cmSystemTools::GetEnvironmentVariables()), 1);
  WriteVariable(*this->BuildFileStream, "CachePath", Quote(cacheDir), 1);
  os << "}\n";
}

void cmGlobalFastbuildGenerator::WriteDivider(std::ostream& os)
{
  os << "// ======================================"
        "=======================================\n";
}

void cmGlobalFastbuildGenerator::Indent(std::ostream& os, int count)
{
  for (int i = 0; i < count; ++i) {
    os << cmGlobalFastbuildGenerator::INDENT;
  }
}

void cmGlobalFastbuildGenerator::WriteComment(std::ostream& os,
                                              const std::string& comment,
                                              int indent)
{
  if (comment.empty()) {
    return;
  }

  std::string::size_type lpos = 0;
  std::string::size_type rpos;
  os << "\n";
  Indent(os, indent);
  os << "/////////////////////////////////////////////\n";
  while ((rpos = comment.find('\n', lpos)) != std::string::npos) {
    Indent(os, indent);
    os << "// " << comment.substr(lpos, rpos - lpos) << "\n";
    lpos = rpos + 1;
  }
  Indent(os, indent);
  os << "// " << comment.substr(lpos) << "\n\n";
}

void cmGlobalFastbuildGenerator::WriteVariable(std::ostream& os,
                                               const std::string& key,
                                               const std::string& value,
                                               int indent)
{
  WriteVariable(os, key, value, "=", indent);
}

void cmGlobalFastbuildGenerator::WriteVariable(std::ostream& os,
                                               const std::string& key,
                                               const std::string& value,
                                               const std::string& op,
                                               int indent)
{
  cmGlobalFastbuildGenerator::Indent(os, indent);
  os << "." << key << " " + op + " " << value << "\n";
}

void cmGlobalFastbuildGenerator::WriteCommand(std::ostream& os,
                                              const std::string& command,
                                              const std::string& value,
                                              int indent)
{
  cmGlobalFastbuildGenerator::Indent(os, indent);
  os << command;
  if (!value.empty()) {
    os << "(" << value << ")";
  }
  os << "\n";
}

void cmGlobalFastbuildGenerator::WriteArray(
  std::ostream& os, const std::string& key,
  const std::vector<std::string>& values, int indent)
{
  WriteArray(os, key, values, "=", indent);
}

void cmGlobalFastbuildGenerator::WriteArray(
  std::ostream& os, const std::string& key,
  const std::vector<std::string>& values, const std::string& op, int indent)
{
  WriteVariable(os, key, "", op, indent);
  Indent(os, indent);
  os << "{\n";
  size_t size = values.size();
  for (size_t index = 0; index < size; ++index) {
    const std::string& value = values[index];
    bool isLast = index == size - 1;

    Indent(os, indent + 1);
    os << value;
    if (!isLast) {
      os << ',';
    }
    os << "\n";
  }
  Indent(os, indent);
  os << "}\n";
}

std::string cmGlobalFastbuildGenerator::Quote(const std::string& str,
                                              const std::string& quotation)
{
  std::string result = str;
  cmSystemTools::ReplaceString(result, quotation, "^" + quotation);
  cmSystemTools::ReplaceString(result, FASTBUILD_DOLLAR_TAG, "$");
  return quotation + result + quotation;
}

struct WrapHelper
{
  std::string m_prefix;
  std::string m_suffix;
  bool escape_dollar;

  std::string operator()(const std::string& in)
  {
    std::string result = m_prefix + in + m_suffix;
    if (escape_dollar) {
      cmSystemTools::ReplaceString(result, "$", "^$");
      cmSystemTools::ReplaceString(result, FASTBUILD_DOLLAR_TAG, "$");
    }
    return result;
  }
};

std::vector<std::string> cmGlobalFastbuildGenerator::Wrap(
  const std::vector<std::string>& in, const std::string& prefix,
  const std::string& suffix, const bool escape_dollar)
{
  std::vector<std::string> result;

  WrapHelper helper = { prefix, suffix, escape_dollar };

  std::transform(in.begin(), in.end(), std::back_inserter(result), helper);

  return result;
}

std::vector<std::string> cmGlobalFastbuildGenerator::Wrap(
  const std::set<std::string>& in, const std::string& prefix,
  const std::string& suffix, const bool escape_dollar)
{
  std::vector<std::string> result;

  WrapHelper helper = { prefix, suffix, escape_dollar };

  std::transform(in.begin(), in.end(), std::back_inserter(result), helper);

  return result;
}

void cmGlobalFastbuildGenerator::WriteDisclaimer(std::ostream& os)
{
  os << "// CMAKE generated file: DO NOT EDIT!\n"
     << "// Generated by \"" << this->GetName() << "\""
     << " Generator, CMake Version " << cmVersion::GetMajorVersion() << "."
     << cmVersion::GetMinorVersion() << "\n\n";
}

void cmGlobalFastbuildGenerator::OpenBuildFileStream()
{
  // Compute Fastbuild's build file path.
  std::string buildFilePath =
    this->GetCMakeInstance()->GetHomeOutputDirectory();
  buildFilePath += "/";
  buildFilePath += cmGlobalFastbuildGenerator::FASTBUILD_BUILD_FILE;

  // Get a stream where to generate things.
  if (!this->BuildFileStream) {
    this->BuildFileStream = new cmGeneratedFileStream(
      buildFilePath.c_str(), false, this->GetMakefileEncoding());
    if (!this->BuildFileStream) {
      // An error message is generated by the constructor if it cannot
      // open the file.
      return;
    }
  }

  // Write the do not edit header.
  this->WriteDisclaimer(*this->BuildFileStream);

  // Write a comment about this file.
  *this->BuildFileStream
    << "// This file contains all the build statements\n\n";
}

void cmGlobalFastbuildGenerator::CloseBuildFileStream()
{
  if (this->BuildFileStream) {
    delete this->BuildFileStream;
    this->BuildFileStream = nullptr;
  } else {
    cmSystemTools::Error("Build file stream was not open.");
  }
}

void cmGlobalFastbuildGenerator::AddTarget(const FastbuildTarget& target)
{
  if (FastbuildTargets.find(target.Name) != FastbuildTargets.end()) {
    cmSystemTools::Error("Duplicated target " + target.Name);
  }
  FastbuildTargets[target.Name] = target;
}

void cmGlobalFastbuildGenerator::WriteCompilers(std::ostream& os)
{
  for (const auto& [key, compilerDef] : Compilers) {
    WriteDivider(os);
    os << "// Compilers\n\n";

    std::string fastbuildFamily = "custom";

    if (compilerDef.language == "C" || compilerDef.language == "CXX" ||
        compilerDef.language == "CUDA") {
      std::map<std::string, std::string> compilerIdToFastbuildFamily = {
        { "MSVC", "msvc" },        { "Clang", "clang" },
        { "AppleClang", "clang" }, { "GNU", "gcc" },
        { "NVIDIA", "cuda-nvcc" },
      };
      auto ft = compilerIdToFastbuildFamily.find(compilerDef.cmakeCompilerID);
      if (ft != compilerIdToFastbuildFamily.end())
        fastbuildFamily = ft->second;
    }

    // Strip out the path to the compiler
    std::string compilerPath = compilerDef.executable;
    // cmSystemTools::ConvertToOutputSlashes(compilerPath);

    // Write out the compiler that has been configured
    WriteCommand(os, "Compiler", Quote(compilerDef.name));
    os << "{\n";
    for (auto const& [key, value] : compilerDef.extraVariables) {
      WriteVariable(os, key, Quote(value), 1);
    }
    WriteVariable(os, "Executable", Quote(compilerPath), 1);
    WriteVariable(os, "CompilerFamily", Quote(fastbuildFamily), 1);
    if (compilerDef.useLightCache) {
      WriteVariable(os, "UseLightCache_Experimental", "true", 1);
    }
    if (fastbuildFamily == "clang")
      WriteVariable(os, "ClangRewriteIncludes", "false", 1);
    if (!compilerDef.extraFiles.empty())
      // Do not escape '$' sign, CMAKE_${LANG}_FASTBUILD_EXTRA_FILES might
      // contain FB variables to be expanded (we do use some internaly).
      // Besides a path cannot contain a '$'
      WriteArray(os, "ExtraFiles",
                 Wrap(compilerDef.extraFiles, "'", "'", false), 1);
    os << "}\n";

    auto compilerId = compilerDef.name;
    cmSystemTools::ReplaceString(compilerId, "-", "_");
    WriteVariable(os, compilerId, Quote(compilerDef.name));
  }
  // We need this because the Library command needs a compiler
  // even if don't compile anything
  if (!this->Compilers.empty())
    WriteVariable(os, "Compiler_dummy",
                  Quote(this->Compilers.begin()->second.name));
}

void cmGlobalFastbuildGenerator::AddCompiler(const std::string& language,
                                             cmMakefile* mf)
{
  if (this->Compilers.find(language) != this->Compilers.end())
    return;

  // Calculate the root location of the compiler
  std::string variableString = "CMAKE_" + language + "_COMPILER";
  std::string compilerLocation = mf->GetSafeDefinition(variableString);
  if (compilerLocation.empty())
    return;

  // Add the language to the compiler's name
  FastbuildCompiler compilerDef;
  compilerDef.extraVariables.push_back(
    { "Root", cmSystemTools::GetFilenamePath(compilerLocation) });
  compilerDef.name = "Compiler";
  compilerDef.executable =
    "$Root$/" + cmSystemTools::GetFilenameName(compilerLocation);
  compilerDef.cmakeCompilerID =
    mf->GetSafeDefinition("CMAKE_" + language + "_COMPILER_ID");

  compilerDef.cmakeCompilerVersion =
    mf->GetSafeDefinition("CMAKE_" + language + "_COMPILER_VERSION");
  compilerDef.name += "-";
  compilerDef.name += language;
  compilerDef.language = language;

  if (compilerDef.cmakeCompilerID == "MSVC" &&
      cmIsOn(mf->GetSafeDefinition("CMAKE_FASTBUILD_USE_LIGHTCACHE")) &&
      (language == "C" || language == "CXX")) {
    compilerDef.useLightCache = true;
  }
  compilerDef.extraFiles = cmExpandedList(
    mf->GetSafeDefinition("CMAKE_" + language + "_FASTBUILD_EXTRA_FILES"));

  // Automatically add extra files based on compiler (see
  // https://fastbuild.org/docs/functions/compiler.html)
  if (language == "C" || language == "CXX") {
    if (compilerDef.cmakeCompilerID == "MSVC") {
      // https://cmake.org/cmake/help/latest/variable/MSVC_VERSION.html

      // Visual Studio 17 (19.30 to 19.39)
      // TODO

      // Visual Studio 16 (19.20 to 19.29)
      if (cmSystemTools::VersionCompare(
            cmSystemTools::OP_GREATER_EQUAL,
            compilerDef.cmakeCompilerVersion.c_str(), "19.20")) {
        compilerDef.extraFiles.push_back("$Root$/c1.dll");
        compilerDef.extraFiles.push_back("$Root$/c1xx.dll");
        compilerDef.extraFiles.push_back("$Root$/c2.dll");
        compilerDef.extraFiles.push_back(
          "$Root$/atlprov.dll"); // Only needed if using ATL
        compilerDef.extraFiles.push_back("$Root$/msobj140.dll");
        compilerDef.extraFiles.push_back("$Root$/mspdb140.dll");
        compilerDef.extraFiles.push_back("$Root$/mspdbcore.dll");
        compilerDef.extraFiles.push_back("$Root$/mspdbsrv.exe");
        compilerDef.extraFiles.push_back("$Root$/mspft140.dll");
        compilerDef.extraFiles.push_back("$Root$/msvcp140.dll");
        compilerDef.extraFiles.push_back(
          "$Root$/msvcp140_atomic_wait.dll"); // Required circa 16.8.3
                                              // (14.28.29333)
        compilerDef.extraFiles.push_back(
          "$Root$/tbbmalloc.dll"); // Required as of 16.2 (14.22.27905)
        compilerDef.extraFiles.push_back("$Root$/vcruntime140.dll");
        compilerDef.extraFiles.push_back(
          "$Root$/vcruntime140_1.dll"); // Required as of 16.5.1 (14.25.28610)
        compilerDef.extraFiles.push_back("$Root$/1033/clui.dll");
        compilerDef.extraFiles.push_back(
          "$Root$/1033/mspft140ui.dll"); // Localized messages for static
                                         // analysis
      }
      // Visual Studio 15 (19.10 to 19.19)
      else if (cmSystemTools::VersionCompare(
                 cmSystemTools::OP_GREATER_EQUAL,
                 compilerDef.cmakeCompilerVersion.c_str(), "19.10")) {
        compilerDef.extraFiles.push_back("$Root$/c1.dll");
        compilerDef.extraFiles.push_back("$Root$/c1xx.dll");
        compilerDef.extraFiles.push_back("$Root$/c2.dll");
        compilerDef.extraFiles.push_back(
          "$Root$/atlprov.dll"); // Only needed if using ATL
        compilerDef.extraFiles.push_back("$Root$/msobj140.dll");
        compilerDef.extraFiles.push_back("$Root$/mspdb140.dll");
        compilerDef.extraFiles.push_back("$Root$/mspdbcore.dll");
        compilerDef.extraFiles.push_back("$Root$/mspdbsrv.exe");
        compilerDef.extraFiles.push_back("$Root$/mspft140.dll");
        compilerDef.extraFiles.push_back("$Root$/msvcp140.dll");
        compilerDef.extraFiles.push_back("$Root$/vcruntime140.dll");
        compilerDef.extraFiles.push_back("$Root$/1033/clui.dll");
      }
    }
    // TODO: Handle Intel compiler
  }

  this->Compilers[language] = compilerDef;
}

std::string cmGlobalFastbuildGenerator::AddLauncher(
  const std::string& launcher, const std::string& language, cmMakefile* mf)
{
  // Add the language to the compiler's name
  FastbuildCompiler compilerDef;
  compilerDef.name = "Launcher";
  compilerDef.executable = launcher;
  compilerDef.cmakeCompilerID =
    mf->GetSafeDefinition("CMAKE_" + language + "_COMPILER_ID");
  compilerDef.cmakeCompilerVersion =
    mf->GetSafeDefinition("CMAKE_" + language + "_COMPILER_VERSION");
  compilerDef.name += "-";
  compilerDef.name += language;
  auto hash =
    cmCryptoHash(cmCryptoHash::AlgoSHA256).HashString(launcher).substr(0, 7);
  compilerDef.name += "-" + hash;
  compilerDef.language = language;

  if (compilerDef.cmakeCompilerID == "MSVC" &&
      cmIsOn(mf->GetSafeDefinition("CMAKE_FASTBUILD_USE_LIGHTCACHE")) &&
      (language == "C" || language == "CXX")) {
    compilerDef.useLightCache = true;
  }

  this->Compilers[language + "-" + hash] = compilerDef;

  auto compilerId = compilerDef.name;
  cmSystemTools::ReplaceString(compilerId, "-", "_");

  return compilerId;
}

std::string cmGlobalFastbuildGenerator::ConvertToFastbuildPath(
  const std::string& path) const
{
  auto root = LocalGenerators[0].get();
  return root->MaybeConvertToRelativePath(
    ((cmLocalCommonGenerator*)root)->GetWorkingDirectory(), path);
}

std::set<std::string> cmGlobalFastbuildGenerator::WriteExecs(
  const std::vector<FastbuildExecNode>& Execs,
  const std::set<std::string>& dependencies)
{
  std::set<std::string> output;

  for (const auto& Exec : Execs) {
    output.insert(Exec.Name);

    auto execInput = Exec.ExecInput;
    for (auto const& dep : dependencies) {
      if (std::find(execInput.begin(), execInput.end(), dep) ==
          execInput.end()) {
        execInput.push_back(dep);
      }
    }

    if (Exec.IsNoop) {
      auto preBuildDependencies = Exec.PreBuildDependencies;
      preBuildDependencies.insert(dependencies.begin(), dependencies.end());
      // When this assert will trigger it means Noop is now using ExecInput
      // instead of PreBuildDependencies and we need to replace the variable
      // passed to WriteAlias with execInput.
      assert(!preBuildDependencies.empty() &&
             "Need to use ExecInput like non-Noop Exec");
      WriteAlias(Exec.Name, preBuildDependencies);
    } else {
      WriteCommand(*BuildFileStream, "Exec", Quote(Exec.Name), 1);
      Indent(*BuildFileStream, 1);
      *BuildFileStream << "{\n";
      {
        if (!Exec.PreBuildDependencies.empty()) {
          WriteArray(*BuildFileStream, "PreBuildDependencies",
                     Wrap(Exec.PreBuildDependencies), 2);
        }
        WriteVariable(*BuildFileStream, "ExecExecutable",
                      Quote(Exec.ExecExecutable), 2);
        if (!Exec.ExecArguments.empty()) {
          WriteVariable(*BuildFileStream, "ExecArguments",
                        Quote(Exec.ExecArguments), 2);
        }
        if (!Exec.ExecWorkingDir.empty()) {
          WriteVariable(*BuildFileStream, "ExecWorkingDir",
                        Quote(Exec.ExecWorkingDir), 2);
        }
        if (!execInput.empty()) {
          WriteArray(*BuildFileStream, "ExecInput", Wrap(execInput), 2);
        }
        if (Exec.ExecUseStdOutAsOutput) {
          WriteVariable(*BuildFileStream, "ExecUseStdOutAsOutput", "true", 2);
        }
        WriteVariable(*BuildFileStream, "ExecAlwaysShowOutput", "true", 2);
        WriteVariable(*BuildFileStream, "ExecOutput", Quote(Exec.ExecOutput),
                      2);
        if (Exec.ExecAlways) {
          WriteVariable(*BuildFileStream, "ExecAlways", "true", 2);
        }
      }
      Indent(*BuildFileStream, 1);
      *BuildFileStream << "}\n";
    }
  }

  // Forward dependencies to the next step
  if (output.empty())
    return dependencies;

  return output;
}

std::set<std::string> cmGlobalFastbuildGenerator::WriteObjectLists(
  const std::vector<FastbuildObjectListNode>& ObjectLists,
  const std::set<std::string>& dependencies)
{
  std::set<std::string> output;

  for (const auto& ObjectList : ObjectLists) {
    output.insert(ObjectList.Name);

    WriteCommand(*BuildFileStream, "ObjectList", Quote(ObjectList.Name), 1);
    Indent(*BuildFileStream, 1);
    *BuildFileStream << "{\n";
    {
      std::set<std::string> objectListDependencies = dependencies;
      for (const auto& dependency : ObjectList.PreBuildDependencies)
        objectListDependencies.insert(dependency);
      if (!objectListDependencies.empty())
        WriteArray(*BuildFileStream, "PreBuildDependencies",
                   Wrap(objectListDependencies), 2);
      WriteVariable(*BuildFileStream, "Compiler", ObjectList.Compiler, 2);
      WriteVariable(*BuildFileStream, "CompilerOptions",
                    Quote(ObjectList.CompilerOptions), 2);
      WriteVariable(*BuildFileStream, "CompilerOutputPath",
                    Quote(ObjectList.CompilerOutputPath), 2);
      WriteVariable(*BuildFileStream, "CompilerOutputExtension",
                    Quote(ObjectList.CompilerOutputExtension), 2);
      WriteVariable(*BuildFileStream, "CompilerOutputKeepBaseExtension",
                    "true", 2);
      WriteArray(*BuildFileStream, "CompilerInputFiles",
                 Wrap(ObjectList.CompilerInputFiles), 2);
      if (!ObjectList.PCHInputFile.empty()) {
        WriteVariable(*BuildFileStream, "PCHInputFile",
                      Quote(ObjectList.PCHInputFile), 2);
        WriteVariable(*BuildFileStream, "PCHOptions",
                      Quote(ObjectList.PCHOptions), 2);
      }
      if (!ObjectList.PCHOutputFile.empty()) {
        WriteVariable(*BuildFileStream, "PCHOutputFile",
                      Quote(ObjectList.PCHOutputFile), 2);
      }
    }
    Indent(*BuildFileStream, 1);
    *BuildFileStream << "}\n";
  }

  return output;
}

std::set<std::string> cmGlobalFastbuildGenerator::WriteLinker(
  const std::vector<FastbuildLinkerNode>& LinkerNodes,
  const std::set<std::string>& dependencies)
{
  std::set<std::string> output;

  for (const auto& LinkerNode : LinkerNodes) {
    output.insert(LinkerNode.Name);

    std::set<std::string> PreBuildDependencies = dependencies;
    for (const auto& library : LinkerNode.Libraries) {
      PreBuildDependencies.erase(library);
    }

    if (LinkerNode.Type == FastbuildLinkerNode::EXECUTABLE ||
        LinkerNode.Type == FastbuildLinkerNode::SHARED_LIBRARY) {
      auto alias = LinkerNode.Name == LinkerNode.LinkerOutput
        ? ""
        : Quote(LinkerNode.Name);
      WriteCommand(*BuildFileStream,
                   LinkerNode.Type == FastbuildLinkerNode::EXECUTABLE
                     ? "Executable"
                     : "DLL",
                   alias, 1);
      Indent(*BuildFileStream, 1);
      *BuildFileStream << "{\n";
      {
        if (!PreBuildDependencies.empty())
          WriteArray(*BuildFileStream, "PreBuildDependencies",
                     Wrap(PreBuildDependencies), 2);
        WriteVariable(*BuildFileStream, "Linker", Quote(LinkerNode.Linker), 2);
        WriteVariable(*BuildFileStream, "LinkerOptions",
                      Quote(LinkerNode.LinkerOptions), 2);
        WriteVariable(*BuildFileStream, "LinkerOutput",
                      Quote(LinkerNode.LinkerOutput), 2);
        WriteArray(*BuildFileStream, "Libraries", Wrap(LinkerNode.Libraries),
                   2);
        WriteVariable(*BuildFileStream, "LinkerLinkObjects", "false", 2);
        WriteVariable(*BuildFileStream, "LinkerType",
                      Quote(LinkerNode.LinkerType), 2);
      }
      Indent(*BuildFileStream, 1);
      *BuildFileStream << "}\n";
    } else {
      WriteCommand(*BuildFileStream, "Library", Quote(LinkerNode.Name), 1);
      Indent(*BuildFileStream, 1);
      *BuildFileStream << "{\n";
      {
        if (!PreBuildDependencies.empty())
          WriteArray(*BuildFileStream, "PreBuildDependencies",
                     Wrap(PreBuildDependencies), 2);
        WriteVariable(*BuildFileStream, "Librarian", Quote(LinkerNode.Linker),
                      2);
        WriteVariable(*BuildFileStream, "LibrarianOptions",
                      Quote(LinkerNode.LinkerOptions), 2);
        WriteArray(*BuildFileStream, "LibrarianAdditionalInputs",
                   Wrap(LinkerNode.Libraries), 2);
        WriteVariable(*BuildFileStream, "LibrarianOutput",
                      Quote(LinkerNode.LinkerOutput), 2);
        WriteVariable(*BuildFileStream, "LibrarianType",
                      Quote(LinkerNode.LinkerType), 2);
        WriteVariable(*BuildFileStream, "Compiler", LinkerNode.Compiler, 2);
        WriteVariable(*BuildFileStream, "CompilerOptions",
                      Quote(LinkerNode.CompilerOptions), 2);
        WriteVariable(*BuildFileStream, "CompilerOutputPath", "'/dummy/'", 2);
      }
      Indent(*BuildFileStream, 1);
      *BuildFileStream << "}\n";
    }
  }

  return output;
}

void cmGlobalFastbuildGenerator::WriteAlias(
  const std::string& alias, const std::vector<std::string>& targets)
{
  if (targets.empty())
    return;
  WriteCommand(*BuildFileStream, "Alias", Quote(alias), 1);
  Indent(*BuildFileStream, 1);
  *BuildFileStream << "{\n";
  WriteArray(*BuildFileStream, "Targets", Wrap(targets), 2);
  Indent(*BuildFileStream, 1);
  *BuildFileStream << "}\n";
}

void cmGlobalFastbuildGenerator::WriteAlias(
  const std::string& alias, const std::set<std::string>& targets)
{
  if (targets.empty())
    return;
  WriteCommand(*BuildFileStream, "Alias", Quote(alias), 1);
  Indent(*BuildFileStream, 1);
  *BuildFileStream << "{\n";
  WriteArray(*BuildFileStream, "Targets", Wrap(targets), 2);
  Indent(*BuildFileStream, 1);
  *BuildFileStream << "}\n";
}

void cmGlobalFastbuildGenerator::WriteTargets(std::ostream& os)
{
  // Add "all" and "noop" targets
  {
    FastbuildTarget allTarget;
    FastbuildAliasNode allNode;
    for (const auto& it : FastbuildTargets) {
      const auto& Target = it.second;
      if (!Target.IsGlobal && !Target.IsExcluded) {
        allNode.Targets.insert(Target.Name + "-products");
        allTarget.Dependencies.push_back(Target.Name);
      }
    }

    // "noop" target
    {
      FastbuildExecNode noopNode;
      noopNode.Name = "noop";
#ifdef _WIN32
      noopNode.ExecExecutable = cmSystemTools::FindProgram("cmd.exe");
      noopNode.ExecArguments = "/C cd .";
#else
      noopNode.ExecExecutable = cmSystemTools::FindProgram("bash");
      noopNode.ExecArguments = "-c :";
#endif
      noopNode.ExecUseStdOutAsOutput = true;
      noopNode.ExecOutput = "noop.txt";

      FastbuildTarget noop;
      noop.Name = noopNode.Name;
      noop.ExecNodes.push_back(noopNode);

      FastbuildTargets[noop.Name] = noop;

      if (allNode.Targets.empty()) {
        allNode.Targets.insert(noop.Name + "-products");
        allTarget.Dependencies.push_back(noop.Name);
      }
    }

    // "all" target
    {
      allTarget.Name = allNode.Name = "all";
      allTarget.AliasNodes.push_back(allNode);
      allTarget.IsGlobal = true;

#ifdef _WIN32
      auto& VCXProject = allTarget.VCXProjects.emplace_back();
      std::string targetName = "ALL_BUILD";

      std::string targetCompileOutDirectory =
        this->LocalGenerators[0]->GetCurrentBinaryDirectory();

      VCXProject.Name = "all-vcxproj";
      VCXProject.ProjectOutput = ConvertToFastbuildPath(
        targetCompileOutDirectory + "/" + targetName + ".vcxproj");
      VCXProject.Platform = "X64";
      VCXProject.Config =
        ((cmLocalCommonGenerator*)this->LocalGenerators[0].get())
          ->GetConfigNames()
          .front();
      VCXProject.Target = "all";
      VCXProject.Folder = "CMakePredefinedTargets";

      std::string cmakeCommand =
        this->LocalGenerators[0]->ConvertToOutputFormat(
          cmSystemTools::GetCMakeCommand(), cmLocalGenerator::SHELL);
      VCXProject.ProjectBuildCommand =
        cmStrCat(cmakeCommand, " --build ",
                 LocalGenerators[0]->GetCurrentBinaryDirectory(),
                 " --target \"all\" --config ", VCXProject.Config);
      VCXProject.ProjectRebuildCommand =
        cmStrCat(VCXProject.ProjectBuildCommand, " -- -clean");
#endif

      FastbuildTargets[allTarget.Name] = allTarget;
    }
  }

  // Add "rebuild-bff" target
  {
    std::vector<std::string> implicitDeps;
    for (auto& lg : LocalGenerators) {
      std::vector<std::string> const& lf = lg->GetMakefile()->GetListFiles();
      for (const auto& dep : lf) {
        implicitDeps.push_back(dep);
      }
    }

    auto root = LocalGenerators[0].get();

    std::string outDir =
      std::string(root->GetMakefile()->GetHomeOutputDirectory()) +
#ifdef _WIN32
      '\\'
#else
      '/'
#endif
      ;

    implicitDeps.push_back(outDir + "CMakeCache.txt");

    std::sort(implicitDeps.begin(), implicitDeps.end());
    implicitDeps.erase(std::unique(implicitDeps.begin(), implicitDeps.end()),
                       implicitDeps.end());

    FastbuildTarget rebuildBFFTarget;
    rebuildBFFTarget.Name = "rebuild-bff";

    FastbuildExecNode rebuildBFF;
    rebuildBFF.Name = "rebuild-bff";

    std::ostringstream args;
    args << root->ConvertToOutputFormat(cmSystemTools::GetCMakeCommand(),
                                        cmOutputConverter::SHELL)
         << " -H"
         << root->ConvertToOutputFormat(root->GetSourceDirectory(),
                                        cmOutputConverter::SHELL)
         << " -B"
         << root->ConvertToOutputFormat(root->GetBinaryDirectory(),
                                        cmOutputConverter::SHELL);
    rebuildBFF.ExecArguments = args.str();
    rebuildBFF.ExecInput = implicitDeps;
    rebuildBFF.ExecExecutable = cmSystemTools::GetCMakeCommand();
    rebuildBFF.ExecOutput =
      ConvertToFastbuildPath(outDir + FASTBUILD_BUILD_FILE);
    rebuildBFFTarget.ExecNodes.push_back(rebuildBFF);

    FastbuildTargets[rebuildBFF.Name] = rebuildBFFTarget;
  }

  std::map<std::string, std::string> objectOutputs;
  for (const auto& [Name, Target] : FastbuildTargets) {
    for (const auto& node : Target.ObjectListNodes) {
      for (const auto& output : node.ObjectOutputs) {
        objectOutputs[output] = Target.Name;
      }
    }
  }

  // Add Object dependecies as target dependencies
  for (auto& [_, Target] : FastbuildTargets) {
    for (auto& node : Target.ObjectListNodes) {
      for (auto dependency = node.ObjectDependencies.begin();
           dependency != node.ObjectDependencies.end();) {
        if (auto dt = objectOutputs.find(*dependency);
            dt != objectOutputs.end()) {
          Target.Dependencies.push_back(dt->second);
          dependency = node.ObjectDependencies.erase(dependency);
        } else {
          ++dependency;
        }
      }

      if (!node.ObjectDependencies.empty()) {
        for (const auto& inputFile : node.CompilerInputFiles) {
          FastbuildExecNode execNode;
          execNode.Name = "object-dependencies-";
          cmCryptoHash hash(cmCryptoHash::AlgoSHA256);
          execNode.Name += hash.HashString(inputFile + node.Name).substr(0, 7);
          execNode.ExecExecutable = cmSystemTools::GetCMakeCommand();
          execNode.ExecArguments = "-E touch " + inputFile;
          execNode.ExecInput = node.ObjectDependencies;
          execNode.ExecOutput = "dummy-" + execNode.Name + ".txt";
          execNode.ExecUseStdOutAsOutput = true;
          Target.ExecNodes.push_back(execNode);
        }
      }
    }
  }

  // Sort targets by dependencies, Fastbuild doesn't like it any other way
  std::vector<std::string> orderedTargets;
  {
    std::vector<std::string> targets;
    std::unordered_multimap<std::string, std::string> forwardDependencies,
      reverseDependencies;
    for (const auto& it : FastbuildTargets) {
      const auto& Target = it.second;

      targets.push_back(Target.Name);
      for (const auto& Dependency : Target.Dependencies) {
        forwardDependencies.emplace(Target.Name, Dependency);
        reverseDependencies.emplace(Dependency, Target.Name);
      }
    }

    while (!targets.empty()) {
      size_t initialSize = targets.size();
      for (auto it = targets.begin(); it != targets.end();) {
        auto targetName = *it;
        if (forwardDependencies.find(targetName) ==
            forwardDependencies.end()) {
          orderedTargets.emplace_back(targetName);
          it = targets.erase(it);

          auto range = reverseDependencies.equal_range(targetName);
          for (auto rt = range.first; rt != range.second; ++rt) {
            // Fetch the list of deps on that target
            auto frange = forwardDependencies.equal_range(rt->second);
            for (auto ft = frange.first; ft != frange.second;) {
              if (ft->second == targetName) {
                ft = forwardDependencies.erase(ft);
              } else {
                ++ft;
              }
            }
          }
        } else {
          ++it;
        }
      }
      if (initialSize == targets.size()) {
        for (const auto& target : targets) {
          orderedTargets.push_back(target);
        }
        targets.clear();
      }
    }
  }

  // Reuse precompiled headers whenever possible
  std::set<std::string> pch;
  for (const auto& targetName : orderedTargets) {
    auto& Target = FastbuildTargets[targetName];

    for (auto& objNode : Target.ObjectListNodes) {
      if (pch.count(objNode.PCHOutputFile)) {
        objNode.PCHInputFile.clear();
        objNode.PCHOptions.clear();
      } else {
        pch.insert(objNode.PCHOutputFile);
      }
    }
  }

  std::string VSConfig, VSPlatform;
  std::vector<std::string> SolutionBuildProjects;
  std::map<std::string, std::vector<std::string>> VSProjects, VSDependencies;
  std::set<std::string> allCustomCommands;
  for (const auto& targetName : orderedTargets) {
    auto& Target = FastbuildTargets[targetName];

    for (auto it = Target.ExecNodes.begin(); it != Target.ExecNodes.end();) {
      if (allCustomCommands.insert(it->Name).second) {
        ++it;
      } else {
        it = Target.ExecNodes.erase(it);
      }
    }

    this->WriteComment(*this->GetBuildFileStream(),
                       "Target definition: " + targetName);
    *this->GetBuildFileStream() << "{\n";

    for (const auto& [key, value] : Target.Variables) {
      WriteVariable(*this->BuildFileStream, key,
                    cmGlobalFastbuildGenerator::Quote(value), 1);
    }

    std::set<std::string> targetNodes;
    std::set<std::string> dependencies;
    dependencies = this->WriteExecs(Target.PreBuildExecNodes, dependencies);
    targetNodes.insert(dependencies.begin(), dependencies.end());
    dependencies = this->WriteExecs(Target.ExecNodes, dependencies);
    targetNodes.insert(dependencies.begin(), dependencies.end());
    auto objectLists =
      this->WriteObjectLists(Target.ObjectListNodes, dependencies);
    targetNodes.insert(objectLists.begin(), objectLists.end());
    dependencies =
      this->WriteExecs(Target.PreLinkExecNodes,
                       objectLists.empty() ? dependencies : objectLists);
    targetNodes.insert(dependencies.begin(), dependencies.end());
    // We want to depend on the products, this way we make sure we are waiting
    // for all generations
    for (const auto& dep : Target.Dependencies) {
      if (!FastbuildTargets.at(dep).IsGlobal)
        dependencies.insert(dep + "-products");
      else
        dependencies.insert(dep);
    }
    auto linked = this->WriteLinker(Target.LinkerNodes, dependencies);
    targetNodes.insert(linked.begin(), linked.end());
    auto products = this->WriteExecs(Target.PostBuildExecNodes,
                                     linked.empty() ? dependencies : linked);
    targetNodes.insert(products.begin(), products.end());

    for (const auto& it : Target.AliasNodes) {
      this->WriteAlias(it.Name, it.Targets);
      targetNodes.insert(it.Name);
    }

    if (!Target.IsGlobal) {
      if (targetNodes.find(Target.Name) == targetNodes.end()) {
        this->WriteAlias(Target.Name, products);
      }
      for (const auto& object : objectLists)
        products.erase(object);
      for (const auto& link : linked)
        products.erase(link);
      if (products.empty())
        products.insert(Target.Name);
      this->WriteAlias(Target.Name + "-products", products);
    } else if (Target.AliasNodes.empty()) {
      if (std::find(products.begin(), products.end(), Target.Name) ==
          products.end()) {
        this->WriteAlias(Target.Name, products);
      }
    }
#ifdef _WIN32
    for (const auto& VCXProject : Target.VCXProjects) {
      if (!Target.IsGlobal &&
          Target.LinkerNodes.front().Type == FastbuildLinkerNode::EXECUTABLE)
        SolutionBuildProjects.push_back(VCXProject.Name);

      WriteCommand(*BuildFileStream, "VCXProject", Quote(VCXProject.Name), 1);
      Indent(*BuildFileStream, 1);
      *BuildFileStream << "{\n";
      {
        WriteVariable(*BuildFileStream, "ProjectOutput",
                      Quote(VCXProject.ProjectOutput), 2);

        std::vector<std::string> ProjectFiles, ProjectFilesWithFolders;
        for (const auto& [folder, files] : VCXProject.ProjectFiles) {
          if (folder.empty()) {
            for (const auto& file : files)
              ProjectFiles.push_back(file);
          } else {
            std::string folderId = folder;
            cmSystemTools::ReplaceString(folderId, " ", "_");
            cmSystemTools::ReplaceString(folderId, "/", "_");
            cmSystemTools::ReplaceString(folderId, "\\", "_");
            cmSystemTools::ReplaceString(folderId, "..", "_");
            cmSystemTools::ReplaceString(folderId, ".", "_");

            std::stringstream ss;
            WriteVariable(ss, "Folder", Quote(folder), 3);
            WriteArray(ss, "Files", Wrap(files), 3);
            Indent(ss, 2);
            ss << "]";
            WriteVariable(*BuildFileStream, folderId, "[\n" + ss.str(), 2);

            ProjectFilesWithFolders.push_back("." + folderId);
          }
        }
        if (!ProjectFiles.empty())
          WriteArray(*BuildFileStream, "ProjectFiles", Wrap(ProjectFiles), 2);
        if (!ProjectFilesWithFolders.empty())
          WriteArray(*BuildFileStream, "ProjectFilesWithFolders",
                     ProjectFilesWithFolders, 2);

        if (!VCXProject.UserProps.empty()) {
          std::stringstream ss;
          WriteVariable(ss, "Condition",
                        Quote("Exists('" + VCXProject.UserProps + "')"), 3);
          WriteVariable(ss, "Project", Quote(VCXProject.UserProps), 3);
          Indent(ss, 2);
          ss << "]";
          WriteVariable(*BuildFileStream, "UserProps", "[\n" + ss.str(), 2);
          WriteArray(*BuildFileStream, "ProjectProjectImports",
                     { ".UserProps" }, 2);
        }
        if (!VCXProject.LocalDebuggerCommand.empty()) {
          WriteVariable(*BuildFileStream, "LocalDebuggerCommand",
                        Quote(VCXProject.LocalDebuggerCommand), 2);
        }
        if (!VCXProject.LocalDebuggerCommandArguments.empty()) {
          WriteVariable(*BuildFileStream, "LocalDebuggerCommandArguments",
                        Quote(VCXProject.LocalDebuggerCommandArguments), 2);
        }
        std::stringstream ss;
        WriteVariable(ss, "Platform", Quote(VCXProject.Platform), 3);
        WriteVariable(ss, "Config", Quote(VCXProject.Config), 3);
        WriteVariable(ss, "Target", Quote(VCXProject.Target), 3);
        WriteVariable(ss, "ProjectBuildCommand",
                      Quote(VCXProject.ProjectBuildCommand), 3);
        WriteVariable(ss, "ProjectRebuildCommand",
                      Quote(VCXProject.ProjectRebuildCommand), 3);
        Indent(ss, 2);
        ss << "]";
        WriteVariable(*BuildFileStream, "ProjectConfigs", "[\n" + ss.str(), 2);
      }
      Indent(*BuildFileStream, 1);
      *BuildFileStream << "}\n";

      VSProjects[VCXProject.Folder].push_back(VCXProject.Name);
      for (const auto& dep : Target.Dependencies) {
        for (const auto& depVCXProject :
             FastbuildTargets.at(dep).VCXProjects) {
          VSDependencies[VCXProject.Name].push_back(depVCXProject.Name);
        }
      }
      VSConfig = VCXProject.Config;
      VSPlatform = VCXProject.Platform;
    }
#endif

    *this->GetBuildFileStream() << "}\n";
  }

  // Write the VSSolution node on Windows
#ifdef _WIN32
  WriteCommand(*BuildFileStream, "VSSolution", Quote("VSSolution-all"));
  *BuildFileStream << "{\n";
  {
    WriteVariable(
      *BuildFileStream, "SolutionOutput",
      Quote(cmStrCat(LocalGenerators[0]->GetCurrentBinaryDirectory(), "/",
                     LocalGenerators[0]->GetProjectName(), ".sln")),
      1);

    std::vector<std::string> SolutionProjects;
    for (const auto& [_, projects] : VSProjects) {
      for (const auto& project : projects)
        SolutionProjects.push_back(project);
    }
    WriteArray(*BuildFileStream, "SolutionProjects", Wrap(SolutionProjects),
               1);
    std::stringstream ss;
    WriteVariable(ss, "Platform", Quote(VSPlatform), 2);
    WriteVariable(ss, "Config", Quote(VSConfig), 2);
    Indent(ss, 1);
    ss << "]";
    WriteVariable(*BuildFileStream, "SolutionConfig", "[\n" + ss.str(), 1);
    WriteArray(*BuildFileStream, "SolutionConfigs", { ".SolutionConfig" }, 1);
    std::vector<std::string> SolutionFolders;
    for (const auto& [folder, projects] : VSProjects) {
      if (folder.empty())
        continue;

      std::string folderId = folder;
      cmSystemTools::ReplaceString(folderId, " ", "_");
      cmSystemTools::ReplaceString(folderId, "/", "_");
      cmSystemTools::ReplaceString(folderId, "\\", "_");

      std::stringstream ss;
      WriteVariable(ss, "Path", Quote(folder), 2);
      WriteArray(ss, "Projects", Wrap(projects), 2);
      Indent(ss, 1);
      ss << "]";
      WriteVariable(*BuildFileStream, folderId, "[\n" + ss.str(), 1);

      SolutionFolders.push_back("." + folderId);
    }
    if (!SolutionFolders.empty())
      WriteArray(*BuildFileStream, "SolutionFolders", SolutionFolders, 1);

    std::vector<std::string> SolutionDependencies;
    for (const auto& [project, dependencies] : VSDependencies) {
      std::string depsId = project + "_deps";

      cmSystemTools::ReplaceString(depsId, "-", "_");

      std::stringstream ss;
      WriteArray(ss, "Projects", Wrap(std::vector<std::string>{ project }), 2);
      WriteArray(ss, "Dependencies", Wrap(dependencies), 2);
      Indent(ss, 1);
      ss << "]";
      WriteVariable(*BuildFileStream, depsId, "[\n" + ss.str(), 1);

      SolutionDependencies.push_back("." + depsId);
    }
    if (!SolutionDependencies.empty())
      WriteArray(*BuildFileStream, "SolutionDependencies",
                 SolutionDependencies, 1);

    WriteArray(*BuildFileStream, "SolutionBuildProject",
               Wrap(SolutionBuildProjects), 1);
  }
#endif
  *BuildFileStream << "}\n";
}

std::string cmGlobalFastbuildGenerator::GetTargetName(
  const cmGeneratorTarget* GeneratorTarget) const
{
  std::string targetName =
    GeneratorTarget->GetLocalGenerator()->GetCurrentBinaryDirectory();
  targetName += "/";
  targetName += GeneratorTarget->GetName();
  targetName = this->ConvertToFastbuildPath(targetName);
  return targetName;
}

bool cmGlobalFastbuildGenerator::IsExcluded(cmGeneratorTarget* target)
{
  return cmGlobalGenerator::IsExcluded(LocalGenerators[0].get(), target);
}

bool cmGlobalFastbuildGenerator::Open(const std::string& bindir,
                                      const std::string& projectName,
                                      bool dryRun)
{
#ifdef _WIN32
  std::string sln = bindir + "/" + projectName + ".sln";

  if (dryRun) {
    return cmSystemTools::FileExists(sln, true);
  }

  sln = cmSystemTools::ConvertToOutputPath(sln);

  auto OpenSolution = [](std::string sln) {
    HRESULT comInitialized =
      CoInitializeEx(NULL, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    if (FAILED(comInitialized)) {
      return false;
    }

    HINSTANCE hi =
      ShellExecuteA(NULL, "open", sln.c_str(), NULL, NULL, SW_SHOWNORMAL);

    CoUninitialize();

    return reinterpret_cast<intptr_t>(hi) > 32;
  };

  return std::async(std::launch::async, OpenSolution, sln).get();
#else
  return cmGlobalCommonGenerator::Open(bindir, projectName, dryRun);
#endif
}
