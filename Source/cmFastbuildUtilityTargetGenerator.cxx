
#include "cmFastbuildUtilityTargetGenerator.h"

#include "cmCustomCommandGenerator.h"
#include "cmGeneratedFileStream.h"
#include "cmGeneratorTarget.h"
#include "cmLocalFastbuildGenerator.h"
#include "cmMakefile.h"
#include "cmSourceFile.h"
#include "cmake.h"

cmFastbuildUtilityTargetGenerator::cmFastbuildUtilityTargetGenerator(
  cmGeneratorTarget* gt)
  : cmFastbuildTargetGenerator(gt)
{
}

void cmFastbuildUtilityTargetGenerator::Generate()
{
  auto targetName = GeneratorTarget->GetName();

  if (GeneratorTarget->GetType() == cmStateEnums::GLOBAL_TARGET) {
    targetName = GetGlobalGenerator()->GetTargetName(GeneratorTarget);
  }
  const std::string& configName = this->GetConfigName();

  cmGlobalFastbuildGenerator::FastbuildTarget fastbuildTarget;
  fastbuildTarget.Name = targetName;

  // Get all dependencies
  cmTargetDependSet const& targetDeps =
    GlobalCommonGenerator->GetTargetDirectDepends(GeneratorTarget);
  for (const cmTargetDepend& depTarget : targetDeps) {
    if (depTarget->GetType() == cmStateEnums::INTERFACE_LIBRARY) {
      continue;
    } else if (depTarget->GetType() == cmStateEnums::GLOBAL_TARGET) {
      const cmGeneratorTarget& t = *depTarget;
      fastbuildTarget.Dependencies.push_back(
        GetGlobalGenerator()->GetTargetName(&t));
    } else {
      fastbuildTarget.Dependencies.push_back(depTarget->GetName());
    }
  }

  if (GeneratorTarget->GetType() == cmStateEnums::GLOBAL_TARGET) {
    for (const auto& util : GeneratorTarget->GetUtilities()) {
      std::string d =
        GeneratorTarget->GetLocalGenerator()->GetCurrentBinaryDirectory() +
        "/" + util.Value.first;
      fastbuildTarget.Dependencies.push_back(this->ConvertToFastbuildPath(d));
    }
  }

  fastbuildTarget.PreBuildExecNodes = GenerateCommands("PreBuild");
  fastbuildTarget.PreLinkExecNodes = GenerateCommands("PreLink");
  fastbuildTarget.PostBuildExecNodes = GenerateCommands("PostBuild");
  fastbuildTarget.ExecNodes = GenerateCommands();
  fastbuildTarget.IsGlobal =
    GeneratorTarget->GetType() == cmStateEnums::GLOBAL_TARGET;
  fastbuildTarget.IsExcluded =
    GetGlobalGenerator()->IsExcluded(GeneratorTarget);

  GetLocalGenerator()->AddTarget(fastbuildTarget);
}
