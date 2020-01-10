#ifndef cmFastbuildTargetGenerator_h
#define cmFastbuildTargetGenerator_h

#include "cmConfigure.h" // IWYU pragma: keep

#include <set>

#include "cmCommonTargetGenerator.h"
#include "cmGlobalFastbuildGenerator.h"
#include "cmOSXBundleGenerator.h"

class cmCustomCommandGenerator;
class cmGeneratedFileStream;
class cmLocalFastbuildGenerator;

class cmFastbuildTargetGenerator : public cmCommonTargetGenerator
{
public:
  /// Create a cmFastbuildTargetGenerator according to the @a target's type.
  static cmFastbuildTargetGenerator* New(cmGeneratorTarget* target);

  cmFastbuildTargetGenerator(cmGeneratorTarget* target);
  ~cmFastbuildTargetGenerator() override;

  virtual void Generate() {}

  void AddIncludeFlags(std::string& flags, std::string const& lang,
                       const std::string& config) override;

  std::string ConvertToFastbuildPath(const std::string& path) const;

  cmGlobalFastbuildGenerator* GetGlobalGenerator() const;

  std::string GetName();

  virtual std::vector<std::string> GetLanguages()
  {
    return std::vector<std::string>();
  }

  cmMakefile* GetMakefile() const { return this->Makefile; }

  cmGeneratorTarget* GetGeneratorTarget() const
  {
    return this->GeneratorTarget;
  }

  std::string GetConfigName();

protected:
  cmGeneratedFileStream& GetBuildFileStream() const
  {
    return *this->GetGlobalGenerator()->GetBuildFileStream();
  }

  cmLocalFastbuildGenerator* GetLocalGenerator() const
  {
    return this->LocalGenerator;
  }

  std::string GetTargetName() const;

  std::vector<cmGlobalFastbuildGenerator::FastbuildExecNode> GenerateCommands(
    const std::string& buildStep = "");

  std::string GetCustomCommandTargetName(const cmCustomCommand& cc,
                                         const std::string& extra = "") const;

  static bool isConfigDependant(const cmCustomCommandGenerator* ccg);

  static void UnescapeFastbuildVariables(std::string& string);
  static void UnescapeFastbuildDefines(std::string& string);

  static void ResolveFastbuildVariables(std::string& string,
                                        const std::string& configName);

  // write rules for Mac OS X Application Bundle content.
  struct MacOSXContentGeneratorType
    : cmOSXBundleGenerator::MacOSXContentGeneratorType
  {
    MacOSXContentGeneratorType(cmFastbuildTargetGenerator* g)
      : Generator(g)
    {
    }

    void operator()(cmSourceFile const& source, const char* pkgloc,
                    const std::string& config) override;

  private:
    cmFastbuildTargetGenerator* Generator;
  };
  friend struct MacOSXContentGeneratorType;

  MacOSXContentGeneratorType* MacOSXContentGenerator;
  // Properly initialized by sub-classes.
  cmOSXBundleGenerator* OSXBundleGenerator;
  std::set<std::string> MacContentFolders;

  std::vector<std::string> ExtraFiles;

private:
  cmLocalFastbuildGenerator* LocalGenerator;

  typedef std::map<std::pair<const cmCustomCommand*, std::string>,
                   std::set<std::string>>
    CustomCommandAliasMap;

  static CustomCommandAliasMap s_customCommandAliases;
};

#endif // cmFastbuildTargetGenerator_h
