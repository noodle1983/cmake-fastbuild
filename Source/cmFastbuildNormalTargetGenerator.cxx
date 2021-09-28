#include "cmFastbuildNormalTargetGenerator.h"

#include "cmComputeLinkInformation.h"
#include "cmCustomCommandGenerator.h"
#include "cmGeneratedFileStream.h"
#include "cmGeneratorExpression.h"
#include "cmGeneratorTarget.h"
#include "cmLinkLineComputer.h"
#include "cmLocalCommonGenerator.h"
#include "cmLocalFastbuildGenerator.h"
#include "cmMakefile.h"
#include "cmOSXBundleGenerator.h"
#include "cmRulePlaceholderExpander.h"
#include "cmSourceFile.h"
#include "cmState.h"
#include "cmStateDirectory.h"

namespace {
struct Objects
{
  std::vector<std::string> sourceFiles;
  std::set<std::string> extraOutputs;
  std::set<std::string> extraDependencies;
};

struct CompileCommand
{
  std::string flags;
  bool usePCH = false;
  std::map<std::string, Objects> objects;
};

void FilterSourceFiles(std::vector<cmSourceFile const*>& filteredSourceFiles,
                       std::vector<cmSourceFile const*>& sourceFiles,
                       const std::string& language)
{
  for (std::vector<cmSourceFile const*>::const_iterator i =
         sourceFiles.begin();
       i != sourceFiles.end(); ++i) {
    const cmSourceFile* sf = *i;
    if (sf->GetLanguage() == language) {
      filteredSourceFiles.push_back(sf);
    }
  }
}
}

cmFastbuildNormalTargetGenerator::cmFastbuildNormalTargetGenerator(
  cmGeneratorTarget* gt)
  : cmFastbuildTargetGenerator(gt)
{
  this->OSXBundleGenerator = new cmOSXBundleGenerator(gt);
  this->OSXBundleGenerator->SetMacContentFolders(&this->MacContentFolders);
}

cmFastbuildNormalTargetGenerator::~cmFastbuildNormalTargetGenerator()
{
  delete this->OSXBundleGenerator;
}

void cmFastbuildNormalTargetGenerator::DetectCompilerFlags(
  std::string& compileFlags, const cmSourceFile* source,
  const std::string& language)
{
  const auto& configName = GetConfigName();

  LocalCommonGenerator->GetTargetCompileFlags(
    this->GeneratorTarget, configName, language, compileFlags, "");

  cmGeneratorExpressionInterpreter genexInterpreter(
    this->GetLocalGenerator(), configName, this->GeneratorTarget, language);

  std::vector<std::string> includes;
  if (auto cincludes = source->GetProperty("INCLUDE_DIRECTORIES")) {
    LocalCommonGenerator->AppendIncludeDirectories(
      includes, genexInterpreter.Evaluate(*cincludes, "INCLUDE_DIRECTORIES"),
      *source);
  }

  LocalCommonGenerator->GetIncludeDirectories(includes, this->GeneratorTarget,
                                              language, configName);

  // Add include directory flags.
  std::string includeFlags = LocalCommonGenerator->GetIncludeFlags(
    includes, this->GeneratorTarget, language,
    language == "RC" ? true : false, // full include paths for RC
    // needed by cmcldeps
    false, configName);

  LocalCommonGenerator->AppendFlags(compileFlags, includeFlags);

  if (source) {
    if (auto cflags = source->GetProperty("COMPILE_FLAGS")) {
      LocalCommonGenerator->AppendFlags(
        compileFlags, genexInterpreter.Evaluate(*cflags, "COMPILE_FLAGS"));
    }
    if (auto cflags = source->GetProperty("COMPILE_OPTIONS")) {
      LocalCommonGenerator->AppendCompileOptions(
        compileFlags, genexInterpreter.Evaluate(*cflags, "COMPILE_OPTIONS"));
    }

    // Add precompile headers compile options.
    const std::string pchSource =
      this->GeneratorTarget->GetPchSource(configName, language);

    if (!pchSource.empty() &&
        !source->GetProperty("SKIP_PRECOMPILE_HEADERS")) {
      std::string pchOptions;
      if (source->GetFullPath() == pchSource) {
        pchOptions = this->GeneratorTarget->GetPchCreateCompileOptions(
          configName, language);
      } else {
        pchOptions =
          this->GeneratorTarget->GetPchUseCompileOptions(configName, language);
      }

      LocalCommonGenerator->AppendCompileOptions(
        compileFlags,
        genexInterpreter.Evaluate(pchOptions, "COMPILE_OPTIONS"));
    }
  }
}

void cmFastbuildNormalTargetGenerator::DetectOutput(
  FastbuildTargetNames& targetNamesOut, const std::string& configName)
{
  if (GeneratorTarget->HaveWellDefinedOutputFiles()) {
    targetNamesOut.targetOutputDir =
      GeneratorTarget->GetDirectory(configName) + "/";

    targetNamesOut.targetOutput = GeneratorTarget->GetFullPath(configName);
    targetNamesOut.targetOutputReal = GeneratorTarget->GetFullPath(
      configName, cmStateEnums::RuntimeBinaryArtifact,
      /*realpath=*/true);
    targetNamesOut.targetOutputImplib = GeneratorTarget->GetFullPath(
      configName, cmStateEnums::ImportLibraryArtifact);
  } else {
    targetNamesOut.targetOutputDir = Makefile->GetHomeOutputDirectory();
    if (targetNamesOut.targetOutputDir.empty() ||
        targetNamesOut.targetOutputDir == ".") {
      targetNamesOut.targetOutputDir = GeneratorTarget->GetName();
    } else {
      targetNamesOut.targetOutputDir += "/";
      targetNamesOut.targetOutputDir += GeneratorTarget->GetName();
    }
    targetNamesOut.targetOutputDir += "/";
    targetNamesOut.targetOutputDir += configName;
    targetNamesOut.targetOutputDir += "/";

    targetNamesOut.targetOutput =
      targetNamesOut.targetOutputDir + "/" + targetNamesOut.targetNameOut;
    targetNamesOut.targetOutputImplib =
      targetNamesOut.targetOutputDir + "/" + targetNamesOut.targetNameImport;
    targetNamesOut.targetOutputReal =
      targetNamesOut.targetOutputDir + "/" + targetNamesOut.targetNameReal;
  }

  if (GeneratorTarget->GetType() == cmStateEnums::EXECUTABLE ||
      GeneratorTarget->GetType() == cmStateEnums::STATIC_LIBRARY ||
      GeneratorTarget->GetType() == cmStateEnums::SHARED_LIBRARY ||
      GeneratorTarget->GetType() == cmStateEnums::MODULE_LIBRARY) {
    targetNamesOut.targetOutputPDBDir =
      GeneratorTarget->GetPDBDirectory(configName);
    targetNamesOut.targetOutputPDBDir += "/";
  }
  if (GeneratorTarget->GetType() <= cmStateEnums::OBJECT_LIBRARY) {
    targetNamesOut.targetOutputCompilePDBDir =
      GeneratorTarget->GetCompilePDBDirectory(configName);
    if (targetNamesOut.targetOutputCompilePDBDir.empty()) {
      targetNamesOut.targetOutputCompilePDBDir =
        GeneratorTarget->GetSupportDirectory() + "/";
    }
  }

  // Make sure all obey the correct slashes
  cmSystemTools::ConvertToOutputSlashes(targetNamesOut.targetOutput);
  cmSystemTools::ConvertToOutputSlashes(targetNamesOut.targetOutputImplib);
  cmSystemTools::ConvertToOutputSlashes(targetNamesOut.targetOutputReal);
  cmSystemTools::ConvertToOutputSlashes(targetNamesOut.targetOutputDir);
  cmSystemTools::ConvertToOutputSlashes(targetNamesOut.targetOutputPDBDir);
  cmSystemTools::ConvertToOutputSlashes(
    targetNamesOut.targetOutputCompilePDBDir);
}

void cmFastbuildNormalTargetGenerator::DetectLinkerLibPaths(
  std::string& linkerLibPath, const std::string& configName)
{
  cmComputeLinkInformation* pcli =
    GeneratorTarget->GetLinkInformation(configName);
  if (!pcli) {
    // No link information, then no linker library paths
    return;
  }
  cmComputeLinkInformation& cli = *pcli;

  std::string libPathFlag =
    Makefile->GetRequiredDefinition("CMAKE_LIBRARY_PATH_FLAG");
  std::string libPathTerminator =
    Makefile->GetSafeDefinition("CMAKE_LIBRARY_PATH_TERMINATOR");

  // Append the library search path flags.
  std::vector<std::string> const& libDirs = cli.GetDirectories();
  for (std::vector<std::string>::const_iterator libDir = libDirs.begin();
       libDir != libDirs.end(); ++libDir) {
    std::string libpath = LocalCommonGenerator->ConvertToOutputForExisting(
      *libDir, cmLocalGenerator::SHELL);
    cmSystemTools::ConvertToOutputSlashes(libpath);

    // Add the linker lib path twice, once raw, then once with
    // the configname attached
    std::string configlibpath = libpath + "/" + configName;
    cmSystemTools::ConvertToOutputSlashes(configlibpath);

    linkerLibPath += " " + libPathFlag;
    linkerLibPath += libpath;
    linkerLibPath += libPathTerminator;

    linkerLibPath += " " + libPathFlag;
    linkerLibPath += configlibpath;
    linkerLibPath += libPathTerminator;
    linkerLibPath += " ";
  }
}

bool cmFastbuildNormalTargetGenerator::DetectBaseLinkerCommand(
  std::string& command, const std::string& configName)
{
  auto* gt = this->GetGeneratorTarget();
  const std::string& linkLanguage = gt->GetLinkerLanguage(configName);
  if (linkLanguage.empty()) {
    cmSystemTools::Error("CMake can not determine linker language for "
                         "target: " +
                         gt->GetName());
    return false;
  }

  std::string linkLibs;
  std::string targetFlags;
  std::string linkFlags;
  std::string frameworkPath;
  std::string dummyLinkPath;

  cmLocalGenerator* root =
    LocalCommonGenerator->GetGlobalGenerator()->GetLocalGenerators()[0].get();
  std::unique_ptr<cmLinkLineComputer> linkLineComputer(
    LocalCommonGenerator->GetGlobalGenerator()->CreateLinkLineComputer(
      root, root->GetStateSnapshot().GetDirectory()));

  LocalCommonGenerator->GetTargetFlags(linkLineComputer.get(), configName,
                                       linkLibs, targetFlags, linkFlags,
                                       frameworkPath, dummyLinkPath, gt);

  auto const targetType = gt->GetType();
  // Add OS X version flags, if any.
  if (targetType == cmStateEnums::SHARED_LIBRARY ||
      targetType == cmStateEnums::MODULE_LIBRARY) {
    this->AppendOSXVerFlag(linkFlags, linkLanguage, "COMPATIBILITY", true);
    this->AppendOSXVerFlag(linkFlags, linkLanguage, "CURRENT", false);
  }
  // Add Arch flags to link flags for binaries
  if (targetType == cmStateEnums::SHARED_LIBRARY ||
      targetType == cmStateEnums::MODULE_LIBRARY ||
      targetType == cmStateEnums::EXECUTABLE) {
    root->AddArchitectureFlags(linkFlags, gt,
                               gt->GetLinkerLanguage(configName), configName);
  }

  std::string linkPath;
  DetectLinkerLibPaths(linkPath, configName);

  UnescapeFastbuildVariables(linkLibs);
  UnescapeFastbuildVariables(targetFlags);
  UnescapeFastbuildVariables(linkFlags);
  UnescapeFastbuildVariables(frameworkPath);
  UnescapeFastbuildVariables(linkPath);

  linkPath = frameworkPath + linkPath;

  cmGeneratorTarget::ModuleDefinitionInfo const* mdi =
    gt->GetModuleDefinitionInfo(configName);
  if (mdi && !mdi->DefFile.empty()) {
    auto const* const defFileFlag =
      LocalCommonGenerator->GetMakefile()->GetDefinition(
        "CMAKE_LINK_DEF_FILE_FLAG");
    if (defFileFlag) {
      linkFlags += *defFileFlag +
        this->LocalCommonGenerator->ConvertToOutputFormat(
          linkLineComputer->ConvertToLinkReference(mdi->DefFile),
          cmOutputConverter::SHELL);
    }
  }
  linkFlags += " " + linkPath;

  cmRulePlaceholderExpander::RuleVariables vars;
  vars.CMTargetName = gt->GetName().c_str();
  vars.CMTargetType = cmState::GetTargetTypeName(targetType).c_str();
  vars.Language = linkLanguage.c_str();
  const std::string manifests = this->GetManifestsAsFastbuildPath();
  vars.Manifests = manifests.c_str();

  std::string responseFlag;
  vars.Objects =
    FASTBUILD_DOLLAR_TAG "FB_INPUT_1_PLACEHOLDER" FASTBUILD_DOLLAR_TAG;
  vars.LinkLibraries = linkLibs.c_str();

  vars.ObjectDir = FASTBUILD_DOLLAR_TAG "TargetOutDir" FASTBUILD_DOLLAR_TAG;
  vars.Target =
    FASTBUILD_DOLLAR_TAG "FB_INPUT_2_PLACEHOLDER" FASTBUILD_DOLLAR_TAG;

  vars.TargetSOName = "";
  vars.TargetPDB =
    FASTBUILD_DOLLAR_TAG "TargetOutPDBPath" FASTBUILD_DOLLAR_TAG;

  // Setup the target version.
  std::string targetVersionMajor;
  std::string targetVersionMinor;
  {
    std::ostringstream majorStream;
    std::ostringstream minorStream;
    int major;
    int minor;
    gt->GetTargetVersion(major, minor);
    majorStream << major;
    minorStream << minor;
    targetVersionMajor = majorStream.str();
    targetVersionMinor = minorStream.str();
  }
  vars.TargetVersionMajor = targetVersionMajor.c_str();
  vars.TargetVersionMinor = targetVersionMinor.c_str();

  vars.Defines =
    FASTBUILD_DOLLAR_TAG "CompileDefineFlags" FASTBUILD_DOLLAR_TAG;
  vars.Flags = targetFlags.c_str();
  vars.LinkFlags = linkFlags.c_str();

  vars.LanguageCompileFlags = "";
  // Rule for linking library/executable.
  std::string launcher;
  auto const* const val =
    LocalCommonGenerator->GetRuleLauncher(gt, "RULE_LAUNCH_LINK");
  if (val && !val->empty()) {
    launcher = *val;
    launcher += " ";
  }

  std::unique_ptr<cmRulePlaceholderExpander> rulePlaceholderExpander(
    LocalCommonGenerator->CreateRulePlaceholderExpander());
  rulePlaceholderExpander->SetTargetImpLib(
    FASTBUILD_DOLLAR_TAG "TargetOutputImplib" FASTBUILD_DOLLAR_TAG);

  std::vector<std::string> linkCmds;
  ComputeLinkCmds(linkCmds, configName);
  for (std::vector<std::string>::iterator i = linkCmds.begin();
       i != linkCmds.end(); ++i) {
    *i = launcher + *i;
    rulePlaceholderExpander->ExpandRuleVariables(
      (cmLocalFastbuildGenerator*)LocalCommonGenerator, *i, vars);
  }

  command = BuildCommandLine(linkCmds);

  return true;
}

void cmFastbuildNormalTargetGenerator::ComputeLinkCmds(
  std::vector<std::string>& linkCmds, std::string configName)
{
  const std::string& linkLanguage =
    GeneratorTarget->GetLinkerLanguage(configName);
  {
    std::string linkCmdVar =
      GeneratorTarget->GetCreateRuleVariable(linkLanguage, configName);
    auto const* const linkCmd = Makefile->GetDefinition(linkCmdVar);
    if (linkCmd) {
      std::string linkCmdStr = *linkCmd;
      if (this->GetGeneratorTarget()->HasImplibGNUtoMS(configName)) {
        std::string ruleVar = "CMAKE_";
        ruleVar +=
          this->GeneratorTarget->GetLinkerLanguage(this->GetConfigName());
        ruleVar += "_GNUtoMS_RULE";
        if (auto const* const rule = this->Makefile->GetDefinition(ruleVar)) {
          linkCmdStr += *rule;
        }
      }
      cmExpandList(linkCmdStr, linkCmds);
      if (this->GetGeneratorTarget()->GetPropertyAsBool("LINK_WHAT_YOU_USE")) {
        std::string cmakeCommand =
          this->GetLocalGenerator()->ConvertToOutputFormat(
            cmSystemTools::GetCMakeCommand(), cmLocalGenerator::SHELL);
        cmakeCommand += " -E __run_co_compile --lwyu=";
        cmGeneratorTarget& gt = *this->GetGeneratorTarget();
        const std::string cfgName = this->GetConfigName();
        std::string targetOutputReal = this->ConvertToFastbuildPath(
          gt.GetFullPath(cfgName, cmStateEnums::RuntimeBinaryArtifact,
                         /*realname=*/true));
        cmakeCommand += targetOutputReal;
        cmakeCommand += " || true";
        linkCmds.push_back(std::move(cmakeCommand));
      }
      return;
    }
  }

  // If the above failed, then lets try this:
  switch (GeneratorTarget->GetType()) {
    case cmStateEnums::STATIC_LIBRARY: {
      std::string linkCmdVar = "CMAKE_";
      linkCmdVar += linkLanguage;
      linkCmdVar += "_ARCHIVE_CREATE";
      const std::string linkCmd = Makefile->GetRequiredDefinition(linkCmdVar);
      cmExpandList(linkCmd, linkCmds);
      // TODO cmake use ar && ranlib ,but fastbuild only supports one command
      std::string& toReplace = linkCmds.back();
      toReplace.replace(toReplace.find(" qc "), 4, " rcs ");
      return;
    }
    case cmStateEnums::SHARED_LIBRARY:
    case cmStateEnums::MODULE_LIBRARY:
    case cmStateEnums::EXECUTABLE:
      break;
    default:
      assert(0 && "Unexpected target type");
  }
  return;
}

std::string cmFastbuildNormalTargetGenerator::ComputeDefines(
  const cmSourceFile* source, const std::string& configName,
  const std::string& language)
{
  std::set<std::string> defines;
  cmGeneratorExpressionInterpreter genexInterpreter(
    this->GetLocalGenerator(), configName, this->GeneratorTarget, language);

  const std::string COMPILE_DEFINITIONS("COMPILE_DEFINITIONS");
  if (auto compile_defs = source->GetProperty(COMPILE_DEFINITIONS)) {
    this->GetLocalGenerator()->AppendDefines(
      defines, genexInterpreter.Evaluate(*compile_defs, COMPILE_DEFINITIONS));
  }

  std::string defPropName = "COMPILE_DEFINITIONS_";
  defPropName += cmSystemTools::UpperCase(configName);
  if (auto config_compile_defs = source->GetProperty(defPropName)) {
    this->GetLocalGenerator()->AppendDefines(
      defines,
      genexInterpreter.Evaluate(*config_compile_defs, COMPILE_DEFINITIONS));
  }

  std::string definesString = this->GetDefines(language, configName);
  this->GetLocalGenerator()->JoinDefines(defines, definesString, language);

  return definesString;
}

void cmFastbuildNormalTargetGenerator::DetectTargetObjectDependencies(
  const std::string& configName, std::vector<std::string>& dependencies)
{
  // Iterate over all source files and look for
  // object file dependencies
  std::set<std::string> objectLibs;

  std::vector<cmSourceFile*> sourceFiles;
  GeneratorTarget->GetSourceFiles(sourceFiles, configName);
  for (cmSourceFile const* sf : sourceFiles) {
    const std::string& objectLib = sf->GetObjectLibrary();
    if (!objectLib.empty()) {
      // Find the target this actually is (might be an alias)
      const cmGeneratorTarget* objectTarget =
        GlobalCommonGenerator->FindGeneratorTarget(objectLib);
      if (objectTarget) {
        std::vector<const cmSourceFile*> objFiles;
        objectTarget->GetObjectSources(objFiles, configName);
        if (!objFiles.empty())
          objectLibs.insert(objectTarget->GetName() + "-objects");
      }
    }
  }

  // Now add the external obj files that also need to be linked in
  std::vector<const cmSourceFile*> objFiles;
  GeneratorTarget->GetExternalObjects(objFiles, configName);
  for (cmSourceFile const* sf : objFiles) {
    const std::string& objectLib = sf->GetObjectLibrary();
    if (objectLib.empty()) {
      objectLibs.insert(this->ConvertToFastbuildPath(sf->GetFullPath()));
    } else {
      // Find the target this actually is (might be an alias)
      const cmGeneratorTarget* objectTarget =
        GlobalCommonGenerator->FindGeneratorTarget(objectLib);
      if (!objectTarget) {
        objectLibs.insert(this->ConvertToFastbuildPath(sf->GetFullPath()));
      } else {
        std::vector<const cmSourceFile*> objFiles;
        objectTarget->GetObjectSources(objFiles, configName);
        if (objFiles.empty())
          objectLibs.insert(this->ConvertToFastbuildPath(sf->GetFullPath()));
      }
    }
  }

  std::copy(objectLibs.begin(), objectLibs.end(),
            std::back_inserter(dependencies));
}

std::string cmFastbuildNormalTargetGenerator::BuildCommandLine(
  const std::vector<std::string>& cmdLines)
{
#ifdef _WIN32
  const char* cmdExe = "cmd.exe";
  std::string cmdExeAbsolutePath = cmSystemTools::FindProgram(cmdExe);
#endif

  // If we have no commands but we need to build a command anyway, use ":".
  // This happens when building a POST_BUILD value for link targets that
  // don't use POST_BUILD.
  if (cmdLines.empty()) {
#ifdef _WIN32
    return cmdExeAbsolutePath + " /C \"cd .\"";
#else
    return ":";
#endif
  }

  std::ostringstream cmd;
  for (std::vector<std::string>::const_iterator li = cmdLines.begin();
       li != cmdLines.end(); ++li)
#ifdef _WIN32
  {
    if (li != cmdLines.begin()) {
      cmd << " && ";
    } else if (cmdLines.size() > 1) {
      cmd << cmdExeAbsolutePath << " /C \"";
    }
    cmd << *li;
  }
  if (cmdLines.size() > 1) {
    cmd << "\"";
  }
#else
  {
    if (li != cmdLines.begin()) {
      cmd << " && ";
    }
    cmd << *li;
  }
#endif
  std::string cmdOut = cmd.str();
  UnescapeFastbuildVariables(cmdOut);

  cmSystemTools::ReplaceString(cmdOut, "\n", " ");

  return cmdOut;
}

void cmFastbuildNormalTargetGenerator::SplitExecutableAndFlags(
  const std::string& command, std::string& program, std::string& args)
{
  const char* c = command.c_str();

  // Skip leading whitespace.
  while (isspace(static_cast<unsigned char>(*c))) {
    ++c;
  }

  // Parse one command-line element up to an unquoted space.
  bool in_double = false;
  bool in_single = false;
  for (; *c; ++c) {
    if (in_single) {
      if (*c == '\'') {
        in_single = false;
      } else {
        program += *c;
      }
    } else if (in_double) {
      if (*c == '"') {
        in_double = false;
      } else {
        program += *c;
      }
    } else if (*c == '"') {
      in_double = true;
    } else if (*c == '\'') {
      in_single = true;
    } else if (isspace(static_cast<unsigned char>(*c))) {
      break;
    } else {
      program += *c;
    }
  }

  // The remainder of the command line holds unparsed arguments.
  args = c;
}

void cmFastbuildNormalTargetGenerator::EnsureDirectoryExists(
  const std::string& path, const char* homeOutputDirectory)
{
  if (cmSystemTools::FileIsFullPath(path.c_str())) {
    cmSystemTools::MakeDirectory(path.c_str());
  } else {
    const std::string fullPath = std::string(homeOutputDirectory) + "/" + path;
    cmSystemTools::MakeDirectory(fullPath.c_str());
  }
}

std::string cmFastbuildNormalTargetGenerator::GetManifestsAsFastbuildPath()
{
  std::vector<cmSourceFile const*> manifest_srcs;
  this->GeneratorTarget->GetManifests(manifest_srcs, this->GetConfigName());

  std::vector<std::string> manifests;
  for (std::vector<cmSourceFile const*>::iterator mi = manifest_srcs.begin();
       mi != manifest_srcs.end(); ++mi) {
    manifests.push_back(ConvertToFastbuildPath((*mi)->GetFullPath()));
  }

  return cmJoin(manifests, " ");
}

std::vector<std::string> cmFastbuildNormalTargetGenerator::GetLanguages()
{
  // Write rules for languages compiled in this target.
  std::set<std::string> languages;
  std::vector<cmSourceFile const*> sourceFiles;
  this->GetGeneratorTarget()->GetObjectSources(
    sourceFiles, this->GetMakefile()->GetSafeDefinition("CMAKE_BUILD_TYPE"));
  for (cmSourceFile const* sf : sourceFiles) {
    std::string const lang = sf->GetLanguage();
    if (!lang.empty()) {
      languages.insert(lang);
    }
  }

  return std::vector<std::string>(languages.begin(), languages.end());
}

std::vector<cmGlobalFastbuildGenerator::FastbuildObjectListNode>
cmFastbuildNormalTargetGenerator::GenerateObjects()
{
  std::map<std::string, cmGlobalFastbuildGenerator::FastbuildObjectListNode>
    objectsByName;

  const std::string& targetName = GeneratorTarget->GetName();
  const std::string& configName = this->GetConfigName();

  // Figure out the list of languages in use by this target
  std::set<std::string> languages;

  std::vector<const cmSourceFile*> sourceFiles;
  this->GeneratorTarget->GetObjectSources(sourceFiles, configName);
  for (const cmSourceFile* sourceFile : sourceFiles) {
    const std::string& lang = sourceFile->GetLanguage();
    if (!lang.empty()) {
      this->GetGlobalGenerator()->AddCompiler(lang, Makefile);
      languages.insert(lang);
    }
  }

  // Write the object list definitions for each language
  // stored in this target
  for (const std::string& language : languages) {
    const std::string pchSource =
      this->GeneratorTarget->GetPchSource(configName, language);
    const std::string pchFile =
      this->GeneratorTarget->GetPchFile(this->GetConfigName(), language);
    auto pchReuseFrom =
      this->GeneratorTarget->GetProperty("PRECOMPILE_HEADERS_REUSE_FROM");
    cmGeneratorTarget* generatorTarget = this->GeneratorTarget;
    if (pchReuseFrom) {
      generatorTarget =
        this->GetGlobalGenerator()->FindGeneratorTarget(*pchReuseFrom);
    }
    const std::string pchObject =
      this->GetGlobalGenerator()->ConvertToFastbuildPath(
        generatorTarget->GetPchFileObject(this->GetConfigName(), language));
    std::string pchOptions;

    cmRulePlaceholderExpander::RuleVariables compileObjectVars;
    compileObjectVars.CMTargetName = GeneratorTarget->GetName().c_str();
    compileObjectVars.CMTargetType =
      cmState::GetTargetTypeName(GeneratorTarget->GetType()).c_str();
    compileObjectVars.Language = language.c_str();
    compileObjectVars.Source =
      FASTBUILD_DOLLAR_TAG "FB_INPUT_1_PLACEHOLDER" FASTBUILD_DOLLAR_TAG;
    compileObjectVars.Object =
      FASTBUILD_DOLLAR_TAG "FB_INPUT_2_PLACEHOLDER" FASTBUILD_DOLLAR_TAG;
    compileObjectVars.ObjectDir =
      FASTBUILD_DOLLAR_TAG "TargetOutputDir" FASTBUILD_DOLLAR_TAG;
    compileObjectVars.ObjectFileDir = "";
    compileObjectVars.Flags = "";
    compileObjectVars.Includes = "";
    const std::string manifests = this->GetManifestsAsFastbuildPath();
    compileObjectVars.Manifests = manifests.c_str();
    compileObjectVars.Defines = "";
    compileObjectVars.Includes = "";
    compileObjectVars.TargetCompilePDB =
      FASTBUILD_DOLLAR_TAG "TargetOutCompilePDBPath" FASTBUILD_DOLLAR_TAG;

    // Rule for compiling object file.
    std::vector<std::string> compileCmds;
    if (language == "CUDA") {
      std::string cmdVar;
      if (this->GeneratorTarget->GetPropertyAsBool(
            "CUDA_SEPARABLE_COMPILATION")) {
        cmdVar = "CMAKE_CUDA_COMPILE_SEPARABLE_COMPILATION";
      } else if (this->GeneratorTarget->GetPropertyAsBool(
                   "CUDA_PTX_COMPILATION")) {
        cmdVar = "CMAKE_CUDA_COMPILE_PTX_COMPILATION";
      } else {
        cmdVar = "CMAKE_CUDA_COMPILE_WHOLE_COMPILATION";
      }
      const std::string& compileCmd =
        LocalCommonGenerator->GetMakefile()->GetRequiredDefinition(cmdVar);
      cmExpandList(compileCmd, compileCmds);
    } else {
      std::string compileCmdVar = "CMAKE_";
      compileCmdVar += language;
      compileCmdVar += "_COMPILE_OBJECT";
      std::string compileCmd =
        LocalCommonGenerator->GetMakefile()->GetRequiredDefinition(
          compileCmdVar);
      cmExpandList(compileCmd, compileCmds);
    }

    // See if we need to use a compiler launcher like ccache or distcc
    std::string compilerLauncher;
    if (!compileCmds.empty() &&
        (language == "C" || language == "CXX" || language == "Fortran" ||
         language == "CUDA")) {
      std::string const clauncher_prop = language + "_COMPILER_LAUNCHER";
      auto clauncher = this->GeneratorTarget->GetProperty(clauncher_prop);
      if (clauncher) {
        compilerLauncher = *clauncher;
      }
    }

    std::string compilerId = "Compiler_" + language;

    // Maybe insert an include-what-you-use runner.
    if (!compileCmds.empty() && (language == "C" || language == "CXX")) {
      std::string const iwyu_prop = language + "_INCLUDE_WHAT_YOU_USE";
      auto iwyu = this->GeneratorTarget->GetProperty(iwyu_prop);
      std::string const tidy_prop = language + "_CLANG_TIDY";
      auto tidy = this->GeneratorTarget->GetProperty(tidy_prop);
      std::string const cpplint_prop = language + "_CPPLINT";
      auto cpplint = this->GeneratorTarget->GetProperty(cpplint_prop);
      std::string const cppcheck_prop = language + "_CPPCHECK";
      auto cppcheck = this->GeneratorTarget->GetProperty(cppcheck_prop);
      if (iwyu || tidy || cpplint || cppcheck) {
        std::string const cmakeCmd =
          this->GetLocalGenerator()->ConvertToOutputFormat(
            cmSystemTools::GetCMakeCommand(), cmLocalGenerator::SHELL);
        std::string run_iwyu = cmStrCat(cmakeCmd, " -E __run_co_compile");
        if (!compilerLauncher.empty()) {
          // In __run_co_compile case the launcher command is supplied
          // via --launcher=<maybe-list> and consumed
          run_iwyu += " --launcher=";
          run_iwyu +=
            this->GetLocalGenerator()->EscapeForShell(compilerLauncher);
          compilerLauncher.clear();
        } else {
          compilerId = this->GetGlobalGenerator()->AddLauncher(
            cmSystemTools::GetCMakeCommand(), language, Makefile);
        }
        if (iwyu) {
          run_iwyu += " --iwyu=";
          run_iwyu += this->GetLocalGenerator()->EscapeForShell(*iwyu);
        }
        if (tidy) {
          run_iwyu += " --tidy=";
          run_iwyu += this->GetLocalGenerator()->EscapeForShell(*tidy);
        }
        if (cpplint) {
          run_iwyu += " --cpplint=";
          run_iwyu += this->GetLocalGenerator()->EscapeForShell(*cpplint);
        }
        if (cppcheck) {
          run_iwyu += " --cppcheck=";
          run_iwyu += this->GetLocalGenerator()->EscapeForShell(*cppcheck);
        }
        if (tidy || cpplint || cppcheck) {
          run_iwyu += " --source=" FASTBUILD_DOLLAR_TAG
                      "FB_INPUT_1_PLACEHOLDER" FASTBUILD_DOLLAR_TAG;
        }
        run_iwyu += " -- ";
        compileCmds.front().insert(0, run_iwyu);
      }
    }

    // If compiler launcher was specified and not consumed above, it
    // goes to the beginning of the command line.
    if (!compileCmds.empty() && !compilerLauncher.empty()) {
      std::vector<std::string> args = cmExpandedList(compilerLauncher, true);
      if (!args.empty()) {
        compilerId =
          this->GetGlobalGenerator()->AddLauncher(args[0], language, Makefile);

        args[0] = this->GetLocalGenerator()->ConvertToOutputFormat(
          args[0], cmOutputConverter::SHELL);
        for (std::string& i : cmMakeRange(args.begin() + 1, args.end())) {
          i = this->GetLocalGenerator()->EscapeForShell(i);
        }
      }
      compileCmds.front().insert(0, cmJoin(args, " ") + " ");
    }

    std::unique_ptr<cmRulePlaceholderExpander> rulePlaceholderExpander(
      LocalCommonGenerator->CreateRulePlaceholderExpander());

    rulePlaceholderExpander->SetTargetImpLib(
      FASTBUILD_DOLLAR_TAG "TargetOutputImplib" FASTBUILD_DOLLAR_TAG);

    std::map<std::string, CompileCommand> commandPermutations;

    // Source files
    {
      // get a list of source files
      std::vector<cmSourceFile const*> objectSources;
      GeneratorTarget->GetObjectSources(objectSources, configName);

      std::vector<cmSourceFile const*> filteredObjectSources;
      FilterSourceFiles(filteredObjectSources, objectSources, language);

      CompileCommand pchCommand;
      // Figure out the compilation commands for all
      // the translation units in the compilation.
      // Detect if one of them is a PreCompiledHeader
      // and extract it to be used in a precompiled header
      // generation step.
      for (cmSourceFile const* srcFile : filteredObjectSources) {
        // Detect flags and defines
        std::string compilerFlags;
        DetectCompilerFlags(compilerFlags, srcFile, language);
        std::string compileDefines =
          ComputeDefines(srcFile, configName, language);

        UnescapeFastbuildDefines(compileDefines);

        compileObjectVars.Flags = compilerFlags.c_str();
        compileObjectVars.Defines = compileDefines.c_str();

        auto compileCmds_ = compileCmds;
        for (auto& compileCmdStr : compileCmds_) {
          rulePlaceholderExpander->ExpandRuleVariables(
            (cmLocalFastbuildGenerator*)LocalCommonGenerator, compileCmdStr,
            compileObjectVars);
        }
        auto compileCmd = BuildCommandLine(compileCmds_);

        std::string executable, baseCompileFlags;
        SplitExecutableAndFlags(compileCmd, executable, baseCompileFlags);

        if (srcFile->GetFullPath() == pchSource) {
          pchOptions = baseCompileFlags;
          cmSystemTools::ReplaceString(pchOptions, "$FB_INPUT_2_PLACEHOLDER$",
                                       pchObject);
          continue;
        }

        const bool usePCH = !pchReuseFrom && !pchSource.empty() &&
          !srcFile->GetProperty("SKIP_PRECOMPILE_HEADERS");

        std::string const& directory = cmSystemTools::GetFilenamePath(
          this->GeneratorTarget->GetObjectName(srcFile));

        std::string configKey =
          baseCompileFlags + "{|}" + (usePCH ? "usePCH" : "");
        CompileCommand& command = commandPermutations[configKey];
        auto& commandObjects = command.objects[directory];
        commandObjects.sourceFiles.push_back(srcFile->GetFullPath());
        command.flags = baseCompileFlags;
        command.usePCH = usePCH;

        if (auto objectOutputs = srcFile->GetProperty("OBJECT_OUTPUTS")) {
          auto outputs = cmExpandedList(*objectOutputs);
          outputs = GetGlobalGenerator()->ConvertToFastbuildPath(outputs);
          for (const auto& output : outputs)
            commandObjects.extraOutputs.insert(output);
        }

        if (auto objectDepends = srcFile->GetProperty("OBJECT_DEPENDS")) {
          auto dependencies = cmExpandedList(*objectDepends);
          dependencies =
            GetGlobalGenerator()->ConvertToFastbuildPath(dependencies);

          for (const auto& dependency : dependencies)
            commandObjects.extraDependencies.insert(dependency);
        }
      }
    }

    // Iterate over all subObjectGroups
    std::string objectGroupRuleName =
      cmStrCat(language, "_ObjectGroup_", targetName);
    std::vector<std::string> configObjectGroups;
    int groupNameCount = 1;
    for (const auto& [key, command] : commandPermutations) {
      for (const auto& [folderName, commandObjects] : command.objects) {
        std::string targetCompileOutDirectory =
          this->GeneratorTarget->GetSupportDirectory();

        const auto ruleName = cmStrCat(objectGroupRuleName, "-", folderName,
                                       "-", std::to_string(groupNameCount++));

        cmGlobalFastbuildGenerator::FastbuildObjectListNode objectListNode;

        configObjectGroups.push_back(ruleName);

        objectListNode.Name = ruleName;
        objectListNode.Compiler = "." + compilerId;
        objectListNode.CompilerOptions = command.flags;
        objectListNode.CompilerInputFiles =
          GetGlobalGenerator()->ConvertToFastbuildPath(
            commandObjects.sourceFiles);
        objectListNode.CompilerOutputPath =
          GetGlobalGenerator()->ConvertToFastbuildPath(
            targetCompileOutDirectory + "/" + folderName);

        objectListNode.ObjectDependencies =
          std::vector<std::string>(commandObjects.extraDependencies.begin(),
                                   commandObjects.extraDependencies.end());
        objectListNode.ObjectOutputs =
          std::vector<std::string>(commandObjects.extraOutputs.begin(),
                                   commandObjects.extraOutputs.end());

        if (!pchSource.empty() && command.usePCH) {
          objectListNode.PCHInputFile =
            GetGlobalGenerator()->ConvertToFastbuildPath(pchSource);
          objectListNode.PCHOptions = pchOptions;
          objectListNode.PCHOutputFile = pchFile;
        }

        // TODO: Ask cmake the output objects and group by extension instead of
        // doing this
        if (language == "RC") {
          objectListNode.CompilerOutputExtension = ".res";
        } else {
          std::string outputExtensionVar = std::string("CMAKE_") + language +
            std::string("_OUTPUT_EXTENSION");
          auto const* const outputExtension =
            Makefile->GetDefinition(outputExtensionVar);

          objectListNode.CompilerOutputExtension = *outputExtension;
        }

        objectsByName[objectListNode.Name] = objectListNode;
      }
    }

    std::vector<cmSourceFile const*> headerSources;
    this->GeneratorTarget->GetHeaderSources(headerSources, configName);
    this->OSXBundleGenerator->GenerateMacOSXContentStatements(
      headerSources, this->MacOSXContentGenerator, configName);
    std::vector<cmSourceFile const*> extraSources;
    this->GeneratorTarget->GetExtraSources(extraSources, configName);
    this->OSXBundleGenerator->GenerateMacOSXContentStatements(
      extraSources, this->MacOSXContentGenerator, configName);

    configObjectGroups.insert(configObjectGroups.end(), ExtraFiles.begin(),
                              ExtraFiles.end());
    if (!configObjectGroups.empty()) {
      // TODO: Write an alias for this object group to group them all together
    }
  }

  std::map<std::string, std::string> objectOutputs;
  for (const auto& [name, object] : objectsByName) {
    for (const auto& output : object.ObjectOutputs) {
      objectOutputs[output] = name;
    }
  }
  for (auto& [name, object] : objectsByName) {
    for (auto it = object.ObjectDependencies.begin();
         it != object.ObjectDependencies.end();) {
      if (auto dependency = objectOutputs.find(*it);
          dependency != objectOutputs.end()) {
        object.PreBuildDependencies.insert(dependency->second);
        it = object.ObjectDependencies.erase(it);
      } else {
        ++it;
      }
    }
  }

  std::vector<std::string> objectNames;
  std::unordered_multimap<std::string, std::string> dependencies;
  for (const auto& [name, object] : objectsByName) {
    objectNames.push_back(object.Name);
    for (const auto& dependency : object.PreBuildDependencies) {
      dependencies.emplace(object.Name, dependency);
    }
  }
  // HACK: Makes sure that the CUDA object files go to the bottom, makes it
  // easier for Fastbuild to pick the right includes
  std::partition(objectNames.begin(), objectNames.end(), [](const auto& name) {
    return name.substr(0, 2) == "C_" || name.substr(0, 4) == "CXX_";
  });
  cmGlobalFastbuildGenerator::SortByDependencies(objectNames, dependencies);

  std::vector<cmGlobalFastbuildGenerator::FastbuildObjectListNode> objects;
  for (const auto& name : objectNames)
    objects.push_back(objectsByName.at(name));

  return objects;
}

std::vector<cmGlobalFastbuildGenerator::FastbuildLinkerNode>
cmFastbuildNormalTargetGenerator::GenerateLink(
  const std::vector<cmGlobalFastbuildGenerator::FastbuildObjectListNode>&
    objectLists)
{
  cmGlobalFastbuildGenerator::FastbuildLinkerNode linkerNode;

  // Detection of the link command as follows:
  switch (GeneratorTarget->GetType()) {
    case cmStateEnums::EXECUTABLE: {
      linkerNode.Type =
        cmGlobalFastbuildGenerator::FastbuildLinkerNode::EXECUTABLE;
      break;
    }
    case cmStateEnums::MODULE_LIBRARY: {
      linkerNode.Type =
        cmGlobalFastbuildGenerator::FastbuildLinkerNode::SHARED_LIBRARY;
      break;
    }
    case cmStateEnums::SHARED_LIBRARY: {
      linkerNode.Type =
        cmGlobalFastbuildGenerator::FastbuildLinkerNode::SHARED_LIBRARY;
      break;
    }
    case cmStateEnums::STATIC_LIBRARY: {
      linkerNode.Type =
        cmGlobalFastbuildGenerator::FastbuildLinkerNode::STATIC_LIBRARY;
      break;
    }
    default: {
      return {};
    }
  }

  const auto targetName = GeneratorTarget->GetName();

  const std::string& configName = this->GetConfigName();

  FastbuildTargetNames targetNames;
  DetectOutput(targetNames, configName);

  auto targetOutput = ConvertToFastbuildPath(targetNames.targetOutput);
  // std::string targetOutput =
  // ConvertToFastbuildPath(GeneratorTarget->GetFullPath(configName));
  std::string targetOutputReal =
    ConvertToFastbuildPath(GeneratorTarget->GetFullPath(
      configName, cmStateEnums::RuntimeBinaryArtifact,
      /*realname=*/true));
  std::string targetOutputImplib =
    ConvertToFastbuildPath(GeneratorTarget->GetFullPath(
      configName, cmStateEnums::ImportLibraryArtifact));

  std::string install_dir =
    this->GetGeneratorTarget()->GetInstallNameDirForBuildTree(configName);

  if (GeneratorTarget->IsAppBundleOnApple()) {
    // Create the app bundle
    std::string outpath = GeneratorTarget->GetDirectory(configName);
    this->OSXBundleGenerator->CreateAppBundle(targetNames.targetNameOut,
                                              outpath, configName);

    // Calculate the output path
    targetOutput = outpath;
    targetOutput += "/";
    targetOutput += targetNames.targetNameOut;
    targetOutput = this->ConvertToFastbuildPath(targetOutput);
    targetOutputReal = outpath;
    targetOutputReal += "/";
    targetOutputReal += targetNames.targetOutputReal;
    targetOutputReal = this->ConvertToFastbuildPath(targetOutputReal);
  } else if (GeneratorTarget->IsFrameworkOnApple()) {
    // Create the library framework.
    this->OSXBundleGenerator->CreateFramework(
      targetNames.targetNameOut, GeneratorTarget->GetDirectory(configName),
      configName);
  } else if (GeneratorTarget->IsCFBundleOnApple()) {
    // Create the core foundation bundle.
    this->OSXBundleGenerator->CreateCFBundle(
      targetNames.targetNameOut, GeneratorTarget->GetDirectory(configName),
      configName);
  }

  // Compile directory always needs to exist
  EnsureDirectoryExists(targetNames.targetOutputCompilePDBDir,
                        Makefile->GetHomeOutputDirectory().c_str());

  // on Windows the output dir is already needed at compile time
  // ensure the directory exists (OutDir test)
  EnsureDirectoryExists(targetNames.targetOutputDir,
                        Makefile->GetHomeOutputDirectory().c_str());
  EnsureDirectoryExists(targetNames.targetOutputPDBDir,
                        Makefile->GetHomeOutputDirectory().c_str());

  // Remove the command from the front and leave the flags behind
  std::string linkCmd;
  if (!DetectBaseLinkerCommand(linkCmd, configName)) {
    return {};
  }

  std::string executable;
  std::string linkerOptions;
  std::string linkerType = "auto";
  SplitExecutableAndFlags(linkCmd, executable, linkerOptions);

  // Now detect the extra dependencies for linking
  std::vector<std::string> dependencies;
  DetectTargetObjectDependencies(configName, dependencies);

  std::for_each(dependencies.begin(), dependencies.end(),
                UnescapeFastbuildVariables);

  // TODO: Select linker compiler
  linkerNode.Compiler = ".Compiler_dummy";
  if (!objectLists.empty()) {
    linkerNode.CompilerOptions = objectLists.front().CompilerOptions;
  }
  linkerNode.Name = targetName;
  linkerNode.Linker = executable;
  linkerNode.LinkerType = linkerType;
  linkerNode.LinkerOutput = targetOutput;
  linkerNode.LinkerOptions = linkerOptions;
  linkerNode.Libraries = dependencies;
  for (const auto& objectList : objectLists)
    linkerNode.Libraries.push_back(objectList.Name);

  return { linkerNode };
}

void cmFastbuildNormalTargetGenerator::Generate()
{
  // This time to define linker settings for each config
  const std::string& configName = this->GetConfigName();

  cmGlobalFastbuildGenerator::FastbuildTarget fastbuildTarget;
  fastbuildTarget.Name = GeneratorTarget->GetName();

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

  const std::string objPath = GetGeneratorTarget()->GetSupportDirectory();
  fastbuildTarget.Variables["TargetOutDir"] =
    "\"" + ConvertToFastbuildPath(objPath) + "\"";

  if (GeneratorTarget->GetType() == cmStateEnums::EXECUTABLE ||
      GeneratorTarget->GetType() == cmStateEnums::STATIC_LIBRARY ||
      GeneratorTarget->GetType() == cmStateEnums::SHARED_LIBRARY ||
      GeneratorTarget->GetType() == cmStateEnums::MODULE_LIBRARY) {
    std::string targetOutPDBPath =
      GeneratorTarget->GetPDBDirectory(configName) + "/" +
      GeneratorTarget->GetPDBName(configName);
    fastbuildTarget.Variables["TargetOutPDBPath"] =
      "\"" + ConvertToFastbuildPath(targetOutPDBPath) + "\"";
  }
  if (GeneratorTarget->GetType() <= cmStateEnums::OBJECT_LIBRARY) {
    std::string targetOutCompilePDBPath =
      GeneratorTarget->GetCompilePDBPath(configName);
    if (targetOutCompilePDBPath.empty()) {
      targetOutCompilePDBPath = GeneratorTarget->GetSupportDirectory() + "/" +
        GeneratorTarget->GetPDBName(configName);
    }
    targetOutCompilePDBPath =
      Makefile->GetHomeOutputDirectory() + "/" + targetOutCompilePDBPath;
    fastbuildTarget.Variables["TargetOutCompilePDBPath"] =
      "\"" + ConvertToFastbuildPath(targetOutCompilePDBPath) + "\"";
  }
  fastbuildTarget.Variables["TargetOutputImplib"] = "\"" +
    ConvertToFastbuildPath(GeneratorTarget->GetFullPath(
      configName, cmStateEnums::ImportLibraryArtifact)) +
    "\"";

  fastbuildTarget.PreBuildExecNodes = GenerateCommands("PreBuild");
  fastbuildTarget.PreLinkExecNodes = GenerateCommands("PreLink");
  fastbuildTarget.PostBuildExecNodes = GenerateCommands("PostBuild");
  fastbuildTarget.ExecNodes = GenerateCommands();
  fastbuildTarget.ObjectListNodes = GenerateObjects();
  fastbuildTarget.LinkerNodes = GenerateLink(fastbuildTarget.ObjectListNodes);

#ifdef _WIN32
  std::string targetName = GeneratorTarget->GetName();
  FastbuildTargetNames targetNames;
  DetectOutput(targetNames, configName);

  std::string targetCompileOutDirectory =
    this->GeneratorTarget->GetSupportDirectory();
  auto& VCXProject = fastbuildTarget.VCXProjects.emplace_back();
  VCXProject.UserProps =
    this->GeneratorTarget->GetSafeProperty("VS_USER_PROPS");
  cmSystemTools::ReplaceString(VCXProject.UserProps, "/", "\\");

  VCXProject.LocalDebuggerCommand = targetNames.targetOutput;
  VCXProject.LocalDebuggerCommandArguments =
    this->GeneratorTarget->GetSafeProperty("VS_DEBUGGER_COMMAND_ARGUMENTS");

  VCXProject.Name = targetName + "-vcxproj";
  VCXProject.ProjectOutput = ConvertToFastbuildPath(
    targetCompileOutDirectory + "/" + targetName + ".vcxproj");
  VCXProject.Platform = "X64";
  VCXProject.Config = configName;
  VCXProject.Target = targetName;
  VCXProject.Folder = GeneratorTarget->GetSafeProperty("FOLDER");

  std::vector<cmSourceGroup> sourceGroups = this->Makefile->GetSourceGroups();
  for (const BT<cmSourceFile*>& source :
       GeneratorTarget->GetSourceFiles(configName)) {
    cmSourceGroup* sourceGroup = this->Makefile->FindSourceGroup(
      source.Value->ResolveFullPath(), sourceGroups);
    VCXProject.ProjectFiles[sourceGroup->GetFullName()].push_back(
      ConvertToFastbuildPath(source.Value->GetFullPath()));
  }
  std::string cmakeCommand = this->GetLocalGenerator()->ConvertToOutputFormat(
    cmSystemTools::GetCMakeCommand(), cmLocalGenerator::SHELL);
  VCXProject.ProjectBuildCommand = cmStrCat(
    cmakeCommand, " --build ",
    GetGlobalGenerator()->GetLocalGenerators()[0]->GetCurrentBinaryDirectory(),
    " --target \"", targetName, "\" --config ", configName);
  VCXProject.ProjectRebuildCommand =
    cmStrCat(VCXProject.ProjectBuildCommand, " -- -clean");
#endif

  fastbuildTarget.IsGlobal =
    GeneratorTarget->GetType() == cmStateEnums::GLOBAL_TARGET;
  fastbuildTarget.IsExcluded =
    GetGlobalGenerator()->IsExcluded(GeneratorTarget);

  cmGeneratorTarget::ModuleDefinitionInfo const* mdi =
    GeneratorTarget->GetModuleDefinitionInfo(this->GetConfigName());
  if (mdi && mdi->DefFileGenerated) {
    cmGlobalFastbuildGenerator::FastbuildExecNode execNode;
    execNode.Name = fastbuildTarget.Name + "-def-files";
    execNode.ExecExecutable = cmSystemTools::GetCMakeCommand();
    execNode.ExecArguments = cmStrCat(
      "-E __create_def ",
      FASTBUILD_DOLLAR_TAG "FB_INPUT_2_PLACEHOLDER" FASTBUILD_DOLLAR_TAG, " ",
      FASTBUILD_DOLLAR_TAG "FB_INPUT_1_PLACEHOLDER" FASTBUILD_DOLLAR_TAG);
    std::string obj_list_file = mdi->DefFile + ".objs";

    const auto nm_executable = GetMakefile()->GetSafeDefinition("CMAKE_NM");
    if (!nm_executable.empty()) {
      execNode.ExecArguments += " --nm=";
      execNode.ExecArguments += ConvertToFastbuildPath(nm_executable);
    }
    execNode.ExecOutput = ConvertToFastbuildPath(mdi->DefFile);
    execNode.ExecInput.push_back(ConvertToFastbuildPath(obj_list_file));

    fastbuildTarget.PreLinkExecNodes.push_back(execNode);

    // create a list of obj files for the -E __create_def to read
    cmGeneratedFileStream fout(obj_list_file);

    if (mdi->WindowsExportAllSymbols) {
      std::vector<cmSourceFile const*> objectSources;
      GeneratorTarget->GetObjectSources(objectSources, configName);
      std::map<cmSourceFile const*, std::string> mapping;
      for (cmSourceFile const* it : objectSources) {
        mapping[it];
      }
      GeneratorTarget->LocalGenerator->ComputeObjectFilenames(mapping,
                                                              GeneratorTarget);

      std::vector<std::string> objs;
      for (cmSourceFile const* it : objectSources) {
        const auto& v = mapping[it];
        std::string objFile = GeneratorTarget->ObjectDirectory + v;
        objs.push_back(objFile);
      }

      std::vector<cmSourceFile const*> externalObjectSources;
      GeneratorTarget->GetExternalObjects(externalObjectSources, configName);
      for (cmSourceFile const* it : externalObjectSources) {
        objs.push_back(it->GetFullPath());
      }

      for (std::string const& objFile : objs) {
        if (cmHasLiteralSuffix(objFile, ".obj")) {
          fout << objFile << "\n";
        }
      }
    }
    for (cmSourceFile const* src : mdi->Sources) {
      fout << src->GetFullPath() << "\n";
    }
  }

  cmGlobalFastbuildGenerator::FastbuildAliasNode objects;
  objects.Name = fastbuildTarget.Name + "-objects";
  for (const auto& object : fastbuildTarget.ObjectListNodes) {
    objects.Targets.insert(object.Name);
  }
  if (!objects.Targets.empty())
    fastbuildTarget.AliasNodes.push_back(objects);
  GetLocalGenerator()->AddTarget(fastbuildTarget);
}
