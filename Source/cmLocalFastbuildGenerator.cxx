/*============================================================================
  CMake - Cross Platform Makefile Generator
  Copyright 2000-2009 Kitware, Inc., Insight Software Consortium

  Distributed under the OSI-approved BSD License (the "License");
  see accompanying file Copyright.txt for details.

  This software is distributed WITHOUT ANY WARRANTY; without even the
  implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
  See the License for more information.
============================================================================*/
#include "cmLocalFastbuildGenerator.h"

#include "cmCustomCommandGenerator.h"
#include "cmFastbuildTargetGenerator.h"
#include "cmGeneratorTarget.h"
#include "cmGlobalFastbuildGenerator.h"
#include "cmMakefile.h"
#include "cmSourceFile.h"
#include "cmStateDirectory.h"
#include "cmSystemTools.h"

#ifdef _WIN32
#  include "windows.h"
#endif

cmLocalFastbuildGenerator::cmLocalFastbuildGenerator(cmGlobalGenerator* gg,
                                                     cmMakefile* makefile)
  : cmLocalCommonGenerator(gg, makefile, makefile->GetCurrentBinaryDirectory())
{
}

void cmLocalFastbuildGenerator::Generate()
{
  const auto& targets = this->GetGeneratorTargets();
  for (const auto& target : targets) {
    if (target->GetType() == cmStateEnums::INTERFACE_LIBRARY) {
      continue;
    }
    cmFastbuildTargetGenerator* tg =
      cmFastbuildTargetGenerator::New(target.get());
    if (tg) {
      tg->Generate();
      delete tg;
    }
  }

  for (cmStateSnapshot const& state : GetStateSnapshot().GetChildren()) {
    std::string const& currentBinaryDir =
      state.GetDirectory().GetCurrentBinary();
    auto AllTargetName =
      GetGlobalFastbuildGenerator()->ConvertToFastbuildPath(currentBinaryDir);

    cmGlobalFastbuildGenerator::FastbuildTarget allTarget;
    cmGlobalFastbuildGenerator::FastbuildAliasNode allNode;
    allTarget.IsGlobal = true;
    allTarget.Name = allNode.Name = AllTargetName + "/all";
    for (const auto& Target : AllTargets[AllTargetName]) {
      allNode.Targets.insert(Target + "-products");
      allTarget.Dependencies.push_back(Target);
    }
    if (allNode.Targets.empty()) {
      allNode.Targets.insert("noop-products");
      allTarget.Dependencies.push_back("noop");
    }

    allTarget.AliasNodes.push_back(allNode);
    GetGlobalFastbuildGenerator()->AddTarget(allTarget);
  }
}

void cmLocalFastbuildGenerator::AddTarget(
  const cmGlobalFastbuildGenerator::FastbuildTarget& target)
{
  GetGlobalFastbuildGenerator()->AddTarget(target);
  if (!target.IsGlobal && !target.IsExcluded) {
    AllTargets[cmSystemTools::GetFilenamePath(target.Name)].push_back(
      target.Name);
  }
}

const cmGlobalFastbuildGenerator*
cmLocalFastbuildGenerator::GetGlobalFastbuildGenerator() const
{
  return static_cast<const cmGlobalFastbuildGenerator*>(
    this->GetGlobalGenerator());
}

cmGlobalFastbuildGenerator*
cmLocalFastbuildGenerator::GetGlobalFastbuildGenerator()
{
  return static_cast<cmGlobalFastbuildGenerator*>(this->GetGlobalGenerator());
}

void cmLocalFastbuildGenerator::ComputeObjectFilenames(
  std::map<cmSourceFile const*, std::string>& mapping,
  cmGeneratorTarget const* gt)
{
  for (auto& si : mapping) {
    cmSourceFile const* sf = si.first;
    si.second = this->GetObjectFileNameWithoutTarget(*sf, gt->ObjectDirectory);
  }
}

// TODO: Picked up from cmLocalUnixMakefileGenerator3.  Refactor it.
std::string cmLocalFastbuildGenerator::GetTargetDirectory(
  const cmGeneratorTarget* target) const
{
  std::string dir = "CMakeFiles/";
  dir += target->GetName();
#if defined(__VMS)
  dir += "_dir";
#else
  dir += ".dir";
#endif
  return dir;
}

void cmLocalFastbuildGenerator::AppendFlagEscape(
  std::string& flags, const std::string& rawFlag) const
{
  std::string escapedFlag = this->EscapeForShell(rawFlag);
  // Other make systems will remove the double $ but
  // fastbuild uses ^$ to escape it. So switch to that.
  // cmSystemTools::ReplaceString(escapedFlag, "$$", "^$");
  this->AppendFlags(flags, escapedFlag);
}
