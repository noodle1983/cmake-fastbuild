#ifndef cmFastbuildNormalTargetGenerator_h
#define cmFastbuildNormalTargetGenerator_h

#include "cmConfigure.h" // IWYU pragma: keep

#include "cmFastbuildTargetGenerator.h"
#include "cmGlobalFastbuildGenerator.h"

class cmCustomCommandGenerator;
class cmGeneratedFileStream;
class cmOSXBundleGenerator;

class cmFastbuildNormalTargetGenerator : public cmFastbuildTargetGenerator
{
public:
  cmFastbuildNormalTargetGenerator(cmGeneratorTarget* gt);
  ~cmFastbuildNormalTargetGenerator();

  void Generate() override;

  std::vector<std::string> GetLanguages() override;

private:
  std::vector<cmGlobalFastbuildGenerator::FastbuildObjectListNode>
  GenerateObjects();
  std::vector<cmGlobalFastbuildGenerator::FastbuildLinkerNode> GenerateLink(
    const std::vector<cmGlobalFastbuildGenerator::FastbuildObjectListNode>&
      objectLists);

  struct FastbuildTargetNames
  {
    std::string targetNameOut;
    std::string targetNameReal;
    std::string targetNameImport;
    std::string targetNamePDB;
    std::string targetNameSO;

    std::string targetOutput;
    std::string targetOutputReal;
    std::string targetOutputImplib;
    std::string targetOutputDir;
    std::string targetOutputPDBDir;
    std::string targetOutputCompilePDBDir;
  };

  void DetectCompilerFlags(std::string& compileFlags,
                           const cmSourceFile* source,
                           const std::string& language);

  void DetectOutput(FastbuildTargetNames& targetNamesOut,
                    const std::string& configName);

  void DetectLinkerLibPaths(std::string& linkerLibPath,
                            const std::string& configName);
  bool DetectBaseLinkerCommand(std::string& command,
                               const std::string& configName);

  void ComputeLinkCmds(std::vector<std::string>& linkCmds,
                       std::string configName);

  std::string ComputeDefines(const cmSourceFile* source,
                             const std::string& configName,
                             const std::string& language);

  void DetectTargetObjectDependencies(const std::string& configName,
                                      std::vector<std::string>& dependencies);

  std::string GetManifestsAsFastbuildPath();

  static std::string BuildCommandLine(
    const std::vector<std::string>& cmdLines);

  static void SplitExecutableAndFlags(const std::string& command,
                                      std::string& executable,
                                      std::string& options);

  static void EnsureDirectoryExists(const std::string& path,
                                    const char* homeOutputDirectory);
};

#endif // cmFastbuildNormalTargetGenerator_h
