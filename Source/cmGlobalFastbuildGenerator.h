/*============================================================================
  CMake - Cross Platform Makefile Generator
  Copyright 2000-2009 Kitware, Inc., Insight Software Consortium

  Distributed under the OSI-approved BSD License (the "License");
  see accompanying file Copyright.txt for details.

  This software is distributed WITHOUT ANY WARRANTY; without even the
  implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
  See the License for more information.
============================================================================*/
#ifndef cmGlobalFastbuildGenerator_h
#define cmGlobalFastbuildGenerator_h

#include "cmConfigure.h" // IWYU pragma: keep

#include "cmGlobalCommonGenerator.h"
#include "cmStateTypes.h"

class cmGlobalGeneratorFactory;
class cmFastbuildTargetGenerator;
class cmGeneratedFileStream;
class cmDocumentationEntry;

/** \class cmGlobalFastbuildGenerator
 * \brief Class for global fastbuild generator.
 */
class cmGlobalFastbuildGenerator : public cmGlobalCommonGenerator
{
public:
  /// The default name of Fastbuild's build file. Typically: fbuild.bff.
  static const char* FASTBUILD_BUILD_FILE;

  /// The indentation string used when generating Fastbuild's build file.
  static const char* INDENT;

  cmGlobalFastbuildGenerator(cmake* cm);

  static std::unique_ptr<cmGlobalGeneratorFactory> NewFactory();

  bool FindMakeProgram(cmMakefile* mf) override;

  void Generate() override;

  std::vector<GeneratedMakeCommand> GenerateBuildCommand(
    const std::string& makeProgram, const std::string& projectName,
    const std::string& projectDir, std::vector<std::string> const& targetNames,
    const std::string& config, bool fast, int jobs, bool verbose,
    std::vector<std::string> const& makeOptions =
      std::vector<std::string>()) override;

  ///! create the correct local generator
  std::unique_ptr<cmLocalGenerator> CreateLocalGenerator(
    cmMakefile* makefile) override;
  std::string GetName() const override
  {
    return cmGlobalFastbuildGenerator::GetActualName();
  }

  bool IsMultiConfig() const override { return false; }

  void ComputeTargetObjectDirectory(cmGeneratorTarget*) const override;

  static std::string GetActualName() { return "Fastbuild"; }

  const char* GetAllTargetName() const override { return "all"; }
  const char* GetInstallTargetName() const override { return "install"; }
  const char* GetCleanTargetName() const override { return "clean"; }
  const char* GetInstallLocalTargetName() const override
  {
    return "install/local";
  }
  const char* GetInstallStripTargetName() const override
  {
    return "install/strip";
  }
  const char* GetTestTargetName() const override { return "test"; }
  const char* GetPackageSourceTargetName() const override
  {
    return "package_source";
  }

  /// Overloaded methods. @see cmGlobalGenerator::GetDocumentation()
  static void GetDocumentation(cmDocumentationEntry& entry);

  static bool SupportsToolset() { return false; }

  /**
   * Utilized by the generator factory to determine if this generator
   * supports platforms.
   */
  static bool SupportsPlatform() { return false; }

  bool IsIPOSupported() const override { return true; }

  static std::string RequiredFastbuildVersion() { return "1.00"; }

  void OpenBuildFileStream();
  void CloseBuildFileStream();

  bool Open(const std::string& bindir, const std::string& projectName,
            bool dryRun) override;

  cmGeneratedFileStream* GetBuildFileStream() const
  {
    return this->BuildFileStream;
  }

  std::string ConvertToFastbuildPath(const std::string& path) const;

  template <typename T>
  std::vector<std::string> ConvertToFastbuildPath(const T& container) const
  {
    std::vector<std::string> ret;
    for (const auto& path : container)
      ret.push_back(ConvertToFastbuildPath(path));
    return ret;
  }

  void WriteBuildFileTop(std::ostream& os);

  static std::string Quote(const std::string& str,
                           const std::string& quotation = "'");

  static std::vector<std::string> Wrap(const std::vector<std::string>& in,
                                       const std::string& prefix = "'",
                                       const std::string& suffix = "'");

  static std::vector<std::string> Wrap(const std::set<std::string>& in,
                                       const std::string& prefix = "'",
                                       const std::string& suffix = "'");

  static void WriteDivider(std::ostream& os);
  static void WriteComment(std::ostream& os, const std::string& comment,
                           int indent = 0);

  /// Write @a count times INDENT level to output stream @a os.
  static void Indent(std::ostream& os, int count);

  static void WriteVariable(std::ostream& os, const std::string& key,
                            const std::string& value, const std::string& op,
                            int indent = 0);
  static void WriteVariable(std::ostream& os, const std::string& key,
                            const std::string& value, int indent = 0);
  static void WriteCommand(std::ostream& os, const std::string& command,
                           const std::string& value = std::string(),
                           int indent = 0);
  static void WriteArray(std::ostream& os, const std::string& key,
                         const std::vector<std::string>& values,
                         int indent = 0);
  static void WriteArray(std::ostream& os, const std::string& key,
                         const std::vector<std::string>& values,
                         const std::string& op, int indent = 0);

  template <typename T>
  static void SortByDependencies(
    std::vector<T>& source, const std::unordered_multimap<T, T>& dependencies)
  {
    std::vector<T> output;

    std::unordered_multimap<T, T> reverseDependencies;
    for (auto it = dependencies.begin(); it != dependencies.end(); ++it) {
      auto range = dependencies.equal_range(it->first);
      for (auto jt = range.first; jt != range.second; ++jt) {
        reverseDependencies.emplace(jt->second, jt->first);
      }
    }

    auto forwardDependencies = dependencies;
    while (!source.empty()) {
      for (auto it = source.begin(); it != source.end();) {
        auto targetName = *it;
        if (forwardDependencies.find(targetName) ==
            forwardDependencies.end()) {
          output.push_back(targetName);
          it = source.erase(it);

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
    }

    std::swap(output, source);
  }

  /// Write the common disclaimer text at the top of each build file.
  void WriteDisclaimer(std::ostream& os);

  void WriteCompilers(std::ostream& os);

  void WriteTargets(std::ostream& os);

  void AddCompiler(const std::string& lang, cmMakefile* mf);

  std::string AddLauncher(const std::string& launcher, const std::string& lang,
                          cmMakefile* mf);

  struct FastbuildCompiler
  {
    std::string name;
    std::string path;
    std::string cmakeCompilerID;
    std::string cmakeCompilerVersion;
    std::string language;
    std::vector<std::string> extraFiles;
    bool useLightCache = false;
  };

  struct FastbuildObjectListNode
  {
    std::string Name;
    std::string Compiler;
    std::string CompilerOptions;
    std::string CompilerOutputPath;
    std::string CompilerOutputExtension;
    std::string PCHInputFile;
    std::string PCHOutputFile;
    std::string PCHOptions;

    std::vector<std::string> CompilerInputFiles;
    std::set<std::string> PreBuildDependencies;

    std::vector<std::string> ObjectDependencies;
    std::vector<std::string> ObjectOutputs;
  };

  struct FastbuildVCXProject
  {
    std::string Name;
    std::string Folder;
    std::string UserProps;
    std::string LocalDebuggerCommand;
    std::string LocalDebuggerCommandArguments;
    std::string ProjectOutput;
    std::map<std::string, std::vector<std::string>> ProjectFiles;
    std::string Platform;
    std::string Config;
    std::string Target;
    std::string ProjectBuildCommand;
    std::string ProjectRebuildCommand;
  };

  struct FastbuildLinkerNode
  {
    enum
    {
      EXECUTABLE,
      SHARED_LIBRARY,
      STATIC_LIBRARY
    } Type;

    std::string Name;
    std::string Compiler;
    std::string CompilerOptions;
    std::string Linker;
    std::string LinkerType;
    std::string LinkerOutput;
    std::string LinkerOptions;
    std::vector<std::string> Libraries;
  };

  struct FastbuildExecNode
  {
    std::string Name;
    std::string ExecExecutable;
    std::string ExecArguments;
    std::string ExecWorkingDir;
    bool ExecUseStdOutAsOutput = false;
    std::string ExecOutput;
    std::vector<std::string> ExecInput;
    std::set<std::string> PreBuildDependencies;
    bool ExecAlways = false;
    bool IsNoop = false;
  };

  struct FastbuildAliasNode
  {
    std::string Name;
    std::set<std::string> Targets;
  };

  struct FastbuildTarget
  {
    std::string Name;
    std::map<std::string, std::string> Variables;
    std::vector<FastbuildObjectListNode> ObjectListNodes;
    std::vector<FastbuildLinkerNode> LinkerNodes;
    std::vector<FastbuildVCXProject> VCXProjects;
    std::vector<FastbuildExecNode> PreBuildExecNodes, PreLinkExecNodes,
      PostBuildExecNodes, ExecNodes;
    std::vector<FastbuildAliasNode> AliasNodes;
    std::vector<std::string> Dependencies;
    bool IsGlobal = false;
    bool IsExcluded = false;
  };

  std::string GetTargetName(const cmGeneratorTarget* GeneratorTarget) const;

  void AddTarget(const FastbuildTarget& target);

  bool IsExcluded(cmGeneratorTarget* target);

  std::set<std::string> WriteExecs(const std::vector<FastbuildExecNode>&,
                                      const std::set<std::string>&);
  std::set<std::string> WriteObjectLists(
    const std::vector<FastbuildObjectListNode>&,
    const std::set<std::string>&);
  std::set<std::string> WriteLinker(const std::vector<FastbuildLinkerNode>&,
                                       const std::set<std::string>&);
  void WriteAlias(const std::string&, const std::set<std::string>&);
  void WriteAlias(const std::string&, const std::vector<std::string>&);

  /// The set of compilers added to the generated build system.
  std::map<std::string, FastbuildCompiler> Compilers;
  std::map<std::string, FastbuildTarget> FastbuildTargets;

  /// The file containing the build statement.
  cmGeneratedFileStream* BuildFileStream;

  std::string FastbuildCommand;
  std::string FastbuildVersion;

  std::map<std::string, std::unique_ptr<cmFastbuildTargetGenerator>> Targets;
  std::unordered_multimap<std::string, std::string> TargetDependencies;
};

#endif
