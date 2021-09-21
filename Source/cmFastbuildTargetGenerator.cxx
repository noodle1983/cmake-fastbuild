#include "cmFastbuildTargetGenerator.h"

#include "cmCustomCommand.h"
#include "cmCustomCommandGenerator.h"
#include "cmFastbuildNormalTargetGenerator.h"
#include "cmFastbuildUtilityTargetGenerator.h"
#include "cmGeneratedFileStream.h"
#include "cmGeneratorTarget.h"
#include "cmGlobalFastbuildGenerator.h"
#include "cmLocalFastbuildGenerator.h"
#include "cmMakefile.h"
#include "cmOSXBundleGenerator.h"
#include "cmRulePlaceholderExpander.h"
#include "cmSourceFile.h"

#define FASTBUILD_DOLLAR_TAG "FASTBUILD_DOLLAR_TAG"

cmFastbuildTargetGenerator::CustomCommandAliasMap
  cmFastbuildTargetGenerator::s_customCommandAliases;

cmFastbuildTargetGenerator* cmFastbuildTargetGenerator::New(
  cmGeneratorTarget* target)
{
  switch (target->GetType()) {
    case cmStateEnums::EXECUTABLE:
    case cmStateEnums::SHARED_LIBRARY:
    case cmStateEnums::STATIC_LIBRARY:
    case cmStateEnums::MODULE_LIBRARY:
    case cmStateEnums::OBJECT_LIBRARY:
      return new cmFastbuildNormalTargetGenerator(target);

    case cmStateEnums::UTILITY:
    case cmStateEnums::GLOBAL_TARGET:
      return new cmFastbuildUtilityTargetGenerator(target);

    default:
      return nullptr;
  }
}

cmFastbuildTargetGenerator::cmFastbuildTargetGenerator(
  cmGeneratorTarget* target)
  : cmCommonTargetGenerator(target)
  , LocalGenerator(
      static_cast<cmLocalFastbuildGenerator*>(target->GetLocalGenerator()))
  , MacOSXContentGenerator(nullptr)
  , OSXBundleGenerator(nullptr)
{
  MacOSXContentGenerator = new MacOSXContentGeneratorType(this);
}

cmFastbuildTargetGenerator::~cmFastbuildTargetGenerator()
{
  delete this->MacOSXContentGenerator;
}

std::string cmFastbuildTargetGenerator::GetConfigName()
{
  auto const& configNames = this->LocalGenerator->GetConfigNames();
  assert(configNames.size() == 1);
  return configNames.front();
}

void cmFastbuildTargetGenerator::MacOSXContentGeneratorType::operator()(
  cmSourceFile const& source, const char* pkgloc, const std::string& config)
{
  // Skip OS X content when not building a Framework or Bundle.
  if (!this->Generator->GetGeneratorTarget()->IsBundleOnApple()) {
    return;
  }

  std::string macdir =
    this->Generator->OSXBundleGenerator->InitMacOSXContentDirectory(pkgloc,
                                                                    config);

  // Get the input file location.
  std::string input = source.GetFullPath();
  input = this->Generator->GetGlobalGenerator()->ConvertToFastbuildPath(input);

  // Get the output file location.
  std::string output = macdir;
  output += "/";
  output += cmSystemTools::GetFilenameName(input);
  output =
    this->Generator->GetGlobalGenerator()->ConvertToFastbuildPath(output);

  // Write a build statement to copy the content into the bundle.
  cmGlobalFastbuildGenerator::WriteCommand(
    this->Generator->GetBuildFileStream(), "Copy",
    cmGlobalFastbuildGenerator::Quote(output), 1);
  cmGlobalFastbuildGenerator::Indent(this->Generator->GetBuildFileStream(), 1);
  this->Generator->GetBuildFileStream() << "{\n";
  cmGlobalFastbuildGenerator::WriteVariable(
    this->Generator->GetBuildFileStream(), "Source",
    cmGlobalFastbuildGenerator::Quote(input), 2);
  cmGlobalFastbuildGenerator::WriteVariable(
    this->Generator->GetBuildFileStream(), "Dest",
    cmGlobalFastbuildGenerator::Quote(output), 2);
  cmGlobalFastbuildGenerator::Indent(this->Generator->GetBuildFileStream(), 1);
  this->Generator->GetBuildFileStream() << "}\n";

  this->Generator->ExtraFiles.push_back(output);
}

void cmFastbuildTargetGenerator::UnescapeFastbuildVariables(
  std::string& string)
{
  // Unescape the Fastbuild configName symbol with $
  cmSystemTools::ReplaceString(string, "^", "^^");
  cmSystemTools::ReplaceString(string, "$$", "^$");
  cmSystemTools::ReplaceString(string, FASTBUILD_DOLLAR_TAG, "$");
}

void cmFastbuildTargetGenerator::UnescapeFastbuildDefines(std::string& string)
{
  std::string sep = "  ";
  std::vector<std::string> chunks;
  size_t pos = 0;
  for (std::string token; (pos = string.find(sep)) != std::string::npos;) {
    token = string.substr(0, pos);
    chunks.push_back(token);
    string.erase(0, pos + sep.length());
  }
  chunks.push_back(string);

  string = "";
  sep = "";
  for (const auto& chunk : chunks) {
    if (!sep.empty() && !chunk.empty())
      sep += "\\\"";
    string += sep + chunk;
    sep = "\\\" \\\"\\\" \\\"";
    if (!chunk.empty())
      sep = "\\\"" + sep;
  }
}

std::string cmFastbuildTargetGenerator::GetCustomCommandTargetName(
  const cmCustomCommand& cc, const std::string& extra) const
{
  std::string targetName = "cc";

  const std::vector<std::string>& outputs = cc.GetOutputs();
  const std::vector<std::string>& byproducts = cc.GetByproducts();
  std::vector<std::string> mergedOutputs;
  mergedOutputs.insert(mergedOutputs.end(), outputs.begin(), outputs.end());
  mergedOutputs.insert(mergedOutputs.end(), byproducts.begin(),
                       byproducts.end());

  std::string extras = extra;
  // If this exec node always generates outputs,
  // then we need to make sure we don't define outputs multiple times.
  // but if the command should always run (i.e. post builds etc)
  // then we will output a new one.
  // when generating output file, makes realpath as part of targetName
  // to make it unique
  for (const auto& output : mergedOutputs) {
    std::string relPath = this->ConvertToFastbuildPath(output);
    extras += "-" + relPath;
  }

  cmCryptoHash hash(cmCryptoHash::AlgoSHA256);
  targetName += "-" + hash.HashString(extras).substr(0, 7);

  return targetName;
}

bool cmFastbuildTargetGenerator::isConfigDependant(
  const cmCustomCommandGenerator* ccg)
{
  typedef std::vector<std::string> StringVector;
  StringVector outputs = ccg->GetOutputs();
  StringVector byproducts = ccg->GetByproducts();

  std::for_each(outputs.begin(), outputs.end(), &UnescapeFastbuildVariables);
  std::for_each(byproducts.begin(), byproducts.end(),
                &UnescapeFastbuildVariables);

  // Make sure that the outputs don't depend on the config name
  for (StringVector::const_iterator iter = outputs.begin();
       iter != outputs.end(); ++iter) {
    const std::string& str = *iter;
    if (str.find("$ConfigName$") != std::string::npos) {
      return true;
    }
  }
  for (StringVector::const_iterator iter = byproducts.begin();
       iter != byproducts.end(); ++iter) {
    const std::string& str = *iter;
    if (str.find("$ConfigName$") != std::string::npos) {
      return true;
    }
  }

  return false;
}

void cmFastbuildTargetGenerator::ResolveFastbuildVariables(
  std::string& string, const std::string& configName)
{ // Replace Fastbuild configName with the config name
  cmSystemTools::ReplaceString(string, "$ConfigName$", configName);
}

std::vector<cmGlobalFastbuildGenerator::FastbuildExecNode>
cmFastbuildTargetGenerator::GenerateCommands(const std::string& buildStep)
{
  std::vector<cmGlobalFastbuildGenerator::FastbuildExecNode> nodes;

  const std::string& configName = this->GetConfigName();
  const std::string& hostTargetName = GeneratorTarget->GetName();

  std::vector<cmCustomCommand> commands;
  if (buildStep == "PreBuild")
    commands = GeneratorTarget->GetPreBuildCommands();
  else if (buildStep == "PreLink")
    commands = GeneratorTarget->GetPreLinkCommands();
  else if (buildStep == "PostBuild")
    commands = GeneratorTarget->GetPostBuildCommands();
  else {
    std::vector<cmSourceFile const*> customCommands;
    GeneratorTarget->GetCustomCommands(customCommands, configName);
    std::unordered_multimap<cmSourceFile const*, cmSourceFile const*>
      dependencies;
    for (cmSourceFile const* source : customCommands) {
      cmCustomCommandGenerator ccg(*source->GetCustomCommand(), configName,
                                   LocalCommonGenerator);
      for (const std::string& dep : ccg.GetDepends()) {
        // Check if we know how to generate this file.
        cmSourcesWithOutput sources =
          this->LocalGenerator->GetSourcesWithOutput(dep);
        // If we failed to find a target or source and we have a relative path,
        // it might be a valid source if made relative to the current binary
        // directory.
        if (!sources.Target && !sources.Source &&
            !cmSystemTools::FileIsFullPath(dep)) {
          auto fullname =
            cmStrCat(this->Makefile->GetCurrentBinaryDirectory(), '/', dep);
          fullname = cmSystemTools::CollapseFullPath(
            fullname, this->Makefile->GetHomeOutputDirectory());
          sources = this->LocalGenerator->GetSourcesWithOutput(fullname);
        }

        // If this dependency comes from a custom command, add that command to
        // the dependencies list
        if (sources.Source) {
          auto command =
            std::find_if(customCommands.begin(), customCommands.end(),
                         [src = sources.Source](cmSourceFile const* source) {
                           return src == source;
                         });
          // Found and not self
          if (command != customCommands.end() && source != *command) {
            dependencies.emplace(source, *command);
          }
        }
      }
    }

    cmGlobalFastbuildGenerator::SortByDependencies(customCommands,
                                                   dependencies);

    for (cmSourceFile const* source : customCommands)
      commands.emplace_back(*source->GetCustomCommand());
  }

  int i = 0;
  for (const cmCustomCommand& cc : commands) {
    // We need to generate the command for execution.
    cmCustomCommandGenerator ccg(cc, configName, LocalCommonGenerator);

    std::string targetName;
    if (!buildStep.empty()) {
      targetName = Makefile->GetCurrentBinaryDirectory();
      targetName += "/";
      targetName += GeneratorTarget->GetName();
      targetName = this->ConvertToFastbuildPath(targetName);
      targetName += "_" + buildStep + "_" + std::to_string(++i);
    }
    targetName = GetCustomCommandTargetName(cc, targetName);
    std::vector<std::string> inputs;
    // Take the dependencies listed and split into targets and files.
    for (const std::string& dep : ccg.GetDepends()) {
      cmTarget* target = GlobalCommonGenerator->FindTarget(dep);
      std::string realDep;
      GetLocalGenerator()->GetRealDependency(dep, this->GetConfigName(),
                                             realDep);
      inputs.push_back(realDep);
    }

    std::vector<std::string> cmdLines;
    if (ccg.GetNumberOfCommands() > 0) {
      std::string wd = ccg.GetWorkingDirectory();
      if (wd.empty()) {
        wd = this->LocalGenerator->GetCurrentBinaryDirectory();
      }

      std::ostringstream cdCmd;
#ifdef _WIN32
      std::string cdStr = "cd /D ";
#else
      std::string cdStr = "cd ";
#endif
      cdCmd << cdStr
            << this->LocalGenerator->ConvertToOutputFormat(
                 wd, cmOutputConverter::SHELL);
      cmdLines.push_back(cdCmd.str());
    }

    std::string launcher;

    const cmProp property_value =
      this->Makefile->GetProperty("RULE_LAUNCH_CUSTOM");

    if (property_value && !property_value->empty()) {
      // Expand rule variables referenced in the given launcher command.
      cmRulePlaceholderExpander::RuleVariables vars;

      std::string output;
      const std::vector<std::string>& outputs = ccg.GetOutputs();
      if (!outputs.empty()) {
        output = outputs[0];
        if (ccg.GetWorkingDirectory().empty()) {
          output = this->LocalGenerator->MaybeConvertToRelativePath(
            this->LocalGenerator->GetCurrentBinaryDirectory(), output);
        }
        output = this->LocalGenerator->ConvertToOutputFormat(
          output, cmOutputConverter::SHELL);
      }
      vars.Output = output.c_str();

      std::unique_ptr<cmRulePlaceholderExpander> rulePlaceholderExpander(
        this->LocalGenerator->CreateRulePlaceholderExpander());

      std::string launcher = *property_value;
      rulePlaceholderExpander->ExpandRuleVariables(this->LocalGenerator,
                                                   launcher, vars);
      if (!launcher.empty()) {
        launcher += " ";
      }
    }

    for (unsigned i = 0; i != ccg.GetNumberOfCommands(); ++i) {
      cmdLines.push_back(launcher +
                         this->LocalGenerator->ConvertToOutputFormat(
                           ccg.GetCommand(i), cmOutputConverter::SHELL));

      std::string& cmd = cmdLines.back();
      ccg.AppendArguments(i, cmd);
    }

    std::for_each(inputs.begin(), inputs.end(), &UnescapeFastbuildVariables);

    cmGlobalFastbuildGenerator::FastbuildExecNode execNode;
    execNode.Name = targetName;
    execNode.IsNoop = cmdLines.empty();

    std::vector<std::string> outputs;
    for (std::string const& output : ccg.GetOutputs()) {
      if (cmSourceFile* sf = this->Makefile->GetSource(output)) {
        if (!sf->GetPropertyAsBool("SYMBOLIC")) {
          outputs.push_back(output);
        }
      }
    }
    execNode.ExecAlways = inputs.empty();
    for (std::string const& output : cc.GetByproducts()) {
      if (cmSourceFile* sf = this->Makefile->GetSource(output)) {
        if (!sf->GetPropertyAsBool("SYMBOLIC")) {
          outputs.push_back(output);
        }
      }
    }

    if (!execNode.IsNoop) {
      std::string scriptFileName = Makefile->GetCurrentBinaryDirectory();
      scriptFileName += "/CMakeFiles";
      scriptFileName += "/";
      scriptFileName += targetName;

#ifdef _WIN32
      scriptFileName += ".bat";
#else
      scriptFileName += ".sh";
#endif

      cmsys::ofstream scriptFile(scriptFileName.c_str());

#ifdef _WIN32
      scriptFile << "@echo off\n";
      int line = 1;
#else
      scriptFile << "set -e\n\n";
#endif
      std::string output;
      if (outputs.size() == 1) {
        output = outputs[0];
      } else {
        // Currently fastbuild doesn't support more than 1
        // output for a custom command (soon to change hopefully).
        std::string outputDir =
          LocalCommonGenerator->GetMakefile()->GetHomeOutputDirectory();
        output = outputDir + "/dummy-out-" + targetName + ".txt";
        std::string cmakeCommand =
          this->GetLocalGenerator()->ConvertToOutputFormat(
            cmSystemTools::GetCMakeCommand(), cmOutputConverter::SHELL);
        cmdLines.push_back(cmakeCommand + " -E touch " + output);
        // Forward command output to the file
        execNode.ExecUseStdOutAsOutput = true;
      }
      execNode.ExecOutput = ConvertToFastbuildPath(output);

      for (auto cmd : cmdLines) {
        cmSystemTools::ReplaceString(cmd, "$$", "$");
        cmSystemTools::ReplaceString(cmd, FASTBUILD_DOLLAR_TAG, "$");
#ifdef _WIN32
        // in windows batch, '%' is a special character that needs to be
        // doubled to be escaped
        cmSystemTools::ReplaceString(cmd, "%", "%%");
#endif
        ResolveFastbuildVariables(cmd, configName);
#ifdef _WIN32
        scriptFile << cmd << " || (set FAIL_LINE=" << ++line
                   << "& goto :ABORT)" << '\n';
#else
        scriptFile << cmd << '\n';
#endif
      }

#ifdef _WIN32
      scriptFile << "goto :EOF\n\n"
                    ":ABORT\n"
                    "set ERROR_CODE=%ERRORLEVEL%\n"
                    "echo Batch file failed at line %FAIL_LINE% "
                    "with errorcode %ERRORLEVEL%\n"
                    "exit /b %ERROR_CODE%";
#endif

#ifdef _WIN32
      execNode.ExecExecutable = cmSystemTools::FindProgram("cmd.exe");
      execNode.ExecArguments = "/C " + scriptFileName;
#else
      execNode.ExecExecutable = ConvertToFastbuildPath(scriptFileName);
#endif
      std::string workingDirectory = ccg.GetWorkingDirectory();
      if (workingDirectory.empty()) {
          workingDirectory =
              this->LocalCommonGenerator->GetCurrentBinaryDirectory();
      }
      if (!workingDirectory.empty()) {
        execNode.ExecWorkingDir = workingDirectory;
      }
    }

    inputs.erase(std::remove_if(inputs.begin(), inputs.end(),
                                [](const auto& s) { return s.empty(); }),
                 inputs.end());
    execNode.ExecInput = GetGlobalGenerator()->ConvertToFastbuildPath(inputs);

    // Make sure we execute in order if it's a buildStep
    if (!buildStep.empty() && !nodes.empty())
      execNode.PreBuildDependencies.insert(nodes.back().Name);

    for (const std::string& dep : ccg.GetDepends()) {
      auto depFilePath = dep;
      if (!cmSystemTools::FileIsFullPath(depFilePath)) {
        depFilePath = cmSystemTools::CollapseFullPath(
          cmStrCat(this->Makefile->GetCurrentSourceDirectory(), '/', dep));
      }
      // If this dependency comes from a custom command, add that command to
      // the dependencies list
      auto command = std::find_if(
        commands.begin(), commands.end(),
        [&depFilePath](cmCustomCommand const& cc) {
          const std::vector<std::string>& outputs = cc.GetOutputs();
          return std::find(outputs.begin(), outputs.end(), depFilePath) !=
            outputs.end();
        });

      if (command != commands.end()) {
        execNode.PreBuildDependencies.insert(
          GetCustomCommandTargetName(*command));
      }
    }

    nodes.push_back(execNode);

    if (outputs.size() > 1) {
      for (const auto& output : outputs) {
        cmGlobalFastbuildGenerator::FastbuildExecNode noop;
        noop.Name = execNode.Name;
        cmCryptoHash hash(cmCryptoHash::AlgoSHA256);
        noop.Name += "-" + hash.HashString(output).substr(0, 7);
        noop.PreBuildDependencies.insert(execNode.Name);
        noop.ExecInput.push_back(execNode.ExecOutput);
        noop.ExecOutput = ConvertToFastbuildPath(output);
        std::string cmakeCommand =
          this->GetLocalGenerator()->ConvertToOutputFormat(
            cmSystemTools::GetCMakeCommand(), cmOutputConverter::SHELL);
        noop.ExecExecutable = cmakeCommand;
        noop.ExecArguments = " -E touch " + output;
        noop.ExecWorkingDir = execNode.ExecWorkingDir;
      }
    }
  }

  return nodes;
}

std::string cmFastbuildTargetGenerator::GetTargetName() const
{
  return this->GeneratorTarget->GetName();
}

void cmFastbuildTargetGenerator::AddIncludeFlags(std::string&,
                                                 std::string const&,
                                                 const std::string&)
{
}

std::string cmFastbuildTargetGenerator::GetName()
{
  return GeneratorTarget->GetName();
}

std::string cmFastbuildTargetGenerator::ConvertToFastbuildPath(
  const std::string& path) const
{
  return GetGlobalGenerator()->ConvertToFastbuildPath(path);
}

cmGlobalFastbuildGenerator* cmFastbuildTargetGenerator::GetGlobalGenerator()
  const
{
  return this->LocalGenerator->GetGlobalFastbuildGenerator();
}
