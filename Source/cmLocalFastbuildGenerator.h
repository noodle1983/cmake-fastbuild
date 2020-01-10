/*============================================================================
  CMake - Cross Platform Makefile Generator
  Copyright 2000-2009 Kitware, Inc., Insight Software Consortium

  Distributed under the OSI-approved BSD License (the "License");
  see accompanying file Copyright.txt for details.

  This software is distributed WITHOUT ANY WARRANTY; without even the
  implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
  See the License for more information.
============================================================================*/
#ifndef cmLocalFastbuildGenerator_h
#define cmLocalFastbuildGenerator_h

#include "cmConfigure.h" // IWYU pragma: keep

#include "cmGlobalFastbuildGenerator.h"
#include "cmLocalCommonGenerator.h"

class cmSourceFile;
class cmGeneratedFileStream;

class cmLocalFastbuildGenerator : public cmLocalCommonGenerator
{
public:
  cmLocalFastbuildGenerator(cmGlobalGenerator* gg, cmMakefile* makefile);

  void Generate() override;

  void AppendFlagEscape(std::string& flags,
                        const std::string& rawFlag) const override;

  std::string GetTargetDirectory(const cmGeneratorTarget* gt) const override;

  void ComputeObjectFilenames(
    std::map<cmSourceFile const*, std::string>& mapping,
    cmGeneratorTarget const* gt) override;

  const cmGlobalFastbuildGenerator* GetGlobalFastbuildGenerator() const;
  cmGlobalFastbuildGenerator* GetGlobalFastbuildGenerator();

  void AddTarget(const cmGlobalFastbuildGenerator::FastbuildTarget& target);

private:
  std::map<std::string, std::vector<std::string>> AllTargets;
};

#endif
